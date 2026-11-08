#!/usr/bin/env bash
set -euo pipefail
service_path=$1
sandbox_dir=$(mktemp -d '/tmp/service cli us13 XXXXXX')
trap 'rm -f -- "$sandbox_dir/service" "$sandbox_dir/out" "$sandbox_dir/err"; rmdir -- "$sandbox_dir"' EXIT
ln -s -- "$service_path" "$sandbox_dir/service"
invalid=("" "-1 5" "+1 5" "1 0" "1 -5" "1 +5" "1 5x" "x 5" "18446744073709551616 5" "1 18446744073710" "1 5 extra")
for arguments in "${invalid[@]}"; do
    read -r -a fields <<< "$arguments" || true
    if (cd /tmp && "$sandbox_dir/service" "${fields[@]}" >"$sandbox_dir/out" 2>"$sandbox_dir/err"); then
        echo "entrada inválida aceita: $arguments" >&2
        exit 1
    fi
    if ! grep -q '^uso: service N X' "$sandbox_dir/err"; then
        echo "diagnóstico ausente: $arguments" >&2
        exit 1
    fi
done
set +e
(cd /tmp && "$sandbox_dir/service" 0 5 >"$sandbox_dir/out" 2>"$sandbox_dir/err")
status=$?
set -e
if [[ $status -ne 2 ]] || ! grep -q 'entrada aceita e recursos preparados' "$sandbox_dir/err"; then
    echo 'preparação válida falhou' >&2
    exit 1
fi
set +e
(cd /tmp && "$sandbox_dir/service" 1 5 >"$sandbox_dir/out" 2>"$sandbox_dir/err")
status=$?
set -e
if [[ $status -ne 2 ]] || ! grep -q 'entrada aceita e recursos preparados' "$sandbox_dir/err"; then
    echo 'preparação finita falhou' >&2
    exit 1
fi
printf 'CLI: validação e execução de outro diretório passaram (US13).\n'
