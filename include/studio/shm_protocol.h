// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_SHM_PROTOCOL_H
#define STUDIO_SHM_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <atomic>
using std_atomic_u64 = std::atomic<uint64_t>;
using std_atomic_u32 = std::atomic<uint32_t>;
#else
#include <stdatomic.h>
typedef _Atomic uint64_t std_atomic_u64;
typedef _Atomic uint32_t std_atomic_u32;
#endif

#define STUDIO_SHM_NAME "/imx477_studio_ai"
#define STUDIO_SHM_MAGIC "IMX477S1"
#define STUDIO_SHM_VERSION 1
#define STUDIO_SHM_MAX_SLOTS 64

#pragma pack(push, 8)

struct StudioSlotHeader {
    std_atomic_u64 seqlock;      // odd: writing; even: valid
    uint64_t sequence;           // camera frame number
    uint64_t sensor_timestamp_ns;// hardware sensor timestamp
    uint64_t host_timestamp_ns;  // CLOCK_MONOTONIC at acquireFrame
    uint64_t exposure_ns;        // actual exposure from metadata
    float analog_gain;           // actual analog gain
    float isp_digital_gain;      // actual ISP digital gain
    uint32_t awb_cct;            // correlated color temperature in K
    uint32_t flags;              // frame flags (0 = normal)
    uint32_t width;
    uint32_t height;
    uint32_t payload_bytes;      // NV12 = width * height * 3 / 2
};

struct StudioShmHeader {
    char magic[8];               // STUDIO_SHM_MAGIC
    uint32_t version;            // STUDIO_SHM_VERSION
    uint32_t num_slots;          // STUDIO_SHM_MAX_SLOTS
    uint32_t width;              // stream width (320 or 640)
    uint32_t height;             // stream height (238 or 476)
    uint32_t slot_stride_bytes;  // sizeof(StudioSlotHeader) + payload_bytes (aligned)
    uint32_t writer_pid;         // PID of current camera process
    uint64_t session_id;         // monotonic launch id
    std_atomic_u64 latest_seq;   // latest completed frame sequence
    std_atomic_u32 futex_word;   // futex wake target
    std_atomic_u64 reader_heartbeat_ns; // updated by readers; 0 = no readers
};

#pragma pack(pop)

#endif // STUDIO_SHM_PROTOCOL_H
