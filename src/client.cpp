// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/client.h"
#include "studio/shm_protocol.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <chrono>
#include <thread>
#include <signal.h>

static uint64_t client_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

struct StudioClient {
    int fd;
    size_t mapped_size;
    StudioShmHeader* header;
    StudioReadMode mode;
    uint64_t baseline_seq;
    uint64_t last_seq;
    uint64_t dropped_count;
    bool has_read_first;
};

static inline StudioSlotHeader* get_slot(StudioShmHeader* h, uint32_t index) {
    uint8_t* base = (uint8_t*)h;
    size_t offset = sizeof(StudioShmHeader) + (size_t)index * h->slot_stride_bytes;
    return (StudioSlotHeader*)(base + offset);
}

static inline uint8_t* get_slot_payload(StudioSlotHeader* slot) {
    return (uint8_t*)slot + sizeof(StudioSlotHeader);
}

static int wait_futex(std_atomic_u32* word, uint32_t val, int timeout_ms) {
    struct timespec ts;
    struct timespec* pts = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        pts = &ts;
    }
    int res = syscall(SYS_futex, (uint32_t*)word, FUTEX_WAIT, val, pts, NULL, 0);
    if (res == 0 || errno == EAGAIN) return 0;
    if (errno == ETIMEDOUT) return 1;
    return -1;
}

StudioClient* studio_client_open(const char* shm_name, StudioReadMode mode) {
    const char* name = shm_name ? shm_name : STUDIO_SHM_NAME;
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(StudioShmHeader)) {
        close(fd);
        return NULL;
    }

    void* ptr = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    StudioShmHeader* h = (StudioShmHeader*)ptr;
    if (memcmp(h->magic, STUDIO_SHM_MAGIC, 8) != 0 || h->version != STUDIO_SHM_VERSION) {
        munmap(ptr, st.st_size);
        close(fd);
        return NULL;
    }
    if (h->num_slots == 0 || h->slot_stride_bytes == 0 ||
        st.st_size < (off_t)(sizeof(StudioShmHeader) + (size_t)h->num_slots * h->slot_stride_bytes)) {
        munmap(ptr, st.st_size);
        close(fd);
        return NULL;
    }


    StudioClient* client = (StudioClient*)calloc(1, sizeof(StudioClient));
    if (!client) {
        munmap(ptr, st.st_size);
        close(fd);
        return NULL;
    }

    client->fd = fd;
    client->mapped_size = st.st_size;
    client->header = h;
    client->mode = mode;
    // Send initial reader heartbeat to awaken writer
    h->reader_heartbeat_ns.store(client_now_ns(), std::memory_order_relaxed);

    client->baseline_seq = h->latest_seq.load(std::memory_order_acquire);
    client->last_seq = client->baseline_seq;
    client->dropped_count = 0;
    client->has_read_first = false;
    return client;
}

void studio_client_close(StudioClient* client) {
    if (!client) return;
    if (client->header) {
        munmap(client->header, client->mapped_size);
    }
    if (client->fd >= 0) {
        close(client->fd);
    }
    free(client);
}

int studio_client_get_format(StudioClient* client, uint32_t* width, uint32_t* height) {
    if (!client || !client->header) return -1;
    if (width) *width = client->header->width;
    if (height) *height = client->header->height;
    return 0;
}

uint64_t studio_client_get_dropped(StudioClient* client) {
    return client ? client->dropped_count : 0;
}

