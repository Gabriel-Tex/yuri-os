#!/usr/bin/env bash
set -euo pipefail
service_path=$1
client_env=(env -u SERVICE_CLIENT_PATH)
if [[ $# -eq 2 ]]; then
    client_env+=("SERVICE_CLIENT_PATH=$2")
fi
test_dir=$(mktemp -d /tmp/service-us18-XXXXXX)
trap 'rm -f -- "$test_dir/out" "$test_dir/err"; rmdir -- "$test_dir"' EXIT

check_report() {
    [[ ! -s "$test_dir/err" ]]
    awk '
        /^[0-9]+$/ { if (report || seen[$0]++) exit 1; pids++; next }
        /^criados: / { created=$2; report=1; next }
        /^concluídos: / { completed=$2; next }
        /^satisfeitos: / { satisfied=$2; next }
        /^insatisfeitos: / { unsatisfied=$2; next }
        /^taxa de satisfação: / { rate=$4; next }
        /^tempo total: / { elapsed=$3; next }
        { exit 1 }
        END {
            if (!report || pids != completed ||
                satisfied + unsatisfied != completed ||
                created < completed || elapsed <= 0) exit 1;
            if (completed == 0) {
                if (rate != "não") exit 1;
            } else {
                split(rate, fraction, "/");
                if (fraction[1] != satisfied || fraction[2] != completed)
                    exit 1;
            }
        }
    ' "$test_dir/out"
}

printf s | timeout 20s "${client_env[@]}" \
    "$service_path" 0 10000 >"$test_dir/out" 2>"$test_dir/err"
check_report
[[ $(awk '/^concluídos: / { print $2 }' "$test_dir/out") -eq 0 ]]

{ printf x; sleep 0.7; printf s; } | timeout 20s \
    "${client_env[@]}" "$service_path" 0 10000 \
    >"$test_dir/out" 2>"$test_dir/err"
check_report
[[ $(awk '/^concluídos: / { print $2 }' "$test_dir/out") -gt 0 ]]

{ head -c 65536 /dev/zero | tr '\0' x; printf s; yes x 2>/dev/null || :; } | \
    timeout 20s "${client_env[@]}" "$service_path" 0 10000 \
    >"$test_dir/out" 2>"$test_dir/err"
check_report

if timeout 20s "${client_env[@]}" \
    "$service_path" 0 10000 </dev/null >"$test_dir/out" \
    2>"$test_dir/err"; then
    echo 'EOF no modo infinito foi aceito' >&2
    exit 1
fi
grep -q '^service: execução infinita:' "$test_dir/err"
! grep -q '^taxa de satisfação:' "$test_dir/out"
printf 'CLI infinita: parada, PIDs drenados, taxa e EOF passaram.\n'
