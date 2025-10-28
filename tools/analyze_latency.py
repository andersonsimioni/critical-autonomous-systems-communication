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

# --- Regex para parsing de logs SEND e RECV ---
send_re = re.compile(r'\[SEND\s+VM=(\d+)\s+PORT=(\d+)\s+TIME=(\d+)\s+TYPE=(\d+)\s+ID=(\d+)\]')
recv_re = re.compile(r'\[RECV\s+VM=(\d+)\s+PORT=(\d+)\s+TIME=(\d+)\s+TYPE=(\d+)\s+ID=(\d+)\]')

# --- Estruturas de dados ---
# Lista de SENDs: (vm, port, type, id, time)
send_list = []
# Lista de RECVs: (vm, port, type, id, time)
recv_list = []

# --- Leitura dos logs ---
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
                # Verifica SEND
                m_send = send_re.search(line)
                if m_send:
                    vm, port, t, typ, mid = m_send.groups()
                    send_list.append((int(vm), int(port), int(typ), int(mid), int(t)))
                    continue

                # Verifica RECV
                m_recv = recv_re.search(line)
                if m_recv:
                    vm, port, t, typ, mid = m_recv.groups()
                    recv_list.append((int(vm), int(port), int(typ), int(mid), int(t)))

# --- Ordenar SENDs por tempo para cada key ---
send_dict = defaultdict(list)  # key=(vm,port,type,id) -> list of times
for vm, port, typ, mid, t in send_list:
    send_dict[(vm, port, typ, mid)].append(t)

for key in send_dict:
    send_dict[key].sort()  # ascending time

# --- Cálculo das latências ---
latency_data = defaultdict(lambda: defaultdict(list))  # recv_vm -> recv_port -> list(latency)
detailed_data = defaultdict(list)  # recv_vm -> rows detalhados

for recv_vm, recv_port, recv_type, mid, recv_t in recv_list:
    key = (recv_vm, recv_port, recv_type, mid)
    send_times = send_dict.get(key)
    if not send_times:
        continue

    # Pega o SEND mais recente antes do RECV
    send_t_candidates = [t for t in send_times if t <= recv_t]
    if not send_t_candidates:
        continue

    send_t = max(send_t_candidates)
    latency_us = recv_t - send_t
    latency_data[recv_vm][recv_port].append(latency_us)
    detailed_data[recv_vm].append([
        recv_vm, recv_port, recv_type, mid, send_t, recv_t, latency_us, latency_us / 1000
    ])

# --- CSV detalhado por VM receptora ---
for recv_vm, rows in detailed_data.items():
    rows.sort(key=lambda x: (x[2], x[3]))  # ordena por TYPE e ID
    csv_file = os.path.join(OUT_DIR, f"vm_{recv_vm}_latency.csv")
    with open(csv_file, "w", newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['recv_vm', 'recv_port', 'type', 'id', 'send_time', 'recv_time', 'latency_us', 'latency_ms'])
        writer.writerows(rows)
    print(f"[INFO] Detailed CSV for VM {recv_vm} saved to {csv_file}")

# --- Resumo por VM receptora ---
summary_file = os.path.join(OUT_DIR, "summary_latency_per_vm.csv")
with open(summary_file, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['recv_vm', 'recv_port', 'count', 'avg_us', 'median_us', 'std_us'])
    for recv_vm, ports in sorted(latency_data.items()):
        for recv_port, latencies in sorted(ports.items()):
            count = len(latencies)
            if count == 0:
                continue
            avg = sum(latencies) / count
            median = statistics.median(latencies)
            stddev = statistics.stdev(latencies) if count > 1 else 0.0
            writer.writerow([recv_vm, recv_port, count,
                             f"{avg:,.2f}", f"{median:,.2f}", f"{stddev:,.2f}"])
print(f"[INFO] Summary CSV per VM saved to {summary_file}")