# 7. Experimentos e métricas

A especificação do projeto pede que as versões sejam comparadas por **tempo de execução, speedup, eficiência e escalabilidade forte e fraca**, discutindo o overhead de comunicação no MPI, o balanceamento de carga e o impacto da transferência de dados entre CPU e GPU. Este documento define como medir e calcular essas métricas de forma igual para todas as versões.

## 7.1 O que medir

- **Tempo da simulação:** o tempo do laço principal (iterações), impresso por todas as versões na linha `Tempo da simulação (s): …`. Não inclui alocação, inicialização nem gravação de arquivos.
- **Versões de GPU:** também o tempo total com as transferências CPU↔GPU (docs/05 e docs/06).
- **MPI + OpenMP:** também o tempo de comunicação (halo + redução) e o tempo de cálculo por processo (docs/04, passo 9).
- Em toda comparação, use os mesmos `N`, `ITERACOES`, `BETA`, `GAMA` e `SEMENTE`. Nas medições, **não** informe `PREFIXO_SAIDA`, para não medir gravação de arquivos.

## 7.2 Métricas

Sejam `T_seq(N)` o tempo da versão sequencial e `T(p, N)` o tempo da versão paralela com `p` núcleos (no MPI + OpenMP, `p = P × T`, processos × threads).

| Métrica | Fórmula | Ideal |
|---|---|---|
| Speedup | `S(p) = T_seq(N) / T(p, N)` | `S(p) = p` |
| Eficiência | `E(p) = S(p) / p` | 1 (100%) |
| Escalabilidade forte | N fixo, p aumenta: observe `S(p)` e `E(p)` | `E(p)` constante em 1 |
| Escalabilidade fraca | trabalho por núcleo fixo (`N²/p` constante): `E_fraca(p) = T(1, N₁) / T(p, N_p)` | 1 (tempo constante) |

- Na escalabilidade fraca, como o trabalho cresce com `N²`, multiplicar `p` por 4 exige multiplicar `N` por 2 (ex.: p = 1, 4, 16, 64 com N = 2048, 4096, 8192, 16384).
- Nas versões de GPU não há "número de núcleos" comparável. Informe o speedup em relação à sequencial (e à melhor configuração de CPU), com e sem as transferências.

## 7.3 Protocolo de medição

1. **Valide antes de medir** (`validar.sh` com a sequencial como referência). Tempo de versão errada não serve.
2. **Mesma partição** para todas as medições de CPU (`amd-512`) e para todas as de GPU (`gpu-4-a100`).
3. **Nó exclusivo** nas medições de CPU (`--exclusive`) e `--hint=compute_bound` nas versões com threads.
4. **Mesmo compilador e mesmas flags** (`-O3`) nas versões de CPU. Registre `gcc --version`, `module list` e, na GPU, `nvidia-smi`.
5. **Repita** cada configuração pelo menos 3 vezes e use a média (ou a mediana), informando o desvio.
6. **Registre tudo no mesmo formato CSV**, um arquivo por versão:

```
versao,n,iteracoes,processos,threads,repeticao,tempo_s
sequencial,4096,500,1,1,1,12.345678
```

(Os números acima são apenas ilustração do formato.)

## 7.4 Plano sugerido

Ajuste os tamanhos depois de medir a versão sequencial no NPAD: grades muito pequenas medem principalmente *overhead*.

| Experimento | Versão | Configuração |
|---|---|---|
| Linha de base | sequencial | N = 1024, 2048, 4096, 8192; 500 iterações; 3 repetições (`job_sequencial.sh`) |
| Escalabilidade forte | MPI + OpenMP | N fixo (ex.: 8192); P × T = 1, 2, 4, …, 128 em um nó, variando a divisão entre P e T; depois 2 e 4 nós |
| Escalabilidade fraca | MPI + OpenMP | (p, N) = (1, 2048), (4, 4096), (16, 8192), (64, 16384) |
| Tamanhos de entrada | MPI + OpenMP | os mesmos N da linha de base, com a melhor combinação (P, T) |
| GPU | OpenMP-GPU e CUDA | N = 1024 a 16384 na mesma GPU; tempo do laço e total com transferências |

Observação para a escalabilidade fraca: com `ITERACOES` fixo, a região infectada tem aproximadamente o mesmo tamanho absoluto em qualquer N, então a fração de células "caras" (que fazem sorteio) é menor nas grades grandes. Registre essa ressalva ao interpretar os resultados.

## 7.5 Calculando as métricas

Média das repetições por configuração, a partir dos CSVs:

```bash
awk -F, '$1 != "versao" { c = $1","$2","$3","$4","$5; soma[c] += $7; qtd[c]++ }
         END { for (c in soma) print c "," soma[c] / qtd[c] }' resultados/*.csv | sort
```

Com as médias, calcule speedup e eficiência numa planilha e faça os gráficos: tempo × p, speedup × p (com a reta ideal) e eficiência × p.

## 7.6 PaScal Analyzer (opcional)

A apresentação do PaScal Suite mostra como automatizar execuções com várias quantidades de núcleos e entradas, e gerar mapas de calor de eficiência, speedup e escalabilidade forte e fraca. A ferramenta é voltada para aplicações de memória compartilhada (OpenMP, PThreads; o `pascalanalyzer` também tem opção `--mpi`). Exemplo de job, adaptado da apresentação, para variar as threads de um processo:

```bash
#!/bin/bash
#SBATCH --job-name=pascal-sir
#SBATCH --partition=amd-512
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --time=0-1:00
#SBATCH --output=%j.out
#SBATCH --error=%j.err

module load softwares/pascalsuite/2025-07-08
pascalanalyzer ./sir_mpi_omp -c 1,2,4,8,16,32,64,128 -i "2048 500,4096 500,8192 500" -r 3 -o relatorio_amd512.json -v INFO
```

Cada item de `-i` vira os argumentos do programa (ex.: `./sir_mpi_omp 2048 500`). Copie o `relatorio_amd512.json` para o seu computador e abra no PaScal Viewer (<https://pascalsuite.imd.ufrn.br/pascal-viewer>). Consulte `pascalanalyzer -h` para as demais opções.

## 7.7 Pontos de discussão exigidos

- **Overhead de comunicação no MPI:** a cada iteração, cada processo troca 2 linhas de `N + 2` bytes e participa de uma redução, enquanto calcula `(N/P) × N` células. Com N fixo, a comunicação pesa mais à medida que P cresce. Compare o tempo de comunicação com o tempo total.
- **Balanceamento de carga:** as linhas são divididas igualmente, mas o custo por célula depende do estado (docs/02, seção 2.6). Os processos com a frente de infecção trabalham mais. Compare os tempos de cálculo máximo e mínimo entre processos.
- **Transferência CPU↔GPU:** compare o tempo do laço com o tempo das cópias e com a variante que copia os dados a cada iteração (docs/05, seção 5.9).
- **OpenMP em GPU × CUDA:** mesma GPU, mesmos N; compare o tempo do laço e o total, e discuta o esforço de programação de cada modelo.

## 7.8 Registro dos resultados

- Use no relatório e na apresentação apenas resultados medidos no NPAD, informando data, partição, nó, versão do compilador e GPU.
- Testes locais (WSL, notebook) servem só para conferir o funcionamento e não substituem as medições do projeto.
