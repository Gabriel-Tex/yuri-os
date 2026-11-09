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
    awk -v expected="$count" '
        /^[0-9]+$/ { if (report || seen[$0]++) exit 1; pids++; next }
        /^criados: / { created=$2; report=1; next }
        /^concluídos: / { completed=$2; next }
        /^satisfeitos: / { satisfied=$2; next }
        /^insatisfeitos: / { unsatisfied=$2; next }
        /^taxa de satisfação: / { rate=$4; next }
        /^tempo total: / { elapsed=$3; next }
        { exit 1 }
        END {
            if (pids != expected || created != expected ||
                completed != expected || satisfied + unsatisfied != expected ||
                !report || !rate || elapsed <= 0) exit 1;
            split(rate, fraction, "/");
            if (fraction[1] != satisfied || fraction[2] != expected) exit 1;
        }
    ' "$test_dir/out"
done
printf 'CLI finita: N=1, 11 e 125, PIDs únicos e relatório passaram.\n'
