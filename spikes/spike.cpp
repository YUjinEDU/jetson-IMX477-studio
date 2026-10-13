// Throwaway M0 spike: single mode-2 stream, per-frame VIC downscale, periodic
// display-size copy, live exposure/gain changes via repeat().
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <nvbufsurface.h>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
using namespace Argus;
using namespace EGLStream;
static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Buf { int fd=-1; NvBufSurface* s=nullptr; };
int main(int argc, char** argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 7261;
    int ai_w = argc > 2 ? atoi(argv[2]) : 320;
    int change_every = argc > 3 ? atoi(argv[3]) : 240;  // frames between control changes
    int disp_every = argc > 4 ? atoi(argv[4]) : 4;
    int ai_h = (int)lround(990.0 * ai_w / 1332) & ~1;
    UniqueObj<CameraProvider> provider(CameraProvider::create());
    auto cp = interface_cast<ICameraProvider>(provider);
    std::vector<CameraDevice*> devs; cp->getCameraDevices(&devs);
    if (devs.empty()) return 1;
    auto props = interface_cast<ICameraProperties>(devs[0]);
    std::vector<SensorMode*> modes; props->getBasicSensorModes(&modes);
    auto im = interface_cast<ISensorMode>(modes[2]);
    printf("mode2 %ux%u exp %lu..%lu gain %.2f..%.2f dur %lu..%lu\n", im->getResolution().width(), im->getResolution().height(),
           im->getExposureTimeRange().min(), im->getExposureTimeRange().max(), im->getAnalogGainRange().min(),
           im->getAnalogGainRange().max(), im->getFrameDurationRange().min(), im->getFrameDurationRange().max());
    UniqueObj<CaptureSession> session(cp->createCaptureSession(devs[0]));
    auto cs = interface_cast<ICaptureSession>(session);
    UniqueObj<OutputStreamSettings> ss(cs->createOutputStreamSettings(STREAM_TYPE_EGL));
    auto es = interface_cast<IEGLOutputStreamSettings>(ss);
    es->setPixelFormat(PIXEL_FMT_YCbCr_420_888); es->setResolution(Size2D<uint32_t>(1332, 990));
    es->setMode(EGL_STREAM_MODE_FIFO); es->setFifoLength(16); es->setMetadataEnable(true);
    UniqueObj<OutputStream> stream(cs->createOutputStream(ss.get()));
    auto egl = interface_cast<IEGLOutputStream>(stream);
    UniqueObj<FrameConsumer> consumer(FrameConsumer::create(stream.get()));
    auto fc = interface_cast<IFrameConsumer>(consumer);
    UniqueObj<Request> request(cs->createRequest(CAPTURE_INTENT_PREVIEW));
    auto req = interface_cast<IRequest>(request);
    auto src = interface_cast<ISourceSettings>(request);
    auto ac = interface_cast<IAutoControlSettings>(req->getAutoControlSettings());
    req->enableOutputStream(stream.get());
    ac->setAwbMode(AWB_MODE_AUTO);
    src->setSensorMode(modes[2]);
    src->setFrameDurationRange(Range<uint64_t>(8333333, 8333333));
    uint64_t exp_ns = 4000000; float gain = 2;
    src->setExposureTimeRange(Range<uint64_t>(exp_ns, exp_ns));
    src->setGainRange(Range<float>(gain, gain));
    if (cs->repeat(request.get()) != STATUS_OK || egl->waitUntilConnected(5000000000ULL) != STATUS_OK) return 1;
    for (int i = 0; i < 60; ++i) { UniqueObj<Frame> f(fc->acquireFrame(2000000000ULL)); if (!f) return 1; }
    Buf ai, disp;
    std::vector<uint8_t> shm((size_t)ai_w * ai_h * 3 / 2);
    uint64_t prev_num = 0, prev_ts = 0, first_ts = 0, last_ts = 0, first_host = 0, last_host = 0;
    int gaps = 0, bad = 0, toggles = 0; double max_iv = 0, min_iv = 1e9, max_repeat_ms = 0, sum_copy = 0, max_copy = 0;
    uint64_t pending_exp = 0; int pending_since = -1; int max_apply_frames = 0;
    for (int i = 0; i < frames; ++i) {
        UniqueObj<Frame> frame(fc->acquireFrame(2000000000ULL));
        uint64_t host = now_ns();
        auto f = interface_cast<IFrame>(frame);
        if (!f) { printf("timeout at %d\n", i); break; }
        auto md = interface_cast<IArgusCaptureMetadata>(frame);
        auto m = md ? interface_cast<ICaptureMetadata>(md->getMetadata()) : nullptr;
        uint64_t num = f->getNumber(), ts = m ? m->getSensorTimestamp() : 0, sexp = m ? m->getSensorExposureTime() : 0;
        if (i) {
            if (num != prev_num + 1) gaps += (int)(num - prev_num - 1);
            double iv = (ts - prev_ts) / 1e6; max_iv = std::max(max_iv, iv); min_iv = std::min(min_iv, iv);
            if (iv > 1.5 * 8.3333 || iv < 0.5 * 8.3333) ++bad;
        } else { first_ts = ts; first_host = host; }
        prev_num = num; prev_ts = ts; last_ts = ts; last_host = host;
        if (pending_since >= 0 && llabs((long long)sexp - (long long)pending_exp) < 50000) {
            max_apply_frames = std::max(max_apply_frames, i - pending_since); pending_since = -1;
        }
        auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
        uint64_t c0 = now_ns();
        if (ai_w) {
            if (ai.fd < 0) {
                ai.fd = native->createNvBuffer(Size2D<uint32_t>(ai_w, ai_h), NVBUF_COLOR_FORMAT_NV12, NVBUF_LAYOUT_PITCH);
                NvBufSurfaceFromFd(ai.fd, (void**)&ai.s); NvBufSurfaceMap(ai.s, 0, -1, NVBUF_MAP_READ);
            } else if (native->copyToNvBuffer(ai.fd) != STATUS_OK) { printf("ai copy fail\n"); break; }
            NvBufSurfaceSyncForCpu(ai.s, 0, -1);
            auto& p = ai.s->surfaceList[0]; size_t off = 0;
            for (int pl = 0; pl < 2; ++pl) {
                int h = pl ? ai_h / 2 : ai_h;
                auto d = (const uint8_t*)p.mappedAddr.addr[pl];
                for (int r = 0; r < h; ++r) memcpy(shm.data() + off + (size_t)r * ai_w, d + (size_t)r * p.planeParams.pitch[pl], ai_w);
                off += (size_t)ai_w * h;
            }
        }
        if (disp_every && i % disp_every == 0) {  // periodic full-res display copy
            if (disp.fd < 0) {
                disp.fd = native->createNvBuffer(Size2D<uint32_t>(1332, 990), NVBUF_COLOR_FORMAT_NV12, NVBUF_LAYOUT_PITCH);
                NvBufSurfaceFromFd(disp.fd, (void**)&disp.s); NvBufSurfaceMap(disp.s, 0, -1, NVBUF_MAP_READ);
            } else if (native->copyToNvBuffer(disp.fd) != STATUS_OK) { printf("disp copy fail\n"); break; }
        }
        double cms = (now_ns() - c0) / 1e6; sum_copy += cms; max_copy = std::max(max_copy, cms);
        frame.reset();
        if (change_every && i % change_every == change_every - 1) {
            ++toggles;
            exp_ns = (toggles & 1) ? 2000000 : 4000000; gain = (toggles & 1) ? 4.0f : 2.0f;
            uint64_t r0 = now_ns();
            src->setExposureTimeRange(Range<uint64_t>(exp_ns, exp_ns));
            src->setGainRange(Range<float>(gain, gain));
            if (cs->repeat(request.get()) != STATUS_OK) { printf("repeat fail\n"); break; }
            max_repeat_ms = std::max(max_repeat_ms, (now_ns() - r0) / 1e6);
            pending_exp = exp_ns; pending_since = i;
        }
    }
    cs->stopRepeat(); cs->waitForIdle(5000000000ULL);
    printf("frames=%d sensor_fps=%.3f host_fps=%.3f gaps=%d bad_intervals=%d iv=%.2f..%.2fms toggles=%d max_repeat=%.2fms "
           "copy mean=%.3f max=%.3fms max_apply_frames=%d\n", frames, (frames - 1) * 1e9 / (last_ts - first_ts),
           (frames - 1) * 1e9 / (last_host - first_host), gaps, bad, min_iv, max_iv, toggles, max_repeat_ms,
           sum_copy / frames, max_copy, max_apply_frames);
    return 0;
}
