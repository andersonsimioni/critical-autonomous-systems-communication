#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU (x86_64)
# ---------------------------------

set -e

KERNEL="bzImage"
INITRD="initramfs.cpio"
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"
NUM_VMS=5

# Host folder for logs
LOGDIR=$(pwd)/logs
mkdir -p "$LOGDIR"

# Detect terminal
if command -v xterm &>/dev/null; then
    TERM_CMD="xterm -hold -e"
elif command -v konsole &>/dev/null; then
    TERM_CMD="konsole -e"
elif command -v xfce4-terminal &>/dev/null; then
    TERM_CMD="xfce4-terminal -e"
elif command -v mate-terminal &>/dev/null; then
    TERM_CMD="mate-terminal -e"
else
    echo "[INFO] No graphical terminal found. Running all VMs in the same terminal..."
    TERM_CMD=""
fi

for i in $(seq 0 $((NUM_VMS - 1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"
    echo "[INFO] Starting VM $i with MAC $MAC, logging to $LOGDIR/vm_$i.log"

    # Each VM gets its own subfolder inside LOGDIR
    VM_LOGDIR="$LOGDIR/vm_$i"
    mkdir -p "$VM_LOGDIR"

    if [ -z "$TERM_CMD" ]; then
        # single-terminal mode
        qemu-system-x86_64 \
            -m 1024 \
            -kernel "$KERNEL" \
            -initrd "$INITRD" \
            -append "console=ttyS0 rdinit=/init" \
            -nographic \
            -virtfs local,id=logs_dev,path="$VM_LOGDIR",security_model=none,mount_tag=hostshare \
            -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
            -device e1000,netdev=vlan0,mac=$MAC &
    else
        # each VM in its own terminal
        $TERM_CMD qemu-system-x86_64 \
            -m 1024 \
            -kernel "$KERNEL" \
            -initrd "$INITRD" \
            -append "console=ttyS0 rdinit=/init" \
            -nographic \
            -virtfs local,id=logs_dev,path="$VM_LOGDIR",security_model=none,mount_tag=hostshare \
            -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
            -device e1000,netdev=vlan0,mac=$MAC &
    fi

    sleep 1
done

wait
echo "[INFO] All VMs have finished."
echo "[INFO] Logs saved in $LOGDIR"