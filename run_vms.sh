#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU
# ---------------------------------

set -e  # in case it failes

# Paths
KERNEL="Image"
INITRD="initramfs.cpio"

# Multicast
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"

# VMs
NUM_VMS=2

# Loop to start VMs
for i in $(seq 0 $((NUM_VMS-1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"  # gera MAC único
    echo "[INFO] Iniciando VM $i com MAC $MAC"

    qemu-system-riscv64 \
        -machine virt \
        -nographic \
        -kernel "$KERNEL" \
        -initrd "$INITRD" \
        -append "root=/dev/ram rw" \
        -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
        -device virtio-net,id=eth0,netdev=vlan0,mac=$MAC &
done

# Wait until every VM has started
wait
echo "[INFO] Todas as VMs foram finalizadas."
