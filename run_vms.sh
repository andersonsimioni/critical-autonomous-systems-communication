#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU (x86_64)
# ---------------------------------

set -e  # aborta em caso de erro

# Caminhos
KERNEL="bzImage"
INITRD="initramfs.cpio"

# Multicast
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"

# Número de VMs
NUM_VMS=5

# Detecta terminal disponível
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

# Loop para iniciar as VMs
for i in $(seq 0 $((NUM_VMS - 1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"
    echo "[INFO] Iniciando VM $i com MAC $MAC"

    if [ -z "$TERM_CMD" ]; then
        # Sem terminal extra: todas no mesmo terminal
        qemu-system-x86_64 \
            -m 1024 \
            -kernel "$KERNEL" \
            -initrd "$INITRD" \
            -append "console=ttyS0 rdinit=/init" \
            -nographic \
            -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
            -device e1000,netdev=vlan0,mac=$MAC \
            &
    else
        # Cada VM em um terminal separado
        $TERM_CMD "
            qemu-system-x86_64 \
                -m 1024 \
                -kernel \"$KERNEL\" \
                -initrd \"$INITRD\" \
                -append \"console=ttyS0 rdinit=/init\" \
                -nographic \
                -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT \
                -device e1000,netdev=vlan0,mac=$MAC;
            exec bash
        " &
    fi
    #sleep 3  # opcional: dá tempo de inicialização entre VMs
done

wait
echo "[INFO] Todas as VMs foram finalizadas."
