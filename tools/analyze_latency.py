#!/usr/bin/env python3
import os
import re
import csv
import statistics
from collections import defaultdict

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # parent of tools/
LOG_DIR = os.path.join(PROJECT_ROOT, "logs")
print(f"[DEBUG] Looking in {os.path.abspath(LOG_DIR)}")
print(f"[DEBUG] Entries: {os.listdir(LOG_DIR)}")
OUT_DIR = "latency_reports"
os.makedirs(OUT_DIR, exist_ok=True)

# Parse log lines like: [SEND VM=1 PORT=102 T=1760915244230275 ID=0]
log_re = re.compile(r'\[(SEND|RECV) VM=(\d+) PORT=(\d+) T=(\d+) ID=(\d+)\]')

# Store messages: {id: {'send': (vm, port, t), 'recv': [(vm, port, t), ...]}}
messages = defaultdict(lambda: {'send': None, 'recv': []})

# Scan log files
for vm_dir in os.listdir(LOG_DIR):
    vm_path = os.path.join(LOG_DIR, vm_dir)
    if not os.path.isdir(vm_path):
        continue
    for fname in os.listdir(vm_path):
        full_path = os.path.join(vm_path, fname)
        if not os.path.isfile(full_path):
            print(f"[DEBUG] No match: {full_path}")
            continue
        with open(full_path, "r") as f:
            for line in f:
                m = log_re.search(line)
                if not m:
                    print(f"[DEBUG] No match: {line.strip()}")
                    continue
                typ, vm, port, t, mid = m.groups()
                vm = int(vm)
                port = int(port)
                t = int(t)
                mid = int(mid)

                if typ == "SEND":
                    messages[mid]['send'] = (vm, port, t)
                elif typ == "RECV":
                    messages[mid]['recv'].append((vm, port, t))

# Organize latency data per destination VM × destination port × source VM × source port
latency_data = defaultdict(lambda: defaultdict(lambda: defaultdict(lambda: defaultdict(list))))
summary_data = defaultdict(lambda: defaultdict(list))  # recv_vm -> send_vm -> list of latencies

for mid, info in messages.items():
    send_info = info['send']
    if send_info is None:
        continue
    send_vm, send_port, send_t = send_info
    for recv_vm, recv_port, recv_t in info['recv']:
        latency_us = recv_t - send_t
        latency_data[recv_vm][recv_port][send_vm][send_port].append(latency_us)
        summary_data[recv_vm][send_vm].append(latency_us)

# Write detailed CSV per destination VM × port
for recv_vm, ports_data in latency_data.items():
    csv_file = os.path.join(OUT_DIR, f"vm_{recv_vm}_latency_stats.csv")
    with open(csv_file, "w", newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['recv_port','send_vm','send_port','count','avg_us','median_us','std_us'])
        for recv_port, send_vms in sorted(ports_data.items()):
            for send_vm, send_ports in sorted(send_vms.items()):
                for send_port, latencies in sorted(send_ports.items()):
                    count = len(latencies)
                    avg = sum(latencies)/count
                    median = statistics.median(latencies)
                    stddev = statistics.stdev(latencies) if count > 1 else 0.0
                    writer.writerow([recv_port, send_vm, send_port, count, f"{avg:.2f}", median, f"{stddev:.2f}"])
    print(f"[INFO] VM {recv_vm} latency statistics CSV saved to {csv_file}")

# Write summary CSV per VM-to-VM pair
summary_file = os.path.join(OUT_DIR, "vm_to_vm_latency_summary.csv")
with open(summary_file, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['recv_vm','send_vm','count','avg_us','median_us','std_us'])
    for recv_vm, send_vms in sorted(summary_data.items()):
        for send_vm, latencies in sorted(send_vms.items()):
            count = len(latencies)
            avg = sum(latencies)/count
            median = statistics.median(latencies)
            stddev = statistics.stdev(latencies) if count > 1 else 0.0
            writer.writerow([recv_vm, send_vm, count, f"{avg:.2f}", median, f"{stddev:.2f}"])
print(f"[INFO] VM-to-VM latency summary CSV saved to {summary_file}")