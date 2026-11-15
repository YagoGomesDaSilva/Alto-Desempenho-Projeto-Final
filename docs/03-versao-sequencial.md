# 3. Versão sequencial (baseline)

**Responsável:** Yago Gomes da Silva.
**Situação:** implementada e testada localmente. **Ainda não foi executada no NPAD**; as seções 3.8 e 3.9 descrevem como fazer isso.

A versão sequencial é a referência do projeto em dois sentidos: define o **resultado correto** (as versões paralelas devem reproduzir sua saída exatamente) e o **tempo de base** `T_seq` usado no cálculo de speedup e eficiência.

## 3.1 Arquivos

| Arquivo | Conteúdo |
|---|---|
| `sequencial/sir_seq.c` | o simulador: um único arquivo em C11, sem dependências externas |
| `sequencial/validar.sh` | testes de corretude (também servem para as versões paralelas) |
| `sequencial/job_sequencial.sh` | job Slurm: compila, valida, gera um exemplo e mede os tempos |

## 3.2 Como o desenvolvimento foi feito, passo a passo

1. **Definir o modelo** (estados, regras e parâmetros), conforme o [docs/02](02-modelo-e-algoritmo.md).
2. **Representar a grade** como um vetor de `(N + 2) × (N + 2)` bytes, com borda fantasma, e usar **dois buffers** (`atual` e `proxima`) para que todas as células sejam atualizadas a partir do mesmo estado anterior.
3. **Escrever a atualização de uma iteração** (`passo`): para cada célula, contar as vizinhas infectadas (*stencil* 3×3) e aplicar as regras.
4. **Usar um gerador aleatório sem estado** (`aleatorio`), para que o resultado não dependa da ordem de processamento.
5. **Contar S, I e R na mesma passada** da atualização (é a agregação global do BSP), guardando a série temporal.
6. **Cronometrar apenas o laço principal**, com `clock_gettime(CLOCK_MONOTONIC)`.
7. **Gravar saídas opcionais** (CSV da série e imagem PPM do estado final), fora da região cronometrada.
8. **Validar** com casos de resultado conhecido (`validar.sh`).
9. **Medir os tempos** no NPAD (`job_sequencial.sh`).

Organização do código:

| Função | Papel |
|---|---|
| `misturar`, `aleatorio` | gerador pseudoaleatório sem estado |
| `vizinhas_infectadas` | conta as infectadas entre as 8 vizinhas de uma célula |
| `passo` | uma iteração: calcula o novo estado e conta S, I e R. **É a função que as versões paralelas vão paralelizar.** |
| `agora` | relógio de parede, em segundos |
| `salvar_csv`, `salvar_ppm` | arquivos de saída |
| `main` | lê os parâmetros, aloca as grades, define a condição inicial, executa o laço cronometrado e imprime os resultados |

## 3.3 Pré-requisitos

- Linux (o NPAD, ou WSL/Linux para testes locais).
- Compilador C com suporte a C11. No NPAD: módulo `compilers/gnu/14.2.0` (o GCC padrão do sistema também deve servir).
- `bash`, `awk` e `cmp` para a validação (presentes em qualquer Linux).

## 3.4 Preparação do ambiente (NPAD)

```bash
ssh -p4422 SEU_USUARIO@sc2.npad.ufrn.br
git clone https://github.com/YagoGomesDaSilva/Alto-Desempenho-Projeto-Final.git   # ou copie com scp (docs/01)
cd Alto-Desempenho-Projeto-Final/sequencial
module load compilers/gnu/14.2.0
```

## 3.5 Compilação

```bash
gcc -O3 -std=c11 -Wall -o sir_seq sir_seq.c
```

- `-O3`: nível de otimização. **Use o mesmo nível em todas as versões** para que a comparação seja justa. Em teste local, este código ficou cerca de 4 vezes mais lento com `-O2` (o GCC só desenrola por completo o laço 3×3 de vizinhas em `-O3`).
- `-std=c11`: padrão da linguagem.
- `-Wall`: mostra avisos (a compilação não deve gerar nenhum).
- Não use `-march=native` no nó de login (ver docs/01, seção 1.5).

## 3.6 Execução e parâmetros

```bash
./sir_seq N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]
```

| Argumento | Significado | Padrão |
|---|---|---|
| `N` | lado da grade (N × N células), N ≥ 1 | obrigatório |
| `ITERACOES` | número de iterações, ≥ 0 | obrigatório |
| `BETA` | probabilidade de transmissão por vizinha infectada, em [0, 1] | 0.3 |
| `GAMA` | probabilidade de recuperação por iteração, em [0, 1] | 0.1 |
| `SEMENTE` | semente do gerador pseudoaleatório | 42 |
| `PREFIXO_SAIDA` | se informado, grava `PREFIXO_serie.csv` e `PREFIXO_final.ppm` | (não grava) |

Para informar um argumento opcional, é preciso informar também os anteriores.

```bash
./sir_seq 1024 500                       # só contagens e tempo (uso nas medições)
./sir_seq 512 300 0.3 0.1 42 exemplo     # grava também exemplo_serie.csv e exemplo_final.ppm
```

Saída do segundo comando em teste local:

```
Simulação SIR em grade - versão sequencial
Grade: 512 x 512 | Iterações: 300
Parâmetros: beta = 0.3 | gama = 0.1 | semente = 42
Estado final: S = 33136 | I = 11411 | R = 217597
Tempo da simulação (s): 0.125868
Arquivo gerado: exemplo_serie.csv
Arquivo gerado: exemplo_final.ppm
```

O cálculo é determinístico e usa só aritmética inteira e operações básicas de ponto flutuante IEEE 754, que dão o mesmo resultado em qualquer processador compatível. Por isso **a linha "Estado final" deve sair idêntica em qualquer máquina**, inclusive no NPAD; só o tempo muda. Se sair diferente, investigue (por exemplo, opções como `-ffast-math`).

No nó de login, rode apenas casos pequenos como esse (limite de 30 minutos de CPU). As medições vão para o Slurm (seção 3.8).

## 3.7 Validação

```bash
bash validar.sh
```

Saída esperada:

```
[PASSOU] determinismo: duas execuções idênticas
[PASSOU] conservação: S + I + R = N*N
[PASSOU] frente determinística: I(t) = (2t+1)^2 com beta=1 e gama=0
[PASSOU] anel determinístico: I(t) = 8t e R(t) = (2t-1)^2 com beta=1 e gama=1
[PASSOU] sem transmissão: S constante com beta=0
Todos os testes passaram.
```

O que cada teste verifica (todos com uma grade 101 × 101):

1. **Determinismo**: duas execuções com a mesma entrada geram o mesmo CSV e a mesma imagem.
2. **Conservação**: em todas as iterações, S + I + R = N².
3. **Frente determinística**: com β = 1 e γ = 0, toda suscetível com vizinha infectada é infectada. A infecção forma um quadrado que cresce uma célula por iteração para cada lado: I(t) = (2t + 1)². Esse teste detecta erros de vizinhança, de borda e de buffer duplo.
4. **Anel determinístico**: com β = 1 e γ = 1, cada infectado se recupera na iteração seguinte. A infecção vira um anel: I(t) = 8t e R(t) = (2t − 1)².
5. **Sem transmissão**: com β = 0, S fica constante (N² − 1).

O script aceita outro executável e uma referência para comparação célula a célula (teste 6), o que será usado pelas versões paralelas:

```bash
bash validar.sh "mpirun ./sir_mpi_omp" ../sequencial/sir_seq
```

## 3.8 Execução no NPAD com Slurm

```bash
cd ~/Alto-Desempenho-Projeto-Final/sequencial
sbatch job_sequencial.sh          # responde "Submitted batch job <JOBID>"
squeue -u $USER                   # aguarde o job sair da fila
cat sir-seq-<JOBID>.out
```

