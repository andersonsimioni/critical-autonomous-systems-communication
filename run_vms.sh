#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU
# ---------------------------------

set -e  # exit if fail

# Paths
KERNEL="bzImage"         # precisa compilar kernel x86 (make bzImage)
INITRD="initramfs.cpio"

# Multicast
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"

# VMs
NUM_VMS=2

# Finds available terminal
if command -v xterm &>/dev/null; then
    TERM_CMD="xterm -hold -e"
elif command -v konsole &>/dev/null; then
    TERM_CMD="konsole -e"
elif command -v xfce4-terminal &>/dev/null; then
    TERM_CMD="xfce4-terminal -e"
elif command -v mate-terminal &>/dev/null; then
    TERM_CMD="mate-terminal -e"
else
    echo "[INFO] Nenhum emulador gráfico encontrado. Rodando todas as VMs no mesmo terminal..."
    TERM_CMD=""
fi

# Loop for VMs
for i in $(seq 0 $((NUM_VMS-1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"
    echo "[INFO] Iniciando VM $i com MAC $MAC"

    if [ -z "$TERM_CMD" ]; then
        qemu-system-x86_64 \
            -m 1024 \
            -kernel "$KERNEL" \
            -initrd "$INITRD" \
            -nographic \
            -append "console=ttyS0 init=/init root=/dev/ram0 rw" \
            -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
            -device e1000,netdev=vlan0,mac=$MAC &
    else
        $TERM_CMD "
            qemu-system-x86_64 \
                -m 1024 \
                -kernel \"$KERNEL\" \
                -initrd \"$INITRD\" \
                -nographic \
                -append \"console=ttyS0 init=/init root=/dev/ram0 rw\" \
                -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
                -device e1000,netdev=vlan0,mac=$MAC;
            exec bash
        " &
    fi
done

wait
echo "[INFO] Todas as VMs foram finalizadas."
