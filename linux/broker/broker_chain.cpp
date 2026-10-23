/* broker_chain.cpp - P7 broker v2b: real SparseDrive 4-engine chain.
 * Engines load independently (auto dtypes/shapes), then shared tensors are
 * rewired post-hoc via context->setTensorAddress. Synthetic inputs
 * (latency-valid only: image decode skipped, det_anchor_embed zero) --
 * all four engines run real TensorRT inference.
 * Build (board): g++ -std=c++17 -O2 -I<TRT> -I<cuda> -I. broker_chain.cpp
 *   -L<TRT lib> -L<cuda lib> -lnvinfer -lcudart -ldl */
#include <NvInferPlugin.h>
#include "tensor.h"
#include "net.h"
#include "frame_v2.h"
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

class Logger : public nvinfer1::ILogger {
public:
    void log(Severity s, const char* msg) noexcept override {
        if (s <= Severity::kWARNING) fprintf(stderr, "[trt] %s\n", msg);
    }
} trt_logger;

int main(int argc, char** argv)
{
    if (argc < 5) {
        fprintf(stderr,
            "usage: broker_chain <bind_ip> <engine_dir> <plugin_so> <csv_path>\n"
            "  bind_ip     IP on the host-QNX bridge (e.g. 192.168.100.1)\n"
            "  engine_dir  dir with backbone/head/map/motion_plan *_orin.engine\n"
            "              plus the G6 small-model engines (see models/README.md)\n"
            "  plugin_so   TRT plugin library (EfficientNMS_TRT etc.)\n"
            "  csv_path    per-release telemetry CSV\n");
        return 2;
    }
    const char* bind_ip = argv[1];
    const char* eng_dir = argv[2];
    const char* plug    = argv[3];
    const char* csv     = argv[4];

    void* ph = dlopen(plug, RTLD_NOW | RTLD_GLOBAL);
    if (!ph) { fprintf(stderr, "dlopen plugin: %s\n", dlerror()); return 1; }
    initLibNvInferPlugins(&trt_logger, "");   /* EfficientNMS_TRT etc. */
    auto trt = nvinfer1::createInferRuntime(trt_logger);
    cudaStream_t stream;
    cudaStreamCreate(&stream);

    nv::TensorMap empty;
    std::string ed = eng_dir;
    auto bb = std::make_unique<nv::Net>(ed + "/backbone_fp16_orin.engine", trt, empty);
    auto hd = std::make_unique<nv::Net>(ed + "/head_fp16_orin.engine", trt, empty);
    auto mp = std::make_unique<nv::Net>(ed + "/map_fp16_orin.engine", trt, empty);
    auto mo = std::make_unique<nv::Net>(ed + "/motion_plan_fp16_orin_v2.engine", trt, empty);

    /* wire shared tensors: consumer input address = producer output address */
    auto wire = [&](nv::Net& consumer, const char* name, nv::Tensor* producer) {
        consumer.context->setTensorAddress(name, producer->ptr);
    };
    for (const char* n : {"feat_0","feat_1","feat_2","feat_3"})
        for (nv::Net* c : {hd.get(), mp.get()}) wire(*c, n, bb->bindings[n].get());
    wire(*mo, "det_cls",         hd->bindings["det_cls"].get());
    wire(*mo, "det_reg",         hd->bindings["det_reg"].get());
    wire(*mo, "det_instance_feat", hd->bindings["x.607"].get());
    wire(*mo, "map_cls",         mp->bindings["map_cls"].get());
    wire(*mo, "map_instance_feat", mp->bindings["map_instance_feature"].get());
    wire(*mo, "map_anchor_embed", mp->bindings["map_anchor_embed"].get());

    /* fill leaf inputs */
    auto f32 = [&](nv::Net& n, const char* t, std::vector<float> v) {
        n.bindings[t]->copyFromHost(v.data(), stream);
    };
    std::vector<float> pm(96, 0.f);
    for (int c = 0; c < 6; ++c) {
        pm[c*16+0]=1.f; pm[c*16+5]=1.f; pm[c*16+10]=1.f; pm[c*16+15]=1.f;
    }
    bb->bindings["img"]->fill(0.5f);
    for (nv::Net* n : {hd.get(), mp.get(), mo.get()}) {
        f32(*n, "projection_mat", pm);
        n->bindings["image_wh"]->fill(704.f);
    }
    mo->bindings["spat_shape"]->copyFromHost(
        std::vector<int32_t>{96,176,48,88,24,44}.data(), stream);
    mo->bindings["lvl_start"]->copyFromHost(
        std::vector<int32_t>{0,16896,21120,22176}.data(), stream);
    for (const char* t : {"feat_flat","tp_near","queue_temp_feat",
                          "queue_temp_anchor","period","det_anchor_embed"})
        mo->bindings[t]->fill(0.f);
    cudaStreamSynchronize(stream);
    /* G6: small-model executor engines (T2 yolox / T3 tlr / T4 tlcls) */
    nv::Net* g6[3] = {nullptr, nullptr, nullptr};
    const char* g6names[3] = {"yolox_tiny_fp16.engine", "tlr_yolox_s_fp16.engine",
                              "mobilenetv2_cls_fp16.engine"};
    for (int i = 0; i < 3; ++i) {
        try {
            g6[i] = new nv::Net(std::string(eng_dir) + "/../engines_g6/" + g6names[i], trt, empty);
            fprintf(stderr, "g6 engine %s loaded\n", g6names[i]);
        } catch (const std::exception& e) {
            fprintf(stderr, "g6 engine %s unavailable: %s\n", g6names[i], e.what());
        } catch (...) {
            fprintf(stderr, "g6 engine %s unavailable: unknown\n", g6names[i]);
        }
    }
    fprintf(stderr, "broker_chain: 4 engines wired, inputs staged\n");

    /* TCP server on :7200 */
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7200);
    inet_pton(AF_INET, bind_ip, &a.sin_addr);
    bind(s, (struct sockaddr*)&a, sizeof a);
    listen(s, 1);
    fprintf(stderr, "broker_chain on %s:7200\n", bind_ip);
    FILE* out = fopen(csv, "w");
    fprintf(out, "seq,rel,tx,dl,rx,exec,pre_end,infer_end,post_end,bb,head,map,motion\n");
    uint8_t buf[128];
    for (;;) {
        int c = accept(s, 0, 0);
        if (c < 0) continue;
        fprintf(stderr, "broker_chain: qnx connected\n");
        for (;;) {
            size_t got = 0;
            while (got < 128) {
                ssize_t n = read(c, buf+got, 128-got);
                if (n <= 0) goto disc;
                got += (size_t)n;
            }
            f2_frame_t f; f2_unpack(buf, &f);
            if (f.type != F2_TYPE_RELEASE || !f2_wire_crc_ok(buf)) continue;
            /* G6 small-model tasks: single engine per release */
            if (f.task_id >= 2 && f.task_id <= 4 && g6[f.task_id - 2]) {
                nv::Net* m = g6[f.task_id - 2];
                uint64_t t_rx = now_ns();
                m->Enqueue(stream); cudaStreamSynchronize(stream);
                uint64_t t_end = now_ns();
                f2_frame_t r; memset(&r, 0, sizeof r);
                r.seq = f.seq; r.ver = 0x0002; r.type = F2_TYPE_COMPLETE;
                r.task_id = f.task_id;
                r.t_release = f.t_release; r.t_tx = f.t_tx; r.t_deadline = f.t_deadline;
                r.t_rx = t_rx; r.t_exec = t_rx; r.t_pre_end = t_rx;
                r.t_infer_end = t_end; r.t_post_end = t_end;
                f2_seal(&r); f2_pack(&r, buf);
                size_t sent = 0;
                while (sent < 128) {
                    ssize_t w = write(c, buf+sent, 128-sent);
                    if (w <= 0) goto disc;
                    sent += (size_t)w;
                }
                continue;
            }
            uint64_t t_rx = now_ns();
            uint64_t t_exec = now_ns();
            if (f.task_id == F2_TASK_TRIGGERED) {
                /* G5: one-shot burst (20x backbone) on QNX event trigger */
                fprintf(stderr, "broker_chain: TRIGGER burst x20\n");
                uint64_t b0 = now_ns();
                for (int i = 0; i < 20; ++i) {
                    bb->Enqueue(stream); cudaStreamSynchronize(stream);
                }
                uint64_t b1 = now_ns();
                f2_frame_t r; memset(&r, 0, sizeof r);
                r.seq = f.seq; r.ver = 0x0002; r.type = F2_TYPE_COMPLETE;
                r.task_id = F2_TASK_TRIGGERED;
                r.t_release = f.t_release; r.t_tx = f.t_tx; r.t_deadline = f.t_deadline;
                r.t_rx = t_rx; r.t_exec = t_exec;
                r.t_pre_end = t_rx; r.t_infer_end = b1; r.t_post_end = now_ns();
                f2_seal(&r); f2_pack(&r, buf);
                size_t sent = 0;
                while (sent < 128) {
                    ssize_t w = write(c, buf+sent, 128-sent);
                    if (w <= 0) goto disc;
                    sent += (size_t)w;
                }
                fprintf(out, "%llu,TRIGGER,burst20,%llu\n",
                        (unsigned long long)f.seq, (unsigned long long)(b1-b0));
                fflush(out);
                continue;
            }
            bb->bindings["img"]->fill(0.5f);
            cudaStreamSynchronize(stream);
            uint64_t t_pre_end = now_ns();
            uint64_t t0 = now_ns();
            bb->Enqueue(stream); cudaStreamSynchronize(stream);
            uint64_t t_bb = now_ns();
            hd->Enqueue(stream); cudaStreamSynchronize(stream);
            uint64_t t_h = now_ns();
            mp->Enqueue(stream); cudaStreamSynchronize(stream);
            uint64_t t_m = now_ns();
            mo->Enqueue(stream); cudaStreamSynchronize(stream);
            uint64_t t_mo = now_ns();
            uint64_t t_post_end = now_ns();
            f2_frame_t r; memset(&r, 0, sizeof r);
            r.seq = f.seq; r.ver = 0x0002; r.type = F2_TYPE_COMPLETE;
            r.task_id = F2_TASK_CHAIN;
            r.t_release = f.t_release; r.t_tx = f.t_tx; r.t_deadline = f.t_deadline;
            r.t_rx = t_rx; r.t_exec = t_exec; r.t_pre_end = t_pre_end;
            r.t_infer_end = t_mo; r.t_post_end = t_post_end;
            f2_seal(&r); f2_pack(&r, buf);
            size_t sent = 0;
            while (sent < 128) {
                ssize_t w = write(c, buf+sent, 128-sent);
                if (w <= 0) goto disc;
                sent += (size_t)w;
            }
            fprintf(out, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                (unsigned long long)f.seq,
                (unsigned long long)r.t_release, (unsigned long long)r.t_tx,
                (unsigned long long)r.t_deadline,
                (unsigned long long)r.t_rx, (unsigned long long)r.t_exec,
                (unsigned long long)r.t_pre_end, (unsigned long long)r.t_infer_end,
                (unsigned long long)r.t_post_end,
                (unsigned long long)(t_bb-t0), (unsigned long long)(t_h-t_bb),
                (unsigned long long)(t_m-t_h), (unsigned long long)(t_mo-t_m));
            fflush(out);
        }
disc:
        close(c);
        fprintf(stderr, "broker_chain: disconnected\n");
    }
}
