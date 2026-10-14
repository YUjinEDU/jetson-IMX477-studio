// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/camera_service.h"
#include "studio/shm_protocol.h"
#include "studio/client.h"

#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <nvbufsurface.h>
#include <opencv2/opencv.hpp>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace Argus;
using namespace EGLStream;
namespace fs = std::filesystem;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void wake_futex(std_atomic_u32* word) {
    word->fetch_add(1, std::memory_order_release);
    syscall(SYS_futex, (uint32_t*)word, FUTEX_WAKE, INT32_MAX, NULL, NULL, 0);
}

struct CameraService::Impl {
    CameraCapabilities caps;
    std::mutex mode_mu;
    int active_sensor_mode{2};
    mutable std::mutex controls_mu;
    CameraControls current_controls;
    CameraControls pending_controls;
    std::atomic<bool> controls_dirty{false};

    mutable std::mutex metrics_mu;
    CameraMetrics metrics;

    // Argus objects
    UniqueObj<CameraProvider> camera_provider;
    UniqueObj<CaptureSession> capture_session;
    UniqueObj<OutputStream> main_stream;
    UniqueObj<OutputStream> ai_stream;
    UniqueObj<FrameConsumer> main_consumer;
    UniqueObj<FrameConsumer> ai_consumer;
    UniqueObj<Request> request;

    std::thread main_thread;
    std::thread ai_thread;

    // Display handshake
    std::mutex display_mu;
    std::condition_variable display_cv;
    bool display_wants_frame{false};
    bool display_has_frame{false};
    int disp_buf_fd{-1};
    NvBufSurface* disp_surface{nullptr};
    uint64_t disp_seq{0};
    uint64_t disp_sensor_ts{0};
    // Native sensor recorder state
    std::mutex rec_mu;
    SensorRecorder recorder;
    int rec_buf_fd{-1};
    NvBufSurface* rec_surface{nullptr};
    std::vector<uint8_t> rec_linear_buf;


    // SHM writer state
    int shm_fd{-1};
    size_t shm_size{0};
    StudioShmHeader* shm_header{nullptr};
    std::string shm_name_str;
    int ai_width{0};
    int ai_height{0};
    int ai_buf_fd{-1};
    NvBufSurface* ai_surface{nullptr};

    uint64_t start_time_ns{0};
    uint64_t prev_num{0};
    uint64_t prev_ts{0};
    uint64_t first_ts{0};
    uint64_t first_host{0};
    std::vector<double> host_arrivals;

    void main_loop(std::atomic<bool>& running);
    void ai_loop(std::atomic<bool>& running);
    void apply_controls_to_request();
};

CameraService::CameraService() : impl_(std::make_unique<Impl>()) {}
CameraService::~CameraService() { stop(); }

CameraCapabilities CameraService::get_capabilities() const {
    return impl_->caps;
}

int CameraService::get_sensor_mode() const {
    return impl_->active_sensor_mode;
}

bool CameraService::set_sensor_mode(int mode_index) {
    if (mode_index < 0 || mode_index > 2) return false;
    std::unique_lock<std::mutex> lock(impl_->mode_mu);
    if (impl_->active_sensor_mode == mode_index && running_.load()) {
        return true;
    }
    int ai_w = impl_->ai_width;
    std::string shm_name = impl_->shm_name_str;
    stop();
    impl_->active_sensor_mode = mode_index;
    return start(ai_w, shm_name, mode_index);
}

CameraControls CameraService::get_controls() const {
    std::lock_guard<std::mutex> lock(impl_->controls_mu);
    return impl_->current_controls;
}

CameraMetrics CameraService::get_metrics() const {
    std::lock_guard<std::mutex> lock(impl_->metrics_mu);
    auto m = impl_->metrics;
    if (impl_->start_time_ns > 0) {
        m.uptime_ms = (now_ns() - impl_->start_time_ns) / 1000000ULL;
    }
    if (!running_.load() || m.last_host_ts_ns == 0 ||
        now_ns() - m.last_host_ts_ns > 1000000000ULL) {
        m.sensor_fps = 0;
        m.host_fps = 0;
    }
    return m;
}

