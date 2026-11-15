#!/bin/bash
# validar.sh - testes rápidos de corretude do simulador SIR.
#
# Uso:  bash validar.sh [COMANDO] [COMANDO_REFERENCIA]
#   COMANDO             como executar o simulador testado (padrão: ./sir_seq)
#   COMANDO_REFERENCIA  (opcional) outra versão para comparar a saída, célula a célula
#
# Exemplos:
#   bash validar.sh                                   # testa a versão sequencial
#   bash validar.sh "mpirun ./sir_mpi_omp" ../sequencial/sir_seq
#
# Os testes usam grades pequenas e terminam em poucos segundos (podem rodar no nó de login).

EXE=${1:-./sir_seq}
REF=${2:-}
N=101
CELULAS=$((N * N))
TMP=$(mktemp -d)
FALHAS=0

verificar() { # uso: verificar "descrição" comando [argumentos...]
    local descricao=$1
    shift
    if "$@"; then
        echo "[PASSOU] $descricao"
    else
        echo "[FALHOU] $descricao"
        FALHAS=$((FALHAS + 1))
    fi
}

iguais() { # mesma série temporal (CSV) e mesmo estado final (imagem PPM)
    cmp -s "$1_serie.csv" "$2_serie.csv" && cmp -s "$1_final.ppm" "$2_final.ppm"
}

# Gera as saídas usadas nos testes
$EXE $N 60 0.3 0.1 7 "$TMP/a" > /dev/null &&
    $EXE $N 60 0.3 0.1 7 "$TMP/b" > /dev/null &&
    $EXE $N 50 1 0 1 "$TMP/c" > /dev/null &&
    $EXE $N 50 1 1 1 "$TMP/d" > /dev/null &&
    $EXE $N 50 0 0.1 1 "$TMP/e" > /dev/null ||
    { echo "Erro ao executar: $EXE"; rm -rf "$TMP"; exit 1; }

# 1) Mesma entrada e mesma semente -> exatamente a mesma saída
verificar "determinismo: duas execuções idênticas" iguais "$TMP/a" "$TMP/b"

# 2) Nenhuma célula é criada ou perdida: S + I + R = N*N em todas as iterações
verificar "conservação: S + I + R = N*N" \
    awk -F, -v T=$CELULAS 'NR > 1 && $2 + $3 + $4 != T { erro = 1 } END { exit erro || NR != 62 }' "$TMP/a_serie.csv"

# 3) beta = 1 e gama = 0: a infecção avança uma célula por iteração em todas as
#    direções e forma um quadrado: I(t) = (2t + 1)^2 (até atingir a borda em t = 50)
verificar "frente determinística: I(t) = (2t+1)^2 com beta=1 e gama=0" \
    awk -F, 'NR > 1 && $3 != (2 * $1 + 1) ^ 2 { erro = 1 } END { exit erro || NR != 52 }' "$TMP/c_serie.csv"

# 4) beta = 1 e gama = 1: cada infectado se recupera na iteração seguinte e a
#    infecção forma um anel: I(t) = 8t e R(t) = (2t - 1)^2 para t >= 1
verificar "anel determinístico: I(t) = 8t e R(t) = (2t-1)^2 com beta=1 e gama=1" \
    awk -F, 'NR > 2 && ($3 != 8 * $1 || $4 != (2 * $1 - 1) ^ 2) { erro = 1 } END { exit erro || NR != 52 }' "$TMP/d_serie.csv"

# 5) beta = 0: ninguém além do paciente zero é infectado
verificar "sem transmissão: S constante com beta=0" \
    awk -F, -v S=$((CELULAS - 1)) 'NR > 1 && $2 != S { erro = 1 } END { exit erro || NR != 52 }' "$TMP/e_serie.csv"

# 6) (opcional) Saída idêntica à de outra versão (ex.: paralela x sequencial).
#    Grade ímpar (257) para exercitar divisões desiguais entre processos.
if [ -n "$REF" ]; then
    $EXE 257 200 0.3 0.1 2026 "$TMP/f" > /dev/null &&
        $REF 257 200 0.3 0.1 2026 "$TMP/g" > /dev/null ||
        { echo "Erro ao executar a comparação com: $REF"; FALHAS=$((FALHAS + 1)); }
    verificar "saída idêntica à referência ($REF)" iguais "$TMP/f" "$TMP/g"
fi

rm -rf "$TMP"
if [ "$FALHAS" -eq 0 ]; then
    echo "Todos os testes passaram."
else
    echo "$FALHAS teste(s) falharam."
fi
exit "$FALHAS"
