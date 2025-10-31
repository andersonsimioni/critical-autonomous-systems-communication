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

# --- Updated regexes with GROUP field ---
# Example: [SEND GROUP=0 VM=4 PORT=65535 TIME=1761952387247026 TYPE=102 ID=1]
pattern_send = re.compile(
    r'\[SEND\s+GROUP=(\d+)\s+VM=(\d+)\s+PORT=(\d+)\s+TIME=(\d+)\s+TYPE=(\d+)\s+ID=(\d+)\]'
)
pattern_recv = re.compile(
    r'\[RECV\s+GROUP=(\d+)\s+VM=(\d+)\s+PORT=(\d+)\s+TIME=(\d+)\s+TYPE=(\d+)\s+ID=(\d+)\]'
)

# --- Data structures ---
send_list = []  # (group, vm, port, type, id, time)
recv_list = []  # (group, vm, port, type, id, time)

# --- Scan log directories ---
for vm_dir in os.listdir(LOG_DIR):
    vm_path = os.path.join(LOG_DIR, vm_dir)
    if not os.path.isdir(vm_path):
        continue
    for fname in os.listdir(vm_path):
        full_path = os.path.join(vm_path, fname)
        if not os.path.isfile(full_path):
            continue

        with open(full_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                # Try SEND
                m = pattern_send.search(line)
                if m:
                    group, vm, port, t, typ, mid = m.groups()
                    send_list.append((int(group), int(vm), int(port), int(typ), int(mid), int(t)))
                    continue

                # Try RECV
                m = pattern_recv.search(line)
                if m:
                    group, vm, port, t, typ, mid = m.groups()
                    recv_list.append((int(group), int(vm), int(port), int(typ), int(mid), int(t)))

# --- Organize SENDs by key for fast lookup ---
# key = (group, vm, port, type, id)
send_dict = defaultdict(list)
for group, vm, port, typ, mid, t in send_list:
    send_dict[(group, vm, port, typ, mid)].append(t)

for key in send_dict:
    send_dict[key].sort()

# --- Compute latencies ---
latency_data = defaultdict(lambda: defaultdict(list))  # recv_vm -> recv_port -> list(latency)
detailed_data = defaultdict(list)  # recv_vm -> rows

for group, recv_vm, recv_port, recv_type, mid, recv_t in recv_list:
    key = (group, recv_vm, recv_port, recv_type, mid)
    send_times = send_dict.get(key)
    if not send_times:
        continue

    # Find the most recent SEND before RECV
    send_candidates = [t for t in send_times if t <= recv_t]
    if not send_candidates:
        continue

    send_t = max(send_candidates)
    latency_us = recv_t - send_t
    latency_data[recv_vm][recv_port].append(latency_us)
    detailed_data[recv_vm].append([
        group, recv_vm, recv_port, recv_type, mid, send_t, recv_t, latency_us, latency_us / 1000
    ])

# --- Write detailed CSVs ---
for recv_vm, rows in detailed_data.items():
    rows.sort(key=lambda x: (x[0], x[3], x[4]))  # sort by GROUP, TYPE, ID
    csv_file = os.path.join(OUT_DIR, f"vm_{recv_vm}_latency.csv")
    with open(csv_file, "w", newline='') as f:
        writer = csv.writer(f)
        writer.writerow([
            'group', 'recv_vm', 'recv_port', 'type', 'id',
            'send_time', 'recv_time', 'latency_us', 'latency_ms'
        ])
        writer.writerows(rows)
    print(f"[INFO] Detailed CSV for VM {recv_vm} saved to {csv_file}")

# --- Summary per receiver VM ---
summary_file = os.path.join(OUT_DIR, "summary_latency_per_vm.csv")
with open(summary_file, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['recv_vm', 'recv_port', 'count', 'avg_us', 'median_us', 'std_us'])
    for recv_vm, ports in sorted(latency_data.items()):
        for recv_port, latencies in sorted(ports.items()):
            if not latencies:
                continue
            count = len(latencies)
            avg = sum(latencies) / count
            median = statistics.median(latencies)
            stddev = statistics.stdev(latencies) if count > 1 else 0.0
            writer.writerow([
                recv_vm, recv_port, count,
                f"{avg:,.2f}", f"{median:,.2f}", f"{stddev:,.2f}"
            ])
print(f"[INFO] Summary CSV per VM saved to {summary_file}")