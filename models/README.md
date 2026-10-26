# models/ - model file download table (link-only)

This repository commits no weights/ONNX/engines. This table lists every model file needed to reproduce the scheduling research: upstream files are link-only (never rehosted), downloaded, verified, then built into engines on-board with trtexec. Engines and images are environment-derived and likewise not committed.

## 1. SparseDrive 4-engine main chain (T1, control path)

The four ONNX files (backbone / head / map / motion_plan, including the _fixed variants) were exported by step2 of the released sibling repo [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) (self-produced deployment ONNX) and are hosted at HF [hous-lab/sparsedrive-trt-edge](https://huggingface.co/hous-lab/sparsedrive-trt-edge). After download, build them with trtexec --fp16 into the engine names broker_chain expects (backbone / head / map / motion_plan, motion_plan exported from v2_fixed; see [linux/broker/broker_chain.cpp](../linux/broker/broker_chain.cpp) and the sibling repo's step3_fp16_orin scripts). For upstream anchor npy/npz files, the authoritative list is the sibling repo's [ckpts/README.md](https://github.com/hous-lab/sparsedrive-trt-edge/blob/main/ckpts/README.md).

## 2. Official Autoware ONNX (T2-T4, real-envelope competing workloads)

Officially hosted by Autoware (the HF AutowareFoundation org), downloaded into models/download/:

| File | Upstream HF repo | Placement | Verification |
|---|---|---|---|
| yolox-tiny.onnx | [tensorrt_yolox](https://huggingface.co/AutowareFoundation/tensorrt_yolox) | models/download/ | sha256 against the HF LFS oid (command below) |
| tlr_car_ped_yolox_s_batch_1.onnx | [traffic_light_fine_detector](https://huggingface.co/AutowareFoundation/traffic_light_fine_detector) | models/download/ | same |
| traffic_light_classifier_mobilenetv2_batch_1.onnx | [traffic_light_classifier](https://huggingface.co/AutowareFoundation/traffic_light_classifier) | models/download/ | same |
| pts_backbone_neck_head_centerpoint_tiny.onnx (T5, extension, not integrated) | an Autoware centerpoint repo (URL per the Autoware docs) | models/download/ | same |

Verification command (the HF paths-info API returns the LFS sha256 oid):

```bash
curl -s https://huggingface.co/api/models/AutowareFoundation/tensorrt_yolox/paths-info/main \
  -d '{"paths":["yolox-tiny.onnx"]}' -H 'Content-Type: application/json' | jq -r '.[0].lfs.oid'
```

## 3. trtexec build commands (on the Orin)

```bash
trtexec --onnx=models/download/yolox-tiny.onnx --fp16 \
        --saveEngine=models/download/yolox_tiny_fp16.engine
trtexec --onnx=tlr_car_ped_yolox_s_batch_1.onnx --fp16 --saveEngine=tlr_yolox_s_fp16.engine
trtexec --onnx=traffic_light_classifier_mobilenetv2_batch_1.onnx --fp16 \
        --saveEngine=mobilenetv2_cls_fp16.engine
# SparseDrive four engines: see the sibling repo sparsedrive-trt-edge step3_fp16_orin build scripts
```

Note: the yolox ONNX embeds the EfficientNMS_TRT plugin, so initLibNvInferPlugins is needed at runtime (broker_chain already calls it) plus dlopen of the plugin library.

## 4. Non-commitment policy

- Upstream weights/ONNX/anchor files are never committed and never rehosted (red line)
- Official Autoware ONNX is released under Apache-2.0 (the three HF AutowareFoundation repos, verified via the HF API on 2026-10-25); the self-produced deployment ONNX is likewise hosted as Apache-2.0 in the hous-lab HF org
- TRT engines are environment-derived and not committed
- QNX/BSP-derived artifacts (startup binary, ifs.bin, disk images) are not redistributable; every user builds with their own SDP 8.0 (see [qnx/build/build-ifs.sh](../qnx/build/build-ifs.sh))
