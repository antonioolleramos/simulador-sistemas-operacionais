#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
make >/dev/null
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
count=0
ok() { count=$((count + 1)); printf '[OK] %s\n' "$1"; }

for name in alternancia prioridade loteria cfs; do
    ./escalonador/escalonador "escalonador/entradas/entrada_${name}.txt" > "$TMP"
    grep -q '^Tempo total:' "$TMP"
    ok "Escalonamento: $name"
done

while IFS=';' read -r filename expected; do
    ./memoria/escalonador "memoria/entradas/${filename}" > "$TMP"
    actual="$(tail -n 1 "$TMP" | tr -d '\r')"
    if [ "$actual" != "$expected" ]; then
        echo "Falha em $filename: esperado '$expected', recebido '$actual'" >&2
        exit 1
    fi
    ok "Memória: $filename"
done <<'EOF'
entrada1.txt;21|21|23|16|empate
entrada2.txt;13|12|18|9|LRU
entrada3.txt;4|4|7|3|empate
entradaMemoria.txt;38|40|35|27|NUF
EOF

for case in '42;96' '8;85'; do
    IFS=';' read -r seed expected_total <<< "$case"
    ./entrada_saida/simulador --seed "$seed" entrada_saida/entradas/entrada_ES.txt > "$TMP"
    grep -q "^Tempo total: ${expected_total}$" "$TMP"
    grep -q '^Trocas de pagina (LRU): 40$' "$TMP"
    ok "Entrada/saída: seed $seed"
done

echo "Todos os ${count} cenários verificados com sucesso."
