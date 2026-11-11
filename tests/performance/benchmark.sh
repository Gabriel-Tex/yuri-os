#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 4 || ! $1 =~ ^(official|controlled)$ ||
      ! $2 =~ ^[1-9][0-9]*$ || ! $3 =~ ^[1-9][0-9]*$ ||
      ! $4 =~ ^[1-9][0-9]{0,2}$ ]]; then
    echo 'Uso: bash tests/performance/benchmark.sh {official|controlled} N X REPETIÇÕES (N, X > 0; 1 a 999 repetições)' >&2
    exit 2
fi

client_kind=$1
count=$2
patience=$3
repetitions=$4
root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
client_path="$root/build/client"
client_env=(env -u SERVICE_CLIENT_PATH -u CONTROLLED_CLIENT_MODE LC_ALL=C)
if [[ $client_kind == controlled ]]; then
    client_path="$root/build/controlled_client"
    client_env+=("SERVICE_CLIENT_PATH=$client_path")
fi
for binary in "$root/build/service" "$root/build/analyst" "$client_path"; do
    if [[ ! -x $binary ]]; then
        printf 'Execute make antes de medir. Executável ausente: %s\n' "$binary" >&2
        exit 1
    fi
done

test_dir=$(mktemp -d)
trap 'rm -f -- "$test_dir/out" "$test_dir/err"; rmdir -- "$test_dir"' EXIT
printf 'cliente,n,x_ms,repeticao,satisfeitos,concluidos,tempo_ms\n'
for ((iteration = 1; iteration <= repetitions; iteration++)); do
    if ! timeout -k 5s 40s "${client_env[@]}" "$root/build/service" \
        "$count" "$patience" >"$test_dir/out" 2>"$test_dir/err" ||
        [[ -s $test_dir/err ]]; then
        printf 'Falha na repetição %d\n' "$iteration" >&2
        cat -- "$test_dir/err" >&2
        exit 1
    fi
    if ! metrics=$(LC_ALL=C awk -v expected="$count" -v emit=1 \
        -f "$root/tests/cli/finite_report.awk" "$test_dir/out"); then
        printf 'Relatório inválido na repetição %d\n' "$iteration" >&2
        exit 1
    fi
    printf '%s,%s,%s,%d,%s\n' "$client_kind" "$count" "$patience" \
        "$iteration" "$metrics"
done
