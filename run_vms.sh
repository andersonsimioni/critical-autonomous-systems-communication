#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU (x86_64)
# ---------------------------------

set -e  # abort on error

# Paths
KERNEL="bzImage"
INITRD="initramfs.cpio"

# Multicast
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"

# Number of VMs
NUM_VMS=5

# Directory for logs on the host
LOGDIR=$(pwd)/logs
mkdir -p "$LOGDIR"

# Detect graphical terminal
if command -v xterm &>/dev/null; then
    TERM_CMD="xterm -hold -e"
elif command -v konsole &>/dev/null; then
    TERM_CMD="konsole -e"
elif command -v xfce4-terminal &>/dev/null; then
    TERM_CMD="xfce4-terminal -e"
elif command -v mate-terminal &>/dev/null; then
    TERM_CMD="mate-terminal -e"
else
    echo "[INFO] No graphical terminal found. VMs will run in the same terminal..."
    TERM_CMD=""
fi

# Start VMs
for i in $(seq 0 $((NUM_VMS - 1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"
    VM_LOG="$LOGDIR/vm_$i.log"
    echo "[INFO] Starting VM $i with MAC $MAC, logging to $VM_LOG"

    # Each VM gets its own log file via VirtFS
    mkdir -p "$LOGDIR/vm_$i"

    # Build QEMU command
    QEMU_CMD=(qemu-system-x86_64
        -m 1024
        -kernel "$KERNEL"
        -initrd "$INITRD"
        -append "console=ttyS0 rdinit=/init"
        -serial mon:stdio
        -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT
        -device e1000,netdev=vlan0,mac=$MAC
        -virtfs local,id=logs_dev,path="$LOGDIR/vm_$i",security_model=none,mount_tag=hostshare
    )

    if [ -z "$TERM_CMD" ]; then
        # Same terminal (nographic)
        "${QEMU_CMD[@]}" &
    else
        # Each VM in its own terminal
        $TERM_CMD "${QEMU_CMD[@]}" &
    fi

    sleep 1  # optional: give some time for VM startup
done

wait
echo "[INFO] All VMs have finished."
echo "[INFO] Logs saved in $LOGDIR"