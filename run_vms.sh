#!/bin/bash
# ---------------------------------
# Script for multiple VMs at QEMU (x86_64)
# Adds optional timeout to terminate VMs automatically
# ---------------------------------

set -e

KERNEL="bzImage"
INITRD="initramfs.cpio"
MCAST_ADDR="230.0.0.1"
MCAST_PORT="1234"
NUM_VMS=6

# Default timeout in seconds (0 = no timeout). Can be overridden by first numeric arg.
TIMEOUT=0

# If first argument is numeric, use it as timeout (seconds) and shift it out
if [ "$#" -ge 1 ]; then
    if [[ "$1" =~ ^[0-9]+$ ]]; then
        TIMEOUT=$1
        shift
    fi
fi

# Host folder for logs
LOGDIR=$(pwd)/logs
mkdir -p "$LOGDIR"

# Detect terminal emulator to open each VM in its own window (optional)
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

# Array to store background PIDs (terminal processes or qemu processes)
PIDS=()

cleanup() {
    echo "[INFO] Cleaning up VMs..."
    for pid in "${PIDS[@]}"; do
        if [ -z "$pid" ]; then
            continue
        fi
        if kill -0 "$pid" 2>/dev/null; then
            
            kill "$pid" 2>/dev/null || true
            sleep 1
            if kill -0 "$pid" 2>/dev/null; then
                
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
    done
}

trap cleanup EXIT INT TERM

for i in $(seq 0 $((NUM_VMS - 1))); do
    MAC="52:54:00:12:34:$(printf "%02x" $i)"
    echo "[INFO] Starting VM $i with MAC $MAC, logging to $LOGDIR/vm_$i.log"

    # Each VM gets its own subfolder inside LOGDIR
    VM_LOGDIR="$LOGDIR/vm_$i"
    mkdir -p "$VM_LOGDIR"

    PCAP_FILE="$VM_LOGDIR/netdump_vm${i}.pcap"
    echo "[INFO] Starting VM $i (MAC=$MAC) → $PCAP_FILE"

    # number of car VMs (VMs 1..N)
    TOTAL_CARS=$((NUM_VMS - 1))

    # Build qemu command arguments into an array for safer handling
    QEMU_CMD=(qemu-system-x86_64
        -m 1024
        -kernel "$KERNEL"
        -initrd "$INITRD"
        -append "console=ttyS0 rdinit=/init vm_id=$i total_sync_vms=$TOTAL_CARS"
        -nographic
        -virtfs local,id=logs_dev,path="$VM_LOGDIR",security_model=none,mount_tag=hostshare
        -netdev socket,id=vlan0,mcast=$MCAST_ADDR:$MCAST_PORT
        -device e1000,netdev=vlan0,mac=$MAC
        -object filter-dump,id=f1,netdev=vlan0,file="$PCAP_FILE")

    if [ -z "$TERM_CMD" ]; then
        # No terminal, run directly
        "${QEMU_CMD[@]}" &
        pid=$!
    else
        # Start qemu inside a new terminal window
        TERM_CMD_ARRAY=($TERM_CMD)
        "${TERM_CMD_ARRAY[@]}" "${QEMU_CMD[@]}" &
        pid=$!
    fi

    # Store PID for later cleanup
    PIDS+=("$pid")

    # sleep 1
done

if [ "$TIMEOUT" -gt 0 ]; then
    echo "[INFO] Timeout set to ${TIMEOUT}s — VMs will be terminated after this period."
    sleep "$TIMEOUT" &
    SLEEP_PID=$!

    # Monitor running VMs and the sleep timer
    while true; do
        any_running=false
        for pid in "${PIDS[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then
                any_running=true
                break
            fi
        done

        # If no VM is running anymore, break
        if [ "$any_running" = false ]; then
            break
        fi

        # If sleep finished, timeout expired -> cleanup and break
        if ! kill -0 "$SLEEP_PID" 2>/dev/null; then
            echo "[INFO] Timeout reached — terminating VMs"
            cleanup
            break
        fi

        # sleep 1
    done
else
    # Wait until all background jobs finish
    wait
fi

echo "[INFO] All VMs have finished or were terminated."
echo "[INFO] Logs saved in $LOGDIR"