import os
import re
import csv
import statistics
from collections import defaultdict

# --- Configuration ---
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_DIR = os.path.join(PROJECT_ROOT, "logs")
OUT_DIR = os.path.join(PROJECT_ROOT, "latency_reports")
os.makedirs(OUT_DIR, exist_ok=True)

print(f"[INFO] Scanning logs in {LOG_DIR}")

# --- Regex for log parsing ---
log_re = re.compile(r'\[(SEND|RECV)\s+VM=(\d+)\s+PORT=(\d+)\s+T=(\d+)\s+ID=(\d+)\]')

# --- Data structures ---
send_data = {}  # key: (vm, port, id) -> send timestamp
recv_data = []  # list of tuples: (recv_vm, recv_port, recv_t, id)

# --- Parse logs ---
for vm_dir in os.listdir(LOG_DIR):
    vm_path = os.path.join(LOG_DIR, vm_dir)
    if not os.path.isdir(vm_path):
        continue
    for fname in os.listdir(vm_path):
        full_path = os.path.join(vm_path, fname)
        if not os.path.isfile(full_path):
            continue
        with open(full_path, "r") as f:
            for line in f:
                m = log_re.search(line)
                if not m:
                    continue
                typ, vm, port, t, mid = m.groups()
                vm = int(vm)
                port = int(port)
                t = int(t)
                mid = int(mid)
                if typ == "SEND":
                    send_data[(vm, port, mid)] = t
                else:
                    recv_data.append((vm, port, t, mid))

# --- Compute latencies per recv_vm ---
latency_data = defaultdict(lambda: defaultdict(list))  # recv_vm -> recv_port -> list of latencies
detailed_data = defaultdict(list)  # recv_vm -> list of rows

for recv_vm, recv_port, recv_t, mid in recv_data:
    send_key = (recv_vm, recv_port, mid)
    if send_key not in send_data:
        continue
    send_t = send_data[send_key]
    latency_us = recv_t - send_t
    latency_data[recv_vm][recv_port].append(latency_us)
    detailed_data[recv_vm].append([recv_port, mid, send_t, recv_t, latency_us, latency_us/1000])

# --- Write one detailed CSV per recv_vm ---
for recv_vm, rows in detailed_data.items():
    rows.sort(key=lambda x: x[1])  # sort by message ID
    csv_file = os.path.join(OUT_DIR, f"vm_{recv_vm}_latency.csv")
    with open(csv_file, "w", newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['recv_port','id','send_t','recv_t','latency_us','latency_ms'])
        for row in rows:
            writer.writerow(row)
    print(f"[INFO] Detailed CSV for VM {recv_vm} saved to {csv_file}")

# --- Write summary CSV per VM ---
summary_file = os.path.join(OUT_DIR, "summary_latency_per_vm.csv")
with open(summary_file, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['recv_vm','recv_port','count','avg_us','median_us','std_us'])
    for recv_vm, ports in sorted(latency_data.items()):
        for recv_port, latencies in sorted(ports.items()):
            count = len(latencies)
            avg = sum(latencies)/count
            median = statistics.median(latencies)
            stddev = statistics.stdev(latencies) if count > 1 else 0.0
            writer.writerow([recv_vm, recv_port, count, f"{avg:,.2f}", f"{median:,.2f}", f"{stddev:,.2f}"])
print(f"[INFO] Summary CSV per VM saved to {summary_file}")