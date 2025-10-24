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
send_re = re.compile(r'\[SEND\s+VM=(\d+)\s+PORT=(\d+)\s+T=(\d+)\s+ID=(\d+)\s+TO=(\d+)\]')
recv_re = re.compile(r'\[RECV\s+VM=(\d+)\s+PORT=(\d+)\s+T=(\d+)\s+ID=(\d+)\]')

# --- Estruturas de dados ---
# Key para SEND: (from_vm, to_vm, id)
send_data = {}
# Lista de RECVs: (recv_vm, port, t, id)
recv_data = []

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
                    vm, port, t, mid, to_vm = m_send.groups()
                    send_data[(int(vm), int(to_vm), int(mid))] = (int(t), int(port))
                    continue

                # Verifica RECV
                m_recv = recv_re.search(line)
                if m_recv:
                    vm, port, t, mid = m_recv.groups()
                    recv_data.append((int(vm), int(port), int(t), int(mid)))

# --- Cálculo das latências ---
latency_data = defaultdict(lambda: defaultdict(list))  # recv_vm -> recv_port -> list(latency)
detailed_data = defaultdict(list)  # recv_vm -> rows detalhados

for recv_vm, recv_port, recv_t, mid in recv_data:
    # Encontrar o SEND correspondente (de qualquer origem) com mesmo ID e destino = recv_vm
    candidates = [(from_vm, send_t, send_port)
                  for (from_vm, to_vm, msg_id), (send_t, send_port) in send_data.items()
                  if msg_id == mid and to_vm == recv_vm]

    if not candidates:
        continue  # sem envio correspondente

    # Se houver múltiplos envios com mesmo ID, pega o mais recente antes do recv_t
    from_vm, send_t, send_port = max(candidates, key=lambda x: x[1])
    latency_us = recv_t - send_t

    latency_data[recv_vm][recv_port].append(latency_us)
    detailed_data[recv_vm].append([
        from_vm, recv_port, mid, send_t, recv_t, latency_us, latency_us / 1000
    ])

# --- CSV detalhado por VM receptora ---
for recv_vm, rows in detailed_data.items():
    rows.sort(key=lambda x: x[2])  # ordena por ID
    csv_file = os.path.join(OUT_DIR, f"vm_{recv_vm}_latency.csv")
    with open(csv_file, "w", newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['from_vm', 'recv_port', 'id', 'send_t', 'recv_t', 'latency_us', 'latency_ms'])
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
