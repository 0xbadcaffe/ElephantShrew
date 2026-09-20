#!/usr/bin/env bash
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "Run this isolated lab as root in a test VM." >&2; exit 1; }
[[ $# -eq 1 ]] || { echo "Usage: $0 /absolute/path/to/elephantshrew" >&2; exit 1; }
BIN=$(realpath "$1")
[[ -x $BIN ]] || { echo "Not executable: $BIN" >&2; exit 1; }
command -v ip >/dev/null
command -v tc >/dev/null
S=es-src D=es-dst M=es-mid
for ns in "$S" "$D" "$M"; do
    if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
        echo "Refusing to reuse namespace: $ns" >&2; exit 1
    fi
done
TMP=$(mktemp -d)
created=()
pid=
cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    if [[ -n ${pid:-} ]] && kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        for _ in {1..50}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    for ns in "${created[@]}"; do ip netns del "$ns" 2>/dev/null || true; done
    if [[ $rc -ne 0 ]]; then
        echo "Lab failed; logs retained in $TMP" >&2
        [[ ! -f $TMP/service.log ]] || tail -60 "$TMP/service.log" >&2
    else
        rm -rf "$TMP"
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for ns in "$S" "$D" "$M"; do ip netns add "$ns"; created+=("$ns"); done
ip -n "$M" link add es-left type veth peer name src0
ip -n "$M" link set src0 netns "$S"
ip -n "$M" link add es-right type veth peer name dst0
ip -n "$M" link set dst0 netns "$D"
ip -n "$M" link add es-br type bridge
for dev in es-left es-right; do
    ip -n "$M" link set "$dev" master es-br
    ip -n "$M" link set "$dev" up
done
ip -n "$M" link set es-br up
for ns in "$S" "$D" "$M"; do ip -n "$ns" link set lo up; done
ip -n "$S" link set src0 up
ip -n "$D" link set dst0 up
ip -n "$S" addr add 192.0.2.1/24 dev src0
ip -n "$D" addr add 192.0.2.2/24 dev dst0
ip netns exec "$S" ping -q -c 3 -W 1 192.0.2.2
cat >"$TMP/observe.json" <<'JSON'
{
  "capture": {"interfaces": ["es-left", "es-right"], "backend": "none"},
  "routing": {"enabled": false, "engine": "pcap"},
  "ebpf": {
    "enabled": true, "hook": "tc", "attach_api": "legacy", "mode": "observe",
    "required": true, "directions": ["ingress", "egress"], "sample_every": 1,
    "snapshot_bytes": 0, "ring_buffer_bytes": 1048576,
    "event_queue_capacity": 8192, "stats_interval_ms": 1000,
    "drain_timeout_ms": 3000, "classify": true, "flow_enabled": false,
    "max_flows": 4096, "select_l4_proto": 0
  },
  "ui": {"show_startup_art": false}
}
JSON
ip netns exec "$M" "$BIN" -c "$TMP/observe.json" >"$TMP/service.log" 2>&1 &
pid=$!
ready=0
for _ in {1..100}; do
    kill -0 "$pid" 2>/dev/null || { echo "Service exited before ready" >&2; exit 1; }
    if grep -q 'eBPF ready' "$TMP/service.log"; then ready=1; break; fi
    sleep 0.1
done
[[ $ready == 1 ]] || { echo "No ready indication" >&2; exit 1; }
ip netns exec "$S" ping -q -c 200 -i 0.01 -W 1 192.0.2.2
stats=0
for _ in {1..50}; do
    if grep -Eq 'eBPF stats iface=es-left direction=ingress packets=[1-9][0-9]*' "$TMP/service.log"; then
        stats=1; break
    fi
    sleep 0.1
done
[[ $stats == 1 ]] || { echo "No positive ingress counter" >&2; exit 1; }
kill -TERM "$pid"
for _ in {1..100}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$pid" 2>/dev/null; then echo "Shutdown did not complete" >&2; exit 1; fi
wait "$pid"
pid=
for dev in es-left es-right; do
    for direction in ingress egress; do
        filters=$(ip netns exec "$M" tc filter show dev "$dev" "$direction")
        if grep -Eq 'es_ingress|es_egress' <<<"$filters"; then
            echo "Observer filter remains: $dev/$direction" >&2; exit 1
        fi
    done
done
ip netns exec "$S" ping -q -c 3 -W 1 192.0.2.2
echo "PASS: passive multi-interface connectivity, counters, and normal cleanup"
