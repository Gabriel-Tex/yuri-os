#!/usr/bin/env bash
set -euo pipefail
service_path=$1
client_env=(env -u SERVICE_CLIENT_PATH)
if [[ $# -eq 2 ]]; then
    client_env+=("SERVICE_CLIENT_PATH=$2")
fi
test_dir=$(mktemp -d /tmp/service-us16-XXXXXX)
trap 'rm -f -- "$test_dir/out" "$test_dir/err"; rmdir -- "$test_dir"' EXIT
for count in 1 11 125; do
    (cd /tmp && timeout 40s "${client_env[@]}" \
        "$service_path" "$count" 10000 >"$test_dir/out" 2>"$test_dir/err")
    [[ ! -s "$test_dir/err" ]]
    awk -v expected="$count" -f "$(dirname -- "$0")/finite_report.awk" \
        "$test_dir/out"
done
printf 'CLI finita: N=1, 11 e 125, PIDs únicos e relatório passaram.\n'