void CameraService::update_controls(const CameraControls& c) {
    std::lock_guard<std::mutex> lock(impl_->controls_mu);
    impl_->pending_controls = c;
    impl_->controls_dirty.store(true);
}

bool CameraService::start(int ai_width, const std::string& shm_name, int sensor_mode) {
    if (running_.load()) return true;

    if (sensor_mode < 0 || sensor_mode > 2) sensor_mode = 2;
    impl_->active_sensor_mode = sensor_mode;
    impl_->shm_name_str = shm_name;
    impl_->ai_width = ai_width;

    // 1. Initialize Argus
    if (!impl_->camera_provider) {
        impl_->camera_provider.reset(CameraProvider::create());
    }
    auto cp = interface_cast<ICameraProvider>(impl_->camera_provider);
    if (!cp) return false;

    std::vector<CameraDevice*> devs;
    cp->getCameraDevices(&devs);
    if (devs.empty()) return false;

    auto props = interface_cast<ICameraProperties>(devs[0]);
    std::vector<SensorMode*> modes;
    props->getBasicSensorModes(&modes);
    if (modes.size() < 3 || (size_t)sensor_mode >= modes.size()) return false;

    auto sm = interface_cast<ISensorMode>(modes[sensor_mode]);
    uint32_t sm_w = sm->getResolution().width();
    uint32_t sm_h = sm->getResolution().height();

    impl_->caps.active_sensor_mode = sensor_mode;
    impl_->caps.width = sm_w;
    impl_->caps.height = sm_h;
    impl_->caps.min_exposure_us = sm->getExposureTimeRange().min() / 1000ULL;
    impl_->caps.min_gain = sm->getAnalogGainRange().min();
    impl_->caps.max_gain = sm->getAnalogGainRange().max();

    uint64_t frame_duration_ns = 8333333ULL;
    if (sensor_mode == 0) {
        impl_->caps.target_fps = 30.0;
        frame_duration_ns = 33333333ULL;
        impl_->caps.max_exposure_us = std::min<uint64_t>(33333ULL, sm->getExposureTimeRange().max() / 1000ULL);
    } else if (sensor_mode == 1) {
        impl_->caps.target_fps = 60.0;
        frame_duration_ns = 16666666ULL;
        impl_->caps.max_exposure_us = std::min<uint64_t>(16666ULL, sm->getExposureTimeRange().max() / 1000ULL);
    } else {
        impl_->caps.target_fps = 120.0;
        frame_duration_ns = 8333333ULL;
        impl_->caps.max_exposure_us = std::min<uint64_t>(8333ULL, sm->getExposureTimeRange().max() / 1000ULL);
    }

    impl_->ai_height = ai_width ? ((int)std::lround((double)sm_h * ai_width / sm_w) & ~1) : 0;

    impl_->capture_session.reset(cp->createCaptureSession(devs[0]));
    auto cs = interface_cast<ICaptureSession>(impl_->capture_session);
    if (!cs) { stop(); return false; }

    // Main stream
    UniqueObj<OutputStreamSettings> ss_main(cs->createOutputStreamSettings(STREAM_TYPE_EGL));
    auto es_main = interface_cast<IEGLOutputStreamSettings>(ss_main);
    es_main->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    es_main->setResolution(Size2D<uint32_t>(sm_w, sm_h));
    es_main->setMode(EGL_STREAM_MODE_FIFO);
    es_main->setFifoLength(16);
    es_main->setMetadataEnable(true);
    impl_->main_stream.reset(cs->createOutputStream(ss_main.get()));
    impl_->main_consumer.reset(FrameConsumer::create(impl_->main_stream.get()));

    // AI stream (if enabled)
    if (impl_->ai_width > 0 && sensor_mode == 2) {
        UniqueObj<OutputStreamSettings> ss_ai(cs->createOutputStreamSettings(STREAM_TYPE_EGL));
        auto es_ai = interface_cast<IEGLOutputStreamSettings>(ss_ai);
        es_ai->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
        es_ai->setResolution(Size2D<uint32_t>(impl_->ai_width, impl_->ai_height));
        es_ai->setMode(EGL_STREAM_MODE_FIFO);
        es_ai->setFifoLength(16);
        es_ai->setMetadataEnable(true);
        impl_->ai_stream.reset(cs->createOutputStream(ss_ai.get()));
        impl_->ai_consumer.reset(FrameConsumer::create(impl_->ai_stream.get()));
    }

    // Request
    impl_->request.reset(cs->createRequest(CAPTURE_INTENT_PREVIEW));
    auto req = interface_cast<IRequest>(impl_->request);
    req->enableOutputStream(impl_->main_stream.get());
    if (impl_->ai_stream) {
        req->enableOutputStream(impl_->ai_stream.get());
    }

    auto src = interface_cast<ISourceSettings>(impl_->request);
    src->setSensorMode(modes[sensor_mode]);
    src->setFrameDurationRange(Range<uint64_t>(frame_duration_ns, frame_duration_ns));

    {
        std::lock_guard<std::mutex> lock(impl_->controls_mu);
        impl_->pending_controls = impl_->current_controls;
    }
    impl_->apply_controls_to_request();

    if (cs->repeat(impl_->request.get()) != STATUS_OK) { stop(); return false; }

    auto egl_main = interface_cast<IEGLOutputStream>(impl_->main_stream);
    if (egl_main->waitUntilConnected(5000000000ULL) != STATUS_OK) { stop(); return false; }

    if (impl_->ai_stream) {
        auto egl_ai = interface_cast<IEGLOutputStream>(impl_->ai_stream);
        if (egl_ai->waitUntilConnected(5000000000ULL) != STATUS_OK) { stop(); return false; }
    }

    // Warmup frames and verify valid frame acquisition
    auto mfc = interface_cast<IFrameConsumer>(impl_->main_consumer);
    auto afc = impl_->ai_consumer ? interface_cast<IFrameConsumer>(impl_->ai_consumer) : nullptr;
    bool warmup_ok = true;
    for (int i = 0; i < 15; ++i) {
        UniqueObj<Frame> f1(mfc->acquireFrame(2000000000ULL));
        if (!f1) { warmup_ok = false; break; }
        if (afc) {
            UniqueObj<Frame> f2(afc->acquireFrame(2000000000ULL));
            if (!f2) { warmup_ok = false; break; }
        }
    }
    if (!warmup_ok) {
        fprintf(stderr, "[ERROR] CameraService warmup failed for mode %d: frame acquisition timed out!\n", sensor_mode);
        stop();
        return false;
    }

    // Reset per-session metrics and timestamps for the new mode
    {
        std::lock_guard<std::mutex> mlock(impl_->metrics_mu);
        impl_->first_ts = 0;
        impl_->first_host = 0;
        impl_->prev_ts = 0;
        impl_->prev_num = 0;
        impl_->metrics.total_frames = 0;
        impl_->metrics.bad_sensor_intervals = 0;
        impl_->metrics.sequence_gaps = 0;
        impl_->metrics.sensor_fps = 0.0;
        impl_->metrics.host_fps = 0.0;
    }
    // 2. Initialize Shared Memory (if AI stream enabled)
    if (impl_->ai_width > 0) {
        shm_unlink(impl_->shm_name_str.c_str());
        uint32_t payload = impl_->ai_width * impl_->ai_height * 3 / 2;
        uint32_t stride = (sizeof(StudioSlotHeader) + payload + 7) & ~7;
        impl_->shm_size = sizeof(StudioShmHeader) + (size_t)STUDIO_SHM_MAX_SLOTS * stride;

        impl_->shm_fd = shm_open(impl_->shm_name_str.c_str(), O_CREAT | O_RDWR, 0666);
        if (impl_->shm_fd >= 0 && ftruncate(impl_->shm_fd, impl_->shm_size) == 0) {
            void* p = mmap(NULL, impl_->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, impl_->shm_fd, 0);
            if (p != MAP_FAILED) {
                impl_->shm_header = (StudioShmHeader*)p;
                new (impl_->shm_header) StudioShmHeader();
                memcpy(impl_->shm_header->magic, STUDIO_SHM_MAGIC, 8);
                impl_->shm_header->version = STUDIO_SHM_VERSION;
                impl_->shm_header->num_slots = STUDIO_SHM_MAX_SLOTS;
                impl_->shm_header->width = impl_->ai_width;
                impl_->shm_header->height = impl_->ai_height;
                impl_->shm_header->slot_stride_bytes = stride;
                impl_->shm_header->writer_pid = getpid();
                impl_->shm_header->session_id = now_ns();
            }
        }
    }

    running_.store(true);
    impl_->start_time_ns = now_ns();
    impl_->main_thread = std::thread(&Impl::main_loop, impl_.get(), std::ref(running_));
    if (impl_->ai_consumer) {
        impl_->ai_thread = std::thread(&Impl::ai_loop, impl_.get(), std::ref(running_));
    }
    return true;
}

