#!/bin/bash
# guest_setup.sh <grid_ms> <budget_ms> <csv> — canonical launch + ping-verified
QEMU_LINE="cd ~/dualos_output && taskset -c 2,3 qemu-system-aarch64 -machine virt,gic-version=3 -cpu host -enable-kvm -smp 2 -m 1G -snapshot -drive file=disk-qemu.vmdk,if=none,id=drv0 -device virtio-blk-device,drive=drv0 -netdev tap,id=n0,ifname=tap-qnx,script=no,downscript=no -device virtio-net-device,netdev=n0,mac=52:54:00:11:11:11 -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-device,rng=rng0 -kernel ifs.bin -nographic -serial mon:stdio 2>&1 | tee /tmp/qnx_boot.log"
launch() {
  tmux kill-session -t qnx 2>/dev/null
  rm -f /tmp/qnx_boot.log
  tmux new-session -d -s qnx "$QEMU_LINE"
}
for attempt in 1 2 3; do
  launch
  ok=0
  for i in $(seq 1 40); do
    grep -q "Startup complete" /tmp/qnx_boot.log 2>/dev/null && ok=1 && break
    sleep 2
  done
  [ $ok = 1 ] || continue
  sleep 3
  for i in $(seq 1 10); do
    tmux send-keys -t qnx "ifconfig vtnet0 192.168.100.10 netmask 255.255.255.0 up" Enter
    sleep 2
    ping -c1 -W1 192.168.100.10 >/dev/null 2>&1 && break
  done
  ping -c1 -W1 192.168.100.10 >/dev/null 2>&1 && { echo "NET UP (attempt $attempt)"; break; }
  [ $attempt = 3 ] && { echo "NET FAIL"; exit 1; }
done
tmux send-keys -t qnx "/proc/boot/p7-timebase-resp &" Enter
sleep 2
if [ "${P7_RT}" = "p7-g6-runtime" ]; then
  tmux send-keys -t qnx "/proc/boot/p7-g6-runtime 192.168.100.1 /proc/boot/tasks_g6.csv" Enter
else
  tmux send-keys -t qnx "/proc/boot/p7-qnx-runtime 192.168.100.1 $3 $1 $2" Enter
fi
sleep 5
tmux capture-pane -t qnx -p | grep -oE "state=[A-Z]+" | tail -1
