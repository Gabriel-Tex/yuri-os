#!/usr/bin/env bash
set -euo pipefail
service_path=$1
client_path=$2
test_dir=$(mktemp -d /tmp/service-us19-XXXXXX)
trap 'rm -f -- "$test_dir/out" "$test_dir/err"; rmdir -- "$test_dir"' EXIT

for mode in demand_missing service_signal; do
    if timeout 20s env SERVICE_CLIENT_PATH="$client_path" \
        CONTROLLED_CLIENT_MODE="$mode" "$service_path" 1 10000 \
        >"$test_dir/out" 2>"$test_dir/err"; then
        echo "falha do cliente aceita: $mode" >&2
        exit 1
    fi
    grep -q '^service: execução finita:' "$test_dir/err"
    grep -q '^service: execução parcial:' "$test_dir/err"
    ! grep -q '^taxa de satisfação:' "$test_dir/out"
done

if timeout 20s env SERVICE_CLIENT_PATH="$client_path" \
    "$service_path" 1 10000 >/dev/full 2>"$test_dir/err"; then
    echo 'falha de saída do Analista aceita' >&2
    exit 1
fi
grep -q '^analyst: imprimir lote:' "$test_dir/err"
grep -q '^service: execução finita:' "$test_dir/err"
printf 'Falhas da CLI: demanda, cliente e saída do Analista passaram.\n'
