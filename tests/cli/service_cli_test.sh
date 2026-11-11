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
if (cd /tmp && "$sandbox_dir/service" 1 18446744073710 >"$sandbox_dir/out" 2>"$sandbox_dir/err"); then
    echo 'X fora da faixa aceito' >&2
    exit 1
fi
grep -q '^X deve ser inteiro entre 1 e 18446744073709 ms$' "$sandbox_dir/err"
controlled_path=${service_path%/*}/controlled_client
(cd /tmp && SERVICE_CLIENT_PATH="$controlled_path" \
    "$sandbox_dir/service" 1 18446744073709 >"$sandbox_dir/out" 2>"$sandbox_dir/err")
[[ ! -s "$sandbox_dir/err" ]]
grep -q '^concluídos: 1$' "$sandbox_dir/out"
set +e
(cd /tmp && SERVICE_CLIENT_PATH=/tmp/cliente-inexistente-us16 "$sandbox_dir/service" 1 5 >"$sandbox_dir/out" 2>"$sandbox_dir/err")
status=$?
set -e
if [[ $status -ne 1 ]] || ! grep -q 'cliente não disponível' "$sandbox_dir/err"; then
    echo 'cliente inexistente aceito' >&2
    exit 1
fi
printf 'CLI: validação e execução de outro diretório passaram (US13).\n'