void CameraService::stop() {
    // Startup can fail after creating an Argus session but before running_ is set.
    // Those partial sessions must be torn down too.
    bool was_running = running_.exchange(false);
    if (!was_running && !impl_->capture_session && !impl_->camera_provider) return;
    {
        std::lock_guard<std::mutex> dlock(impl_->display_mu);
        impl_->display_wants_frame = false;
        impl_->display_has_frame = false;
        impl_->display_cv.notify_all();
    }

    if (impl_->capture_session) {
        auto cs = interface_cast<ICaptureSession>(impl_->capture_session);
        if (cs) {
            cs->stopRepeat();
            cs->waitForIdle(2000000000ULL);
        }
    }

    if (impl_->main_thread.joinable()) impl_->main_thread.join();
    if (impl_->ai_thread.joinable()) impl_->ai_thread.join();

    {
        std::lock_guard<std::mutex> dlock(impl_->display_mu);
        if (impl_->disp_surface) {
            NvBufSurfaceUnMap(impl_->disp_surface, 0, -1);
            NvBufSurfaceDestroy(impl_->disp_surface);
            impl_->disp_surface = nullptr;
        }
        impl_->disp_buf_fd = -1;
    }
    {
        std::lock_guard<std::mutex> rlock(impl_->rec_mu);
        impl_->recorder.stop();
        if (impl_->rec_surface) {
            NvBufSurfaceUnMap(impl_->rec_surface, 0, -1);
            NvBufSurfaceDestroy(impl_->rec_surface);
            impl_->rec_surface = nullptr;
        }
        impl_->rec_buf_fd = -1;
        impl_->rec_linear_buf.clear();
    }

    if (impl_->ai_surface) {
        NvBufSurfaceUnMap(impl_->ai_surface, 0, -1);
        NvBufSurfaceDestroy(impl_->ai_surface);
        impl_->ai_surface = nullptr;
    }
    impl_->ai_buf_fd = -1;

    if (impl_->shm_header) {
        impl_->shm_header->writer_pid = 0;
        wake_futex(&impl_->shm_header->futex_word);
        munmap(impl_->shm_header, impl_->shm_size);
        impl_->shm_header = nullptr;
    }
    if (impl_->shm_fd >= 0) {
        close(impl_->shm_fd);
        shm_unlink(impl_->shm_name_str.c_str());
        impl_->shm_fd = -1;
    }
    impl_->request.reset();
    impl_->main_consumer.reset();
    impl_->ai_consumer.reset();
    impl_->main_stream.reset();
    impl_->ai_stream.reset();
    impl_->capture_session.reset();
    // Release the provider as well: retaining it keeps the old sensor/ISP context
    // alive across mode changes, including a failed startup's error state.
    impl_->camera_provider.reset();
    {
        std::lock_guard<std::mutex> lock(impl_->metrics_mu);
        impl_->metrics = CameraMetrics{};
        impl_->start_time_ns = 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void CameraService::Impl::apply_controls_to_request() {
    auto req = interface_cast<IRequest>(request);
    auto src = interface_cast<ISourceSettings>(request);
    auto ac = interface_cast<IAutoControlSettings>(req->getAutoControlSettings());
    if (!src || !ac) return;

    CameraControls c;
    {
        std::lock_guard<std::mutex> lock(controls_mu);
        c = pending_controls;
        current_controls = c;
        controls_dirty.store(false);
    }

    // 1. Exposure and gain
    if (c.auto_exposure) {
        uint64_t max_exp = std::clamp<uint64_t>(c.ae_max_exposure_us * 1000ULL, caps.min_exposure_us * 1000ULL, caps.max_exposure_us * 1000ULL);
        src->setExposureTimeRange(Range<uint64_t>(caps.min_exposure_us * 1000ULL, max_exp));
        float max_g = std::clamp<float>(c.ae_max_gain, caps.min_gain, caps.max_gain);
        src->setGainRange(Range<float>(caps.min_gain, max_g));
        ac->setAeLock(c.ae_lock);
    } else {
        uint64_t exp_ns = std::clamp<uint64_t>(c.exposure_us * 1000ULL, caps.min_exposure_us * 1000ULL, caps.max_exposure_us * 1000ULL);
        float g = std::clamp<float>(c.gain, caps.min_gain, caps.max_gain);
        src->setExposureTimeRange(Range<uint64_t>(exp_ns, exp_ns));
        src->setGainRange(Range<float>(g, g));
        ac->setAeLock(false);
    }
    ac->setExposureCompensation(std::clamp(c.ev_compensation, -2.0f, 2.0f));

    // Antibanding
    if (c.antibanding == "50hz") ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_50HZ);
    else if (c.antibanding == "60hz") ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_60HZ);
    else if (c.antibanding == "off") ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
    else ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_AUTO);

    // AWB
    ac->setAwbLock(c.awb_lock);
    if (c.awb_mode == "incandescent") ac->setAwbMode(AWB_MODE_INCANDESCENT);
    else if (c.awb_mode == "fluorescent") ac->setAwbMode(AWB_MODE_FLUORESCENT);
    else if (c.awb_mode == "daylight") ac->setAwbMode(AWB_MODE_DAYLIGHT);
    else if (c.awb_mode == "cloudy") ac->setAwbMode(AWB_MODE_CLOUDY_DAYLIGHT);
    else if (c.awb_mode == "shade") ac->setAwbMode(AWB_MODE_SHADE);
    else if (c.awb_mode == "off") {
        ac->setAwbMode(AWB_MODE_OFF);
        BayerTuple<float> gains(c.awb_gains[0], c.awb_gains[1], c.awb_gains[2], c.awb_gains[3]);
        ac->setWbGains(gains);
    } else {
        ac->setAwbMode(AWB_MODE_AUTO);
    }

    // ISP Digital Gain
    float dg = std::clamp(c.isp_digital_gain, 1.0f, 8.0f);
    ac->setIspDigitalGainRange(Range<float>(dg, dg));

    // Denoise
    auto idn = interface_cast<IDenoiseSettings>(request);
    if (idn) {
        if (c.denoise_mode == "off") idn->setDenoiseMode(DENOISE_MODE_OFF);
        else if (c.denoise_mode == "high_quality") idn->setDenoiseMode(DENOISE_MODE_HIGH_QUALITY);
        else idn->setDenoiseMode(DENOISE_MODE_FAST);
        idn->setDenoiseStrength(std::clamp(c.denoise_strength, 0.0f, 1.0f));
    }

    // Edge Enhancement
    auto iee = interface_cast<IEdgeEnhanceSettings>(request);
    if (iee) {
        if (c.edge_mode == "off") iee->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_OFF);
        else if (c.edge_mode == "high_quality") iee->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_HIGH_QUALITY);
        else iee->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_FAST);
        iee->setEdgeEnhanceStrength(std::clamp(c.edge_strength, 0.0f, 1.0f));
    }
}

void CameraService::Impl::main_loop(std::atomic<bool>& running) {
    auto fc = interface_cast<IFrameConsumer>(main_consumer);
    auto cs = interface_cast<ICaptureSession>(capture_session);

    while (running.load()) {
        UniqueObj<Frame> frame(fc->acquireFrame(2000000000ULL));
        uint64_t host_now = now_ns();
        if (!frame) {
            if (!running.load()) break;
            continue;
        }

        auto f = interface_cast<IFrame>(frame);
        auto md = interface_cast<IArgusCaptureMetadata>(frame);
        auto m = md ? interface_cast<ICaptureMetadata>(md->getMetadata()) : nullptr;
        uint64_t num = f ? f->getNumber() : 0;
        uint64_t sensor_ts = m ? m->getSensorTimestamp() : 0;

        // Apply pending runtime controls
        if (controls_dirty.load()) {
            apply_controls_to_request();
            cs->repeat(request.get());
        }

        // Metrics update
        {
            std::lock_guard<std::mutex> lock(metrics_mu);
            metrics.total_frames++;
            metrics.last_sequence = num;
            metrics.last_sensor_ts_ns = sensor_ts;
            metrics.last_host_ts_ns = host_now;

            if (m) {
                metrics.actual_exposure_us = m->getSensorExposureTime() / 1000ULL;
                metrics.actual_analog_gain = m->getSensorAnalogGain();
                metrics.actual_isp_gain = m->getIspDigitalGain();
                metrics.actual_cct = m->getAwbCct();
                BayerTuple<float> wb = m->getAwbGains();
                metrics.actual_awb_gains[0] = wb.r();
                metrics.actual_awb_gains[1] = wb.gEven();
                metrics.actual_awb_gains[2] = wb.gOdd();
                metrics.actual_awb_gains[3] = wb.b();
            }

            if (metrics.total_frames == 1) {
                first_ts = sensor_ts;
                first_host = host_now;
            } else {
                if (num != prev_num + 1) {
                    metrics.sequence_gaps += (num > prev_num ? (num - prev_num - 1) : 1);
                }
                double iv = (sensor_ts - prev_ts) / 1e6;
                if (metrics.total_frames == 2) {
                    metrics.min_interval_ms = iv;
                    metrics.max_interval_ms = iv;
                } else {
                    metrics.min_interval_ms = std::min(metrics.min_interval_ms, iv);
                    metrics.max_interval_ms = std::max(metrics.max_interval_ms, iv);
                }
                double expected_iv = 1000.0 / std::max(1.0, caps.target_fps);
                if (iv > expected_iv * 1.5 || iv < expected_iv * 0.5) {
                    metrics.bad_sensor_intervals++;
                }
                if (sensor_ts > first_ts) {
                    metrics.sensor_fps = (metrics.total_frames - 1) * 1e9 / (sensor_ts - first_ts);
                }
                if (host_now > first_host) {
                    metrics.host_fps = (metrics.total_frames - 1) * 1e9 / (host_now - first_host);
                }
            }
            prev_num = num;
            prev_ts = sensor_ts;
        }

        // 1. Full-rate native sensor recording
        bool rec_active = false;
        {
            std::lock_guard<std::mutex> rlock(rec_mu);
            rec_active = recorder.is_recording();
        }
        if (rec_active) {
            auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
            if (native) {
                if (rec_buf_fd < 0) {
                    rec_buf_fd = native->createNvBuffer(Size2D<uint32_t>(caps.width, caps.height),
                                                        NVBUF_COLOR_FORMAT_NV12,
                                                        NVBUF_LAYOUT_PITCH);
                    NvBufSurfaceFromFd(rec_buf_fd, (void**)&rec_surface);
                    NvBufSurfaceMap(rec_surface, 0, -1, NVBUF_MAP_READ);
                }
                native->copyToNvBuffer(rec_buf_fd);
                NvBufSurfaceSyncForCpu(rec_surface, 0, -1);
                const auto& p = rec_surface->surfaceList[0];

                size_t total_sz = (size_t)caps.width * caps.height * 3 / 2;
                if (rec_linear_buf.size() != total_sz) {
                    rec_linear_buf.resize(total_sz);
                }
                const uint8_t* y_src = static_cast<const uint8_t*>(p.mappedAddr.addr[0]);
                uint32_t y_pitch = p.planeParams.pitch[0];
                for (uint32_t r = 0; r < caps.height; ++r) {
                    memcpy(rec_linear_buf.data() + (size_t)r * caps.width, y_src + (size_t)r * y_pitch, caps.width);
                }
                const uint8_t* uv_src = static_cast<const uint8_t*>(p.mappedAddr.addr[1]);
                uint32_t uv_pitch = p.planeParams.pitch[1];
                size_t uv_off = (size_t)caps.width * caps.height;
                for (uint32_t r = 0; r < caps.height / 2; ++r) {
                    memcpy(rec_linear_buf.data() + uv_off + (size_t)r * caps.width, uv_src + (size_t)r * uv_pitch, caps.width);
                }

                FrameMetadata fmeta;
                fmeta.sequence = num;
                fmeta.sensor_timestamp_ns = sensor_ts;
                fmeta.exposure_us = metrics.actual_exposure_us;
                fmeta.analog_gain = metrics.actual_analog_gain;
                fmeta.isp_digital_gain = metrics.actual_isp_gain;
                fmeta.awb_cct = metrics.actual_cct;

                std::lock_guard<std::mutex> rlock(rec_mu);
                recorder.push_frame(rec_linear_buf.data(), total_sz, fmeta);
            }
        }

        // 2. Display copy handshake
        {
            std::lock_guard<std::mutex> lock(display_mu);
            if (display_wants_frame && !display_has_frame) {
                if (rec_active && rec_surface) {
                    if (disp_buf_fd < 0) {
                        auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
                        disp_buf_fd = native->createNvBuffer(Size2D<uint32_t>(caps.width, caps.height),
                                                             NVBUF_COLOR_FORMAT_NV12,
                                                             NVBUF_LAYOUT_PITCH);
                        NvBufSurfaceFromFd(disp_buf_fd, (void**)&disp_surface);
                        NvBufSurfaceMap(disp_surface, 0, -1, NVBUF_MAP_READ);
                    }
                    NvBufSurfaceCopy(rec_surface, disp_surface);
                    disp_seq = num;
                    disp_sensor_ts = sensor_ts;
                    display_has_frame = true;
                    display_wants_frame = false;
                    display_cv.notify_one();
                } else {
                    auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
                    if (native) {
                        if (disp_buf_fd < 0) {
                            disp_buf_fd = native->createNvBuffer(Size2D<uint32_t>(caps.width, caps.height),
                                                                 NVBUF_COLOR_FORMAT_NV12,
                                                                 NVBUF_LAYOUT_PITCH);
                            NvBufSurfaceFromFd(disp_buf_fd, (void**)&disp_surface);
                            NvBufSurfaceMap(disp_surface, 0, -1, NVBUF_MAP_READ);
                        }
                        native->copyToNvBuffer(disp_buf_fd);
                        disp_seq = num;
                        disp_sensor_ts = sensor_ts;
                        display_has_frame = true;
                        display_wants_frame = false;
                        display_cv.notify_one();
                    }
                }
            }
        }

    }
}

void CameraService::Impl::ai_loop(std::atomic<bool>& running) {
    auto fc = interface_cast<IFrameConsumer>(ai_consumer);
    if (!fc || !shm_header) return;

    while (running.load()) {
        UniqueObj<Frame> frame(fc->acquireFrame(2000000000ULL));
        uint64_t host_now = now_ns();
        if (!frame) {
            if (!running.load()) break;
            continue;
        }

        uint64_t hb = shm_header->reader_heartbeat_ns.load(std::memory_order_relaxed);
        // Skip format copy if no active readers exist (heartbeat is 0 or expired > 2s)
        if (hb == 0 || (host_now > hb && (host_now - hb) > 2000000000ULL)) {
            continue;
        }

        auto f = interface_cast<IFrame>(frame);
        auto md = interface_cast<IArgusCaptureMetadata>(frame);
        auto m = md ? interface_cast<ICaptureMetadata>(md->getMetadata()) : nullptr;
        uint64_t num = f ? f->getNumber() : 0;
        uint64_t sensor_ts = m ? m->getSensorTimestamp() : 0;

        auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
        if (!native) continue;

        if (ai_buf_fd < 0) {
            ai_buf_fd = native->createNvBuffer(Size2D<uint32_t>(ai_width, ai_height),
                                               NVBUF_COLOR_FORMAT_NV12, NVBUF_LAYOUT_PITCH);
            NvBufSurfaceFromFd(ai_buf_fd, (void**)&ai_surface);
            NvBufSurfaceMap(ai_surface, 0, -1, NVBUF_MAP_READ);
        }
        native->copyToNvBuffer(ai_buf_fd);
        NvBufSurfaceSyncForCpu(ai_surface, 0, -1);

        uint32_t slot_idx = (uint32_t)(num % shm_header->num_slots);
        uint8_t* slot_base = (uint8_t*)shm_header + sizeof(StudioShmHeader) + slot_idx * shm_header->slot_stride_bytes;
        StudioSlotHeader* slot = (StudioSlotHeader*)slot_base;
        uint8_t* payload = slot_base + sizeof(StudioSlotHeader);

        slot->seqlock.fetch_add(1, std::memory_order_release); // odd
        std::atomic_thread_fence(std::memory_order_release);
        slot->sequence = num;
        slot->sensor_timestamp_ns = sensor_ts;
        slot->host_timestamp_ns = host_now;
        if (m) {
            slot->exposure_ns = m->getSensorExposureTime();
            slot->analog_gain = m->getSensorAnalogGain();
            slot->isp_digital_gain = m->getIspDigitalGain();
            slot->awb_cct = m->getAwbCct();
        }
        slot->flags = 0;
        slot->width = ai_width;
        slot->height = ai_height;
        slot->payload_bytes = ai_width * ai_height * 3 / 2;

        // Copy NV12 planes with pitch
        const auto& params = ai_surface->surfaceList[0];
        size_t off = 0;
        for (int pl = 0; pl < 2; ++pl) {
            int h = pl ? ai_height / 2 : ai_height;
            auto d = (const uint8_t*)params.mappedAddr.addr[pl];
            uint32_t pitch = params.planeParams.pitch[pl];
            for (int r = 0; r < h; ++r) {
                memcpy(payload + off + (size_t)r * ai_width, d + (size_t)r * pitch, ai_width);
            }
            off += (size_t)ai_width * h;
        }

        std::atomic_thread_fence(std::memory_order_release);
        slot->seqlock.fetch_add(1, std::memory_order_release); // even
        shm_header->latest_seq.store(num, std::memory_order_release);
        wake_futex(&shm_header->futex_word);
    }
}

bool CameraService::acquire_display_frame(NvBufSurface** out_surface, uint64_t* out_seq,
                                          uint64_t* out_sensor_ts, int timeout_ms) {
    if (!running_.load()) return false;
    std::unique_lock<std::mutex> lock(impl_->display_mu);
    if (!impl_->display_has_frame) impl_->display_wants_frame = true;

    if (!impl_->display_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [&] { return impl_->display_has_frame || !running_.load(); })) {
        impl_->display_wants_frame = false;
        return false;
    }
    if (!running_.load() || !impl_->display_has_frame) return false;

    NvBufSurfaceSyncForCpu(impl_->disp_surface, 0, -1);
    impl_->display_has_frame = false;
    *out_surface = impl_->disp_surface;
    if (out_seq) *out_seq = impl_->disp_seq;
    if (out_sensor_ts) *out_sensor_ts = impl_->disp_sensor_ts;
    return true;
}

void CameraService::release_display_frame() {
    std::lock_guard<std::mutex> lock(impl_->display_mu);
    if (running_.load()) impl_->display_wants_frame = true;
}
bool CameraService::start_recording(const std::string& directory) {
    if (!running_.load()) return false;
    std::lock_guard<std::mutex> lock(impl_->rec_mu);
    if (impl_->recorder.is_recording()) return false;
    auto caps = get_capabilities();
    auto t = std::time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    std::ostringstream ss;
    ss << directory << "/rec_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".avi";
    std::string path = ss.str();
    return impl_->recorder.start(path, caps.width, caps.height, caps.target_fps);
}

bool CameraService::stop_recording() {
    std::lock_guard<std::mutex> lock(impl_->rec_mu);
    return impl_->recorder.stop();
}

OriginalRecordingStatus CameraService::get_recording_status() const {
    std::lock_guard<std::mutex> lock(impl_->rec_mu);
    return impl_->recorder.get_status();
}

AiStatus CameraService::get_ai_status() const {
    AiStatus st;
    st.enabled = (impl_->ai_consumer.get() != nullptr && impl_->shm_header != nullptr);
    st.width = impl_->ai_width;
    st.height = impl_->ai_height;
    st.shm_name = impl_->shm_name_str;
    if (st.enabled && impl_->shm_header) {
        st.last_sequence = impl_->shm_header->latest_seq.load(std::memory_order_relaxed);
        uint64_t hb = impl_->shm_header->reader_heartbeat_ns.load(std::memory_order_relaxed);
        uint64_t now = now_ns();
        st.reader_active = (hb > 0 && now >= hb && (now - hb) <= 2000000000ULL);
    }
    return st;
}

