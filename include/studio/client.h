// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_CLIENT_H
#define STUDIO_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StudioClient StudioClient;

typedef struct {
    uint64_t sequence;
    uint64_t sensor_timestamp_ns;
    uint64_t host_timestamp_ns;
    uint64_t exposure_ns;
    float analog_gain;
    float isp_digital_gain;
    uint32_t awb_cct;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t payload_bytes;
} StudioFrameMeta;

typedef enum {
    STUDIO_READ_LATEST = 0,
    STUDIO_READ_FIFO = 1
} StudioReadMode;

// Open connection to shared memory segment. Returns NULL on error.
StudioClient* studio_client_open(const char* shm_name, StudioReadMode mode);

// Close connection and unmap memory.
void studio_client_close(StudioClient* client);

// Read next frame. Blocks up to timeout_ms milliseconds (0 = non-blocking, <0 = infinite).
// Copies NV12 payload into out_buffer (must be at least meta->payload_bytes bytes).
// Fills out_meta with metadata.
// Returns:
//   0 on success
//   1 on timeout
//   2 on frame dropped / overrun (in FIFO mode; still returns the newest valid frame)
//  -1 on unrecoverable error (e.g. server died)
int studio_client_read(StudioClient* client, void* out_buffer, size_t buffer_size,
                       StudioFrameMeta* out_meta, int timeout_ms);

// Get current stream geometry. Returns 0 on success.
int studio_client_get_format(StudioClient* client, uint32_t* width, uint32_t* height);

// Get dropped frame count since open.
uint64_t studio_client_get_dropped(StudioClient* client);

#ifdef __cplusplus
}
#endif

#endif // STUDIO_CLIENT_H
