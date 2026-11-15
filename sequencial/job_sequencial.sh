#!/bin/bash
#SBATCH --job-name=sir-seq
#SBATCH --partition=amd-512
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=0-0:30
#SBATCH --output=sir-seq-%j.out
#SBATCH --error=sir-seq-%j.err
#
# Versão sequencial (baseline) no NPAD: compila, valida, gera um exemplo e mede os tempos.
# Submeter de dentro da pasta sequencial/:   sbatch job_sequencial.sh
#
# --exclusive reserva o nó inteiro para que outros jobs não interfiram na medição de
# tempo. Pode aumentar a espera na fila; para um teste rápido, remova essa linha.
# --time está no formato dias-horas:minutos; ajuste após o primeiro teste.

# 1) Ambiente: use o mesmo compilador em todas as versões de CPU (confira com "module avail")
module load compilers/gnu/14.2.0
echo "Nó: $(hostname) | Início: $(date)"
gcc --version | head -n 1

# 2) Compilação (no próprio nó de computação, sem -march=native)
gcc -O3 -std=c11 -Wall -o sir_seq sir_seq.c || exit 1

# 3) Testes de corretude
bash validar.sh ./sir_seq || exit 1

# 4) Execução de exemplo com série temporal (CSV) e imagem do estado final (PPM)
mkdir -p resultados
./sir_seq 512 300 0.3 0.1 42 resultados/exemplo

# 5) Tempos da linha de base (sem gravar arquivos, para não medir E/S)
ITERACOES=500
REPETICOES=3
ARQUIVO=resultados/tempos_sequencial.csv
echo "versao,n,iteracoes,processos,threads,repeticao,tempo_s" > "$ARQUIVO"
for N in 1024 2048 4096 8192; do
    for REP in $(seq 1 "$REPETICOES"); do
        TEMPO=$(./sir_seq "$N" "$ITERACOES" | awk '/^Tempo/ { print $NF }')
        echo "sequencial,$N,$ITERACOES,1,1,$REP,$TEMPO" >> "$ARQUIVO"
    done
done
cat "$ARQUIVO"
echo "Fim: $(date)"
