# PROVENANCE

逐文件来源与许可清单。本仓库复用的外部文件全部原样拷贝（未修改），来源与许可逐条记录于此；后续修改时在 Modifications 列追加说明。

参考仓：https://github.com/ChHaEoN/qnx-linux-dual-vm-proxy （MIT；build-qemu-virt.sh 为 Apache-2.0）

| 本仓路径 | 来源路径（参考仓内） | License | Modifications |
|---|---|---|---|
| contract/reference/frame_v1_reference.h | ipc-test/common/frame.h | MIT | none (2026-09-21) |
| qnx/smoke/server.c + Makefile | ipc-test/qnx-server-net/ | MIT | none (2026-09-21) |
| linux/reference-client/ | ipc-test/linux-client/ | MIT | none (2026-09-21) |
| scripts/orin/setup-bridge.sh | scripts/orin/setup-bridge-orin.sh | MIT | renamed only |
| scripts/orin/reference/launch-qnx-on-orin.sh | scripts/orin/launch-qnx-on-orin.sh | MIT | none (2026-09-21) |
| scripts/orin/reference/launch-qnx-on-orin-tcg.sh | scripts/orin/launch-qnx-on-orin-tcg.sh | MIT | none (2026-09-21) |
| qnx/ifs/build-qemu-virt.sh | orin-native/startup/build-qemu-virt.sh | Apache-2.0 | board path (startup/), patch via patch(1), 2026-09-23 |
| qnx/ifs/startup/qemu-virt/ | orin-native/startup/qemu-virt/ | Apache-2.0 | none (2026-09-21) |
| qnx/ifs/patches/gic-bounded-rwp.patch | orin-native/startup/patches/gic-bounded-rwp.patch | MIT | none (2026-09-21) |
| qnx/ifs/snippets/qnx-safety-vm-post_start.custom | scripts/orin/qnx-safety-vm-post_start.custom | MIT | none (2026-09-21) |
| qnx/smoke/common/{frame.h,frame_io.h} | ipc-test/common/ | MIT | renamed path only |
| linux/reference-client/common/{frame.h,frame_io.h} | ipc-test/common/ | MIT | renamed path only |
| qnx/build/build-ifs-reference.sh | scripts/build-qnx-ifs.sh | MIT | none (2026-09-21; untested on Linux SDP host — first SDP run validates it) |

边界说明（本仓库使用条款与合规边界，与 README "What this is NOT" 段一致）：

- **研究性质 / 非商业用途**：本仓库为纯研究性质，用于非商业目的。QNX SDP 8.0 依 BlackBerry QNX Non-Commercial Use EULA 使用；每个使用者需自备并遵守自己的 QNX SDP license（含 BSP）。本仓库不向任何人授予任何 QNX 相关权利。
- **不分发 QNX/BSP 派生二进制**：qnx/ifs/** 的构建产物（startup-qemu-virt 二进制、ifs.bin、disk 镜像）是 QNX BSP 派生二进制，**不可再分发**；镜像由每个使用者用自己的 SDP 8.0 构建（qnx/build/build-ifs.sh fail-fast 门禁 + 凭据生成脚本）。
- **评测数字不发布**：QNX 软件的 benchmark 数字受 NCEULA 4.6(i) 发布限制，未经 BlackBerry QNX 书面批准不得发布。本仓库只发布方法学与代码，定量结果不入库（results/** 由 .gitignore 强制忽略），复测方法见 bench/ 与 docs/。
- **mkqnximage 模板骨架不入库**：qnx/build/local/ 中 tracked 的内容仅为我们对 mkqnximage 生成骨架的配置增量（local/options 增量文件、凭据生成脚本与其 README）。mkqnximage init 生成的模板骨架（options 默认值、snippets/*、valgrind.files、part.info 等）是 QNX SDP 派生内容，不入库——每个使用者用自有 SDP 8.0 运行 `mkqnximage init` 再生成骨架后，套用本仓的增量文件。
- **无认证/无保证声明**：非 DRIVE OS，也不是其复现；KVM 边界非认证 Type-1 hypervisor；不声称任何实时性保证、determinism、freedom-from-interference、ASIL / ISO 26262。闭环为 CARLA 仿真器非实车。
