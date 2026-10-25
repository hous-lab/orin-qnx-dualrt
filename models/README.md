# models/ - 模型文件下载表（link-only）

本仓库不提交任何权重/ONNX/引擎。本表给出复现调度研究需要的全部模型文件：上游 link-only（不得 rehost），下载后校验，再在板上用 trtexec 构建引擎。引擎与镜像属环境派生产物，同样不入库。

## 1. SparseDrive 4-engine 主链（T1，控制路径）

四枚 ONNX（backbone / head / map / motion_plan，含 _fixed 变体）由已发布兄弟仓 [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) 的 step2 导出（自产部署 ONNX），托管于 HF [hous-lab/sparsedrive-trt-edge](https://huggingface.co/hous-lab/sparsedrive-trt-edge)。下载后在 Orin 上以 trtexec --fp16 构建为 broker_chain 期望的引擎名（backbone / head / map / motion_plan，motion_plan 用 v2_fixed 导出；具体见 [linux/broker/broker_chain.cpp](../linux/broker/broker_chain.cpp) 与兄弟仓 step3_fp16_orin 脚本）。锚点 npy/npz 等上游文件清单以兄弟仓 [ckpts/README.md](https://github.com/hous-lab/sparsedrive-trt-edge/blob/main/ckpts/README.md) 为准。

## 2. Autoware 官方 ONNX（T2-T4，真实包络竞争负载）

Autoware 官方托管（HF AutowareFoundation org），下载到 models/download/：

| 文件 | 上游 HF 仓 | 放置路径 | 校验 |
|---|---|---|---|
| yolox-tiny.onnx | [tensorrt_yolox](https://huggingface.co/AutowareFoundation/tensorrt_yolox) | models/download/ | sha256 对照 HF LFS oid（命令见下） |
| tlr_car_ped_yolox_s_batch_1.onnx | [traffic_light_fine_detector](https://huggingface.co/AutowareFoundation/traffic_light_fine_detector) | models/download/ | 同上 |
| traffic_light_classifier_mobilenetv2_batch_1.onnx | [traffic_light_classifier](https://huggingface.co/AutowareFoundation/traffic_light_classifier) | models/download/ | 同上 |
| pts_backbone_neck_head_centerpoint_tiny.onnx（T5，拓展未接入） | Autoware centerpoint 相关仓（URL 以 Autoware 文档为准） | models/download/ | 同上 |

校验命令（HF paths-info API 返回 LFS sha256 oid）：

```bash
curl -s https://huggingface.co/api/models/AutowareFoundation/tensorrt_yolox/paths-info/main \
  -d '{"paths":["yolox-tiny.onnx"]}' -w '' -H 'Content-Type: application/json' | jq -r '.[0].lfs.oid'
```

## 3. trtexec 构建命令（在 Orin 上）

```bash
trtexec --onnx=models/download/yolox-tiny.onnx --fp16 \
        --saveEngine=models/download/yolox_tiny_fp16.engine
trtexec --onnx=tlr_car_ped_yolox_s_batch_1.onnx --fp16 --saveEngine=tlr_yolox_s_fp16.engine
trtexec --onnx=traffic_light_classifier_mobilenetv2_batch_1.onnx --fp16 \
        --saveEngine=mobilenetv2_cls_fp16.engine
# SparseDrive 四引擎: 见兄弟仓 sparsedrive-trt-edge step3_fp16_orin 的构建脚本
```

注意：yolox ONNX 内嵌 EfficientNMS_TRT 插件，运行时需 initLibNvInferPlugins（broker_chain 已调用）并 dlopen 对应插件库。

## 4. 不入库政策

- 上游权重/ONNX/锚点文件绝不入库、绝不 rehost（版权红线）
- Autoware 上游 ONNX 依 Apache-2.0 发布（HF AutowareFoundation 三仓，2026-10-25 经 HF API 核验）；自产部署 ONNX 同以 Apache-2.0 托管于 hous-lab HF
- TRT 引擎属环境派生产物，不入库
- QNX/BSP 派生产物（startup 二进制、ifs.bin、磁盘镜像）不可再分发，各使用者以自己的 SDP 8.0 构建（见 [qnx/build/build-ifs.sh](../qnx/build/build-ifs.sh)）
