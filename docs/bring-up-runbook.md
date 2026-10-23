# Bring-up runbook — QNX 8.0 guest on AGX Orin (KVM)

目标：在 AGX Orin R36.5 上验证"宿主 Linux 持 GPU 跑 TensorRT 推理 + QNX 8.0 KVM guest 并存"的双域拓扑，并用测量关闭两个已知 bring-up 风险（GIC `Error(19)`、NISV 挂起）。阶段验收标准见 docs/milestones.md，本文件是逐步操作序列。

## 前置条件（一次性）

| item | where | note |
|---|---|---|
| QNX SDP 8.0 (Linux host) | 任意 x86_64 host | SDP 8.0.x，默认装 ~/qnx800；验证 qcc/mkqnximage/mkifs/objdump 可用 |
| Software Center: com.qnx.qnx800.target.qemuvirt | build host | stock startup-qemu-virt；缺它 mkqnximage 在 ifs 步骤报 "Host file not available" |
| Software Center: com.qnx.qnx800.bsp.hypervisor_guest_arm | build host | startup lib，build-qemu-virt.sh 重建 NISV-fixed startup 用 |
| IFS artifacts | build host output/ | ifs.bin + disk-qemu.vmdk + disk-qemu（成对出现） |
| board reachable | `ssh <user>@<board-ip>` | USB / PCIe NIC 均可，桥接子网见 scripts/orin/setup-bridge.sh |
| /dev/kvm | board | 首先确认，见 Step 1 |

## Step 1 — 板端 KVM 可用性（5 分钟）

```bash
ssh <user>@<board-ip>
ls -la /dev/kvm || echo "NO /dev/kvm"
dmesg | grep -i kvm
# expect: kvm module loaded, /dev/kvm present (JetPack 6 L4T enables KVM)
```

## Step 2 — vGIC smoke test（第一个晚上，约 15 分钟）

目的：在全量 boot 之前分离两种失败模式。参考仓 docs/orin-port.md 有 AGX 专属的 `Error(19)` 记录（Orin Nano 上不复现）。

```bash
# minimal vGIC probe — no guest image needed
taskset -c 2,3 qemu-system-aarch64 -machine virt,gic-version=3 -cpu host \
  -enable-kvm -smp 2 -m 1G -nographic -serial mon:stdio \
  -kernel /dev/null 2>&1 | head -20
```

预期结果：
- 出 QEMU banner + null kernel 的"insufficient"类报错 → **vGIC 创建正常**，Error(19) 风险关闭，进 Step 3
- 报 `VmCreateGIC Error(19)` → 走降级阶梯：先重建 L4T 内核 + `tegra234-soc-minimal.dtsi` GICv3 patch（参考仓 docs/orin-port.md 有补丁位置），再试 `gic-version=host`/去掉 flag，最后 TCG（仅功能级，时间基论断作废）

## Step 3 — 全量 guest 启动（NISV-fixed startup）

从构建机同步产物（ifs.bin + vmdk + extent + SHA256SUMS）到板端，然后：

```bash
sudo ./scripts/orin/setup-bridge.sh
./scripts/orin/launch-qnx-gpu-coexist.sh output/ifs.bin output/disk-qemu.vmdk
```

预期串口走到 `Startup complete`。若误用 SDP 自带 startup，NISV 挂起会出现在 `FOUND GICv3 ITS` 之后——确认 IFS 是用本仓 startup 构建的。

guest 起来后：guest 内 `ifconfig` 显示 192.168.100.10；宿主侧 `ping 192.168.100.10`。

## Step 4 — GPU 推理并存（30 分钟）

```bash
# host 侧：跑真实 4-engine 链（引擎构建见 models/README.md，
# 推理链来自兄弟仓 sparsedrive-trt-edge），~10 Hz 等效节奏循环
# board 侧：guest up + echo server；host: ping -c 18000 192.168.100.10
```

## Step 4 验收检查单（全部成立）

- [ ] guest 达到 Startup complete + io-sock up + echo server listening
- [ ] 宿主侧真实 4-engine 链跑通且输出有效（数十 ms/帧量级）
- [ ] 并存 30 分钟：无 OOM、无网络抖动、延迟变化可解释
- [ ] vCPU pinning（2,3）+ guest 1G 冻结在启动脚本中固定
- [ ] Error(19)/NISV 处置结论归档；dmesg + boot log + bench JSON 归档
- [ ] 结果按 results/README.md 政策留存私有侧

## 已知 AGX 与 Orin Nano 差异（来自参考仓）

| item | Orin Nano (参考仓已验证) | AGX Orin (本仓待验证点) |
|---|---|---|
| GIC create | works | `Error(19)` 有记录，Step 2 先测 |
| kernel | 5.15.148-tegra (R36.4.7) | 5.15.148-tegra (R36.5) 同族 |
| QEMU | 6.2.0 distro pkg | 6.2.0 via apt (Ubuntu 22.04 base) |
| debug UART | pins unknown | uarti (SBSA UART) on micro-USB |
| cores | 6x A78AE | 12x A78AE（pinning 余量更大） |
| RAM | 8G (guest 1G) | 32/64G（guest 1G 偏小） |

## 降级阶梯

1. 内核重建 + dtsi GICv3 patch
2. gic-version=host
3. TCG（仅功能级；时间基论断作废）