O que o `job_sequencial.sh` faz, na partição `amd-512` com o nó exclusivo:

1. carrega o GCC e registra o nó e a versão do compilador;
2. compila com `-O3`;
3. roda `validar.sh` e para se algum teste falhar;
4. gera um exemplo com arquivos de saída (`resultados/exemplo_*`);
5. mede o tempo para N = 1024, 2048, 4096 e 8192, com 500 iterações e 3 repetições cada, e grava em `resultados/tempos_sequencial.csv`.

Para mudar tamanhos, iterações ou repetições, edite as variáveis no próprio script. Se o job for cancelado por tempo, aumente `--time`. Para um teste rápido, remova `--exclusive` (a espera na fila tende a ser menor, mas os tempos medidos ficam menos confiáveis).

## 3.9 Verificação e obtenção dos resultados

| Arquivo | O que conferir |
|---|---|
| `sir-seq-<JOBID>.out` | nó, versão do GCC, os 5 testes com `[PASSOU]`, a saída do exemplo e a tabela de tempos |
| `sir-seq-<JOBID>.err` | erros; deve estar vazio ou ter apenas mensagens do `module` |
| `resultados/exemplo_serie.csv` | série S, I, R por iteração; abra numa planilha e faça o gráfico das três curvas |
| `resultados/exemplo_final.ppm` | imagem do estado final (ver abaixo como visualizar) |
| `resultados/tempos_sequencial.csv` | tempos de base `T_seq` por tamanho de grade, usados no speedup das versões paralelas ([docs/07](07-experimentos-e-metricas.md)) |

Conferências rápidas:

- A linha "Estado final" do exemplo deve ser `S = 33136 | I = 11411 | R = 217597`.
- Em `tempos_sequencial.csv`, o tempo deve crescer cerca de 4 vezes a cada vez que N dobra (custo proporcional a N²), e as repetições devem dar valores próximos.

Para visualizar a imagem PPM, copie-a para o seu computador e abra no GIMP, IrfanView ou XnView, ou converta para PNG com Python e a biblioteca Pillow (`pip install pillow`):

```bash
python -c "from PIL import Image; Image.open('exemplo_final.ppm').save('exemplo_final.png')"
```

Para copiar os resultados para o seu computador, no seu computador:

```bash
scp -P4422 -r SEU_USUARIO@sc2.npad.ufrn.br:~/Alto-Desempenho-Projeto-Final/sequencial/resultados .
```

## 3.10 O que já foi testado e o que falta

Testes locais (notebook com Intel Core i5-1235U, Ubuntu 24.04 no WSL2, GCC 13.3):

- Compilação com `-Wall -Wextra -pedantic -Wshadow -Wconversion` sem nenhum aviso.
- `validar.sh`: os 5 testes passaram. Também passaram com o executável instrumentado com AddressSanitizer e UndefinedBehaviorSanitizer (nenhum erro de memória ou comportamento indefinido), e as saídas dos executáveis `-O2`, `-O3` e instrumentado foram idênticas.
- Os testes detectaram os 4 defeitos introduzidos de propósito para conferir a validação: atualização no próprio buffer, vizinhas diagonais ignoradas, coordenada do sorteio deslocada e contagem errada de recuperados.
- Casos de borda: N = 1, N = 2, `ITERACOES` = 0, parâmetros inválidos e pasta de saída inexistente.
- O fluxo do `job_sequencial.sh` foi simulado localmente com bash (sem Slurm e com tamanhos menores).

Tempos locais com `-O3` e 500 iterações, **apenas para estimar o `--time` (não são resultados do projeto)**: N = 1024 → 0,66 s; N = 2048 → 2,75 s; N = 4096 → 11,0 s.

**Falta fazer no NPAD:** executar `job_sequencial.sh` na `amd-512`, conferir a validação e registrar os tempos oficiais da linha de base.