int studio_client_read(StudioClient* client, void* out_buffer, size_t buffer_size,
                       StudioFrameMeta* out_meta, int timeout_ms) {
    if (!client || !client->header || !out_buffer || !out_meta) return -1;

    StudioShmHeader* h = client->header;
    h->reader_heartbeat_ns.store(client_now_ns(), std::memory_order_relaxed);

    uint64_t deadline_ns = timeout_ms >= 0 ? (client_now_ns() + (uint64_t)timeout_ms * 1000000ULL) : UINT64_MAX;

    int overrun_flag = 0;
    uint64_t target_seq = 0;


    while (true) {
        if (h->writer_pid > 0 && kill((pid_t)h->writer_pid, 0) != 0 && errno == ESRCH) {
            return -1; // Camera service died
        }
        uint64_t latest = h->latest_seq.load(std::memory_order_acquire);
        if (latest == 0) {
            // No frame has been produced yet
            if (timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
            uint32_t cur_val = h->futex_word.load(std::memory_order_relaxed);
            wait_futex(&h->futex_word, cur_val, 10);
            continue;
        }

        if (!client->has_read_first) {
            // First read after opening: wait for a FRESH frame written after connection
            if (latest <= client->baseline_seq) {
                if (timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                uint32_t cur_val = h->futex_word.load(std::memory_order_relaxed);
                int r = wait_futex(&h->futex_word, cur_val, 10);
                if (r == 1 && timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                continue;
            }
            target_seq = latest;
            break;
        }

        if (client->mode == STUDIO_READ_LATEST) {
            if (latest <= client->last_seq) {
                // Already read latest, wait for next
                if (timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                uint32_t cur_val = h->futex_word.load(std::memory_order_relaxed);
                int r = wait_futex(&h->futex_word, cur_val, 10);
                if (r == 1 && timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                continue;
            }
            if (latest > client->last_seq + 1) {
                client->dropped_count += (latest - client->last_seq - 1);
            }
            target_seq = latest;
            break;
        } else {
            // FIFO mode
            target_seq = client->last_seq + 1;
            if (target_seq > latest) {
                // Waiting for target_seq to be written
                if (timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                uint32_t cur_val = h->futex_word.load(std::memory_order_relaxed);
                int r = wait_futex(&h->futex_word, cur_val, 10);
                if (r == 1 && timeout_ms >= 0 && client_now_ns() >= deadline_ns) return 1;
                continue;
            }
            // Check if target_seq has been overwritten by the writer ring
            if (latest >= target_seq + h->num_slots) {
                // Lapped by writer!
                uint64_t lost = latest - target_seq;
                client->dropped_count += lost;
                overrun_flag = 2;
                target_seq = latest; // catch up to latest
            }
            break;
        }
    }

    // Read the slot corresponding to target_seq with seqlock
    uint32_t slot_idx = (uint32_t)(target_seq % h->num_slots);
    StudioSlotHeader* slot = get_slot(h, slot_idx);
    uint8_t* payload = get_slot_payload(slot);

    int retries = 0;
    while (retries++ < 100) {
        uint64_t lock_before = slot->seqlock.load(std::memory_order_acquire);
        if (lock_before & 1) {
            std::this_thread::yield();
            continue;
        }

        // Copy metadata
        out_meta->sequence = slot->sequence;
        out_meta->sensor_timestamp_ns = slot->sensor_timestamp_ns;
        out_meta->host_timestamp_ns = slot->host_timestamp_ns;
        out_meta->exposure_ns = slot->exposure_ns;
        out_meta->analog_gain = slot->analog_gain;
        out_meta->isp_digital_gain = slot->isp_digital_gain;
        out_meta->awb_cct = slot->awb_cct;
        out_meta->flags = slot->flags;
        out_meta->width = slot->width;
        out_meta->height = slot->height;
        out_meta->payload_bytes = slot->payload_bytes;

        if (buffer_size < slot->payload_bytes) {
            return -1; // buffer too small
        }

        memcpy(out_buffer, payload, slot->payload_bytes);

        uint64_t lock_after = slot->seqlock.load(std::memory_order_acquire);
        if (lock_before == lock_after) {
            if (out_meta->sequence != target_seq) {
                uint64_t cur_latest = h->latest_seq.load(std::memory_order_acquire);
                if (cur_latest > client->last_seq) {
                    client->dropped_count += (cur_latest - client->last_seq - 1);
                }
                overrun_flag = 2;
                target_seq = cur_latest;
                slot_idx = (uint32_t)(target_seq % h->num_slots);
                slot = get_slot(h, slot_idx);
                payload = get_slot_payload(slot);
                continue;
            }
            client->last_seq = target_seq;
            client->has_read_first = true;
            return overrun_flag;
        }
    }

    return -1; // failed to acquire consistent frame after retries
}
