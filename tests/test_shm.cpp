// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/shm_protocol.h"
#include "studio/client.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define TEST_SHM_NAME "/test_studio_ai"

static void wake_futex(std_atomic_u32* word) {
    word->fetch_add(1, std::memory_order_release);
    syscall(SYS_futex, (uint32_t*)word, FUTEX_WAKE, INT32_MAX, NULL, NULL, 0);
}

int main() {
    printf("Running SHM unit test...\n");
    shm_unlink(TEST_SHM_NAME);

    const uint32_t w = 320, h = 238;
    const uint32_t payload_bytes = w * h * 3 / 2;
    const uint32_t stride = (sizeof(StudioSlotHeader) + payload_bytes + 7) & ~7;
    const size_t total_size = sizeof(StudioShmHeader) + (size_t)STUDIO_SHM_MAX_SLOTS * stride;

    int fd = shm_open(TEST_SHM_NAME, O_CREAT | O_RDWR, 0666);
    assert(fd >= 0);
    assert(ftruncate(fd, total_size) == 0);

    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(ptr != MAP_FAILED);
    memset(ptr, 0, total_size);

    StudioShmHeader* header = (StudioShmHeader*)ptr;
    memcpy(header->magic, STUDIO_SHM_MAGIC, 8);
    header->version = STUDIO_SHM_VERSION;
    header->num_slots = STUDIO_SHM_MAX_SLOTS;
    header->width = w;
    header->height = h;
    header->slot_stride_bytes = stride;
    header->writer_pid = getpid();
    header->session_id = 12345;
    header->latest_seq.store(0);
    header->futex_word.store(0);
    header->reader_heartbeat_ns.store(0);

    // Test 1: FIFO reading 500 frames sequentially (open client BEFORE writer to ensure baseline=0)
    StudioClient* client = studio_client_open(TEST_SHM_NAME, STUDIO_READ_FIFO);
    assert(client != NULL);

    std::thread writer([&] {
        std::vector<uint8_t> frame_data(payload_bytes);
        for (uint64_t seq = 1; seq <= 500; ++seq) {
            uint32_t slot_idx = seq % STUDIO_SHM_MAX_SLOTS;
            uint8_t* slot_base = (uint8_t*)ptr + sizeof(StudioShmHeader) + slot_idx * stride;
            StudioSlotHeader* slot = (StudioSlotHeader*)slot_base;
            uint8_t* payload = slot_base + sizeof(StudioSlotHeader);

            slot->seqlock.fetch_add(1, std::memory_order_release); // odd
            slot->sequence = seq;
            slot->sensor_timestamp_ns = seq * 8333333ULL;
            slot->host_timestamp_ns = seq * 8333333ULL;
            slot->exposure_ns = 4000000;
            slot->analog_gain = 2.0f;
            slot->isp_digital_gain = 1.0f;
            slot->awb_cct = 5000;
            slot->flags = 0;
            slot->width = w;
            slot->height = h;
            slot->payload_bytes = payload_bytes;

            uint32_t magic_val = (uint32_t)seq;
            memcpy(payload, &magic_val, sizeof(magic_val));

            slot->seqlock.fetch_add(1, std::memory_order_release); // even
            header->latest_seq.store(seq, std::memory_order_release);
            wake_futex(&header->futex_word);

            std::this_thread::sleep_for(std::chrono::microseconds(100)); // fast simulation
        }
    });

    std::vector<uint8_t> buf(payload_bytes);
    StudioFrameMeta meta;
    for (uint64_t expected_seq = 1; expected_seq <= 500; ++expected_seq) {
        int r = studio_client_read(client, buf.data(), buf.size(), &meta, 1000);
        assert(r == 0);
        assert(meta.sequence == expected_seq);
        uint32_t val = 0;
        memcpy(&val, buf.data(), sizeof(val));
        assert(val == (uint32_t)expected_seq);
    }
    writer.join();
    assert(studio_client_get_dropped(client) == 0);
    studio_client_close(client);

    // Test 2: Timeout check (writer finished at 500, waits for fresh frame >500 which never arrives)
    client = studio_client_open(TEST_SHM_NAME, STUDIO_READ_FIFO);
    assert(client != NULL);
    auto t0 = std::chrono::steady_clock::now();
    int r = studio_client_read(client, buf.data(), buf.size(), &meta, 50);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    assert(r == 1); // timeout waiting for fresh frame
    assert(elapsed >= 40 && elapsed <= 150);
    studio_client_close(client);
    // Test 3: Forced Lapping Test (writer writes 100 frames while reader is paused)
    // 100 frames > 64 slots, so writer will lap the ring buffer
    client = studio_client_open(TEST_SHM_NAME, STUDIO_READ_FIFO);
    assert(client != NULL);

    auto write_slot = [&](uint64_t seq) {
        uint32_t slot_idx = seq % STUDIO_SHM_MAX_SLOTS;
        uint8_t* slot_base = (uint8_t*)ptr + sizeof(StudioShmHeader) + slot_idx * stride;
        StudioSlotHeader* slot = (StudioSlotHeader*)slot_base;
        uint8_t* payload = slot_base + sizeof(StudioSlotHeader);
        slot->seqlock.fetch_add(1, std::memory_order_release);
        slot->sequence = seq;
        slot->sensor_timestamp_ns = seq * 8333333ULL;
        slot->host_timestamp_ns = seq * 8333333ULL;
        slot->width = w; slot->height = h; slot->payload_bytes = payload_bytes;
        uint32_t val = (uint32_t)seq;
        memcpy(payload, &val, sizeof(val));
        slot->seqlock.fetch_add(1, std::memory_order_release);
        header->latest_seq.store(seq, std::memory_order_release);
        wake_futex(&header->futex_word);
    };

    // Initial read establishes fresh baseline: frame 501
    write_slot(501);
    r = studio_client_read(client, buf.data(), buf.size(), &meta, 100);
    assert(r == 0 && meta.sequence == 501);
    assert(studio_client_get_dropped(client) == 0);

    // Writer laps reader by writing 100 frames (502..601) without reader reading
    for (uint64_t seq = 502; seq <= 601; ++seq) {
        write_slot(seq);
    }

    // Reader reads: must detect overrun, catch up to latest (601), and record exactly 99 dropped frames!
    r = studio_client_read(client, buf.data(), buf.size(), &meta, 100);
    assert(r == 2); // overrun reported!
    assert(meta.sequence == 601);
    assert(studio_client_get_dropped(client) == 99);

    // Subsequent read: sequential frame 602 with no further drops
    write_slot(602);
    r = studio_client_read(client, buf.data(), buf.size(), &meta, 100);
    assert(r == 0); // normal sequential read!
    assert(meta.sequence == 602);
    assert(studio_client_get_dropped(client) == 99);
    studio_client_close(client);

    munmap(ptr, total_size);
    close(fd);
    shm_unlink(TEST_SHM_NAME);

    printf("SHM unit test PASSED!\n");
    return 0;
}
