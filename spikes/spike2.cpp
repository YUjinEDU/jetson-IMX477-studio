// Throwaway M0 spike 2: two ISP output streams (full + AI size) from one request,
// one consumer thread each; periodic full-res display copy; live control changes.
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <nvbufsurface.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
using namespace Argus;
using namespace EGLStream;
static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Result { int frames = 0, gaps = 0, bad = 0; double fps = 0, min_iv = 1e9, max_iv = 0, copy_mean = 0, copy_max = 0; };
static void consume(IFrameConsumer* fc, int frames, int w, int h, int copy_every, bool to_cpu, Result* out) {
    int fd = -1; NvBufSurface* s = nullptr; std::vector<uint8_t> dst((size_t)w * h * 3 / 2);
    uint64_t prev_num = 0, prev_ts = 0, first_ts = 0; double sum = 0;
    for (int i = 0; i < frames; ++i) {
        UniqueObj<Frame> frame(fc->acquireFrame(2000000000ULL));
        auto f = interface_cast<IFrame>(frame);
        if (!f) { printf("timeout %dx%d at %d\n", w, h, i); break; }
        auto md = interface_cast<IArgusCaptureMetadata>(frame);
        auto m = md ? interface_cast<ICaptureMetadata>(md->getMetadata()) : nullptr;
        uint64_t num = f->getNumber(), ts = m ? m->getSensorTimestamp() : 0;
        if (i) {
            if (num != prev_num + 1) out->gaps += (int)(num - prev_num - 1);
            double iv = (ts - prev_ts) / 1e6; out->min_iv = std::min(out->min_iv, iv); out->max_iv = std::max(out->max_iv, iv);
            if (iv > 12.5 || iv < 4.17) ++out->bad;
        } else first_ts = ts;
        prev_num = num; prev_ts = ts; out->frames = i + 1;
        if (copy_every && i % copy_every == 0) {
            uint64_t c0 = now_ns();
            auto native = interface_cast<NV::IImageNativeBuffer>(f->getImage());
            if (fd < 0) {
                fd = native->createNvBuffer(Size2D<uint32_t>(w, h), NVBUF_COLOR_FORMAT_NV12, NVBUF_LAYOUT_PITCH);
                NvBufSurfaceFromFd(fd, (void**)&s); NvBufSurfaceMap(s, 0, -1, NVBUF_MAP_READ);
            } else if (native->copyToNvBuffer(fd) != STATUS_OK) { printf("copy fail\n"); break; }
            if (to_cpu) {
                NvBufSurfaceSyncForCpu(s, 0, -1);
                auto& p = s->surfaceList[0]; size_t off = 0;
                for (int pl = 0; pl < 2; ++pl) {
                    int ph = pl ? h / 2 : h; auto d = (const uint8_t*)p.mappedAddr.addr[pl];
                    for (int r = 0; r < ph; ++r) memcpy(dst.data() + off + (size_t)r * w, d + (size_t)r * p.planeParams.pitch[pl], w);
                    off += (size_t)w * ph;
                }
            }
            double ms = (now_ns() - c0) / 1e6; sum += ms; out->copy_max = std::max(out->copy_max, ms);
        }
    }
    out->fps = out->frames > 1 ? (out->frames - 1) * 1e9 / (prev_ts - first_ts) : 0;
    out->copy_mean = copy_every ? sum / std::max(1, out->frames / copy_every) : 0;
}
int main(int argc, char** argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 7261;
    int ai_w = argc > 2 ? atoi(argv[2]) : 320;
    int change_every_ms = argc > 3 ? atoi(argv[3]) : 2000;
    int disp_every = argc > 4 ? atoi(argv[4]) : 4;
    int ai_h = (int)lround(990.0 * ai_w / 1332) & ~1;
    UniqueObj<CameraProvider> provider(CameraProvider::create());
    auto cp = interface_cast<ICameraProvider>(provider);
    std::vector<CameraDevice*> devs; cp->getCameraDevices(&devs);
    auto props = interface_cast<ICameraProperties>(devs[0]);
    std::vector<SensorMode*> modes; props->getBasicSensorModes(&modes);
    UniqueObj<CaptureSession> session(cp->createCaptureSession(devs[0]));
    auto cs = interface_cast<ICaptureSession>(session);
    auto make = [&](int w, int h) {
        UniqueObj<OutputStreamSettings> ss(cs->createOutputStreamSettings(STREAM_TYPE_EGL));
        auto es = interface_cast<IEGLOutputStreamSettings>(ss);
        es->setPixelFormat(PIXEL_FMT_YCbCr_420_888); es->setResolution(Size2D<uint32_t>(w, h));
        es->setMode(EGL_STREAM_MODE_FIFO); es->setFifoLength(16); es->setMetadataEnable(true);
        return cs->createOutputStream(ss.get());
    };
    UniqueObj<OutputStream> main_stream(make(1332, 990));
    UniqueObj<OutputStream> ai_stream(ai_w ? make(ai_w, ai_h) : nullptr);
    UniqueObj<FrameConsumer> main_c(FrameConsumer::create(main_stream.get()));
    UniqueObj<FrameConsumer> ai_c(ai_w ? FrameConsumer::create(ai_stream.get()) : nullptr);
    UniqueObj<Request> request(cs->createRequest(CAPTURE_INTENT_PREVIEW));
    auto req = interface_cast<IRequest>(request);
    auto src = interface_cast<ISourceSettings>(request);
    auto ac = interface_cast<IAutoControlSettings>(req->getAutoControlSettings());
    req->enableOutputStream(main_stream.get());
    if (ai_w) req->enableOutputStream(ai_stream.get());
    ac->setAwbMode(AWB_MODE_AUTO);
    src->setSensorMode(modes[2]);
    src->setFrameDurationRange(Range<uint64_t>(8333333, 8333333));
    src->setExposureTimeRange(Range<uint64_t>(4000000, 4000000));
    src->setGainRange(Range<float>(2, 2));
    if (cs->repeat(request.get()) != STATUS_OK) return 1;
    if (interface_cast<IEGLOutputStream>(main_stream)->waitUntilConnected(5000000000ULL) != STATUS_OK) return 1;
    if (ai_w && interface_cast<IEGLOutputStream>(ai_stream)->waitUntilConnected(5000000000ULL) != STATUS_OK) return 1;
    auto mfc = interface_cast<IFrameConsumer>(main_c);
    auto afc = ai_w ? interface_cast<IFrameConsumer>(ai_c) : nullptr;
    for (int i = 0; i < 60; ++i) {
        UniqueObj<Frame> a(mfc->acquireFrame(2000000000ULL));
        if (afc) { UniqueObj<Frame> b(afc->acquireFrame(2000000000ULL)); }
    }
    Result rm, ra; std::atomic<bool> done{false};
    std::thread tm([&] { consume(mfc, frames, 1332, 990, disp_every, true, &rm); });
    std::thread ta;
    if (afc) ta = std::thread([&] { consume(afc, frames, ai_w, ai_h, 1, true, &ra); });
    std::thread ctl([&] {
        int t = 0; double max_rep = 0;
        while (!done && change_every_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(change_every_ms));
            if (done) break;
            ++t; uint64_t e = (t & 1) ? 2000000 : 4000000; float g = (t & 1) ? 4.f : 2.f;
            uint64_t r0 = now_ns();
            src->setExposureTimeRange(Range<uint64_t>(e, e)); src->setGainRange(Range<float>(g, g));
            cs->repeat(request.get());
            max_rep = std::max(max_rep, (now_ns() - r0) / 1e6);
        }
        printf("control changes=%d max_repeat=%.2fms\n", t, max_rep);
    });
    tm.join(); if (ta.joinable()) ta.join(); done = true; ctl.join();
    cs->stopRepeat(); cs->waitForIdle(5000000000ULL);
    auto pr = [](const char* n, Result& r) {
        printf("%s frames=%d fps=%.3f gaps=%d bad=%d iv=%.2f..%.2f copy mean=%.3f max=%.3f\n", n, r.frames, r.fps, r.gaps, r.bad,
               r.min_iv, r.max_iv, r.copy_mean, r.copy_max);
    };
    pr("main", rm); if (ai_w) pr("ai  ", ra);
    return 0;
}
