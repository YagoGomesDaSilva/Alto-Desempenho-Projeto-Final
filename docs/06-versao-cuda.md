# 6. Versão CUDA (guia de desenvolvimento)

> **Situação: não implementada neste repositório.** Este guia mostra como desenvolvê-la a partir de `sequencial/sir_seq.c`. Os trechos de código são esboços (não foram compilados nem testados) e os comandos são os recomendados, a confirmar no NPAD.

## 6.1 Ideia

- Uma thread da GPU por célula, organizadas em blocos 2D (ex.: 32 × 8 = 256 threads).
- O *kernel* `passo_kernel` faz o mesmo que a função `passo` da versão sequencial para a sua célula.
- As duas grades ficam na memória da GPU (`cudaMalloc`) durante toda a simulação; os ponteiros são trocados a cada iteração, sem copiar dados.
- As contagens S, I e R são somadas por bloco com `__syncthreads_count` e acumuladas com `atomicAdd` (uma operação atômica por bloco).

## 6.2 Passo a passo do desenvolvimento

### Passo 1: criar o arquivo a partir da versão sequencial

```bash
mkdir -p cuda
cp sequencial/sir_seq.c cuda/sir_cuda.cu
```

O `nvcc` compila arquivos `.cu` como C++. Por isso, converta o retorno de `malloc`/`calloc`: `(unsigned char *)calloc(total, 1)`.

### Passo 2: marcar as funções usadas na GPU

```cpp
__host__ __device__ static uint64_t misturar(uint64_t x) { /* sem alterações */ }
__host__ __device__ static double aleatorio(uint64_t semente, int iteracao, int linha, int coluna) { /* idem */ }
__host__ __device__ static int vizinhas_infectadas(const unsigned char *grade, int largura, int i, int j) { /* idem */ }
```

### Passo 3: escrever o kernel

```cpp
__global__ void passo_kernel(const unsigned char *atual, unsigned char *proxima, Parametros p,
                             int iteracao, unsigned long long *contagem)
{
    const int j = blockIdx.x * blockDim.x + threadIdx.x + 1;   /* coluna, 1..n */
    const int i = blockIdx.y * blockDim.y + threadIdx.y + 1;   /* linha, 1..n */
    const int n = p.n, largura = n + 2;
    int estado = -1;                                           /* -1: thread fora da grade */

    if (i <= n && j <= n) {
        const size_t id = (size_t)i * largura + j;
        estado = atual[id];
        if (estado == SUSCETIVEL) {
            const int k = vizinhas_infectadas(atual, largura, i, j);
            if (k > 0 && aleatorio(p.semente, iteracao, i - 1, j - 1) < p.prob_infeccao[k])
                estado = INFECTADO;
        } else if (estado == INFECTADO) {
            if (aleatorio(p.semente, iteracao, i - 1, j - 1) < p.gama)
                estado = RECUPERADO;
        }
        proxima[id] = (unsigned char)estado;
    }

    /* contagem do bloco: TODAS as threads precisam chegar aqui (nenhum return antes) */
    const int s = __syncthreads_count(estado == SUSCETIVEL);
    const int inf = __syncthreads_count(estado == INFECTADO);
    const int r = __syncthreads_count(estado == RECUPERADO);
    if (threadIdx.x == 0 && threadIdx.y == 0) {
        atomicAdd(&contagem[0], (unsigned long long)s);
        atomicAdd(&contagem[1], (unsigned long long)inf);
        atomicAdd(&contagem[2], (unsigned long long)r);
    }
}
```

A struct `Parametros` é passada por valor e já leva a tabela `prob_infeccao` calculada na CPU, como pede o [docs/02](02-modelo-e-algoritmo.md), seção 2.4.

### Passo 4: laço principal no host

```cpp
#define CUDA_OK(chamada) do { cudaError_t e_ = (chamada); if (e_ != cudaSuccess) { \
        fprintf(stderr, "Erro CUDA: %s (%s:%d)\n", cudaGetErrorString(e_), __FILE__, __LINE__); \
        exit(EXIT_FAILURE); } } while (0)

CUDA_OK(cudaFree(0));                    /* inicializa a GPU fora da medição */
unsigned char *d_atual, *d_proxima;
unsigned long long *d_contagem;
CUDA_OK(cudaMalloc(&d_atual, total));
CUDA_OK(cudaMalloc(&d_proxima, total));
CUDA_OK(cudaMalloc(&d_contagem, 3 * sizeof(unsigned long long)));

double t0 = agora();
CUDA_OK(cudaMemcpy(d_atual, atual, total, cudaMemcpyHostToDevice));     /* CPU -> GPU */
CUDA_OK(cudaMemset(d_proxima, 0, total));                               /* bordas = S */
double t1 = agora();

const dim3 bloco(32, 8);
const dim3 grade((n + bloco.x - 1) / bloco.x, (n + bloco.y - 1) / bloco.y);
for (int t = 0; t < p.iteracoes; t++) {
    CUDA_OK(cudaMemset(d_contagem, 0, 3 * sizeof(unsigned long long)));
    passo_kernel<<<grade, bloco>>>(d_atual, d_proxima, p, t, d_contagem);
    CUDA_OK(cudaGetLastError());
    unsigned long long c[3];
    CUDA_OK(cudaMemcpy(c, d_contagem, sizeof c, cudaMemcpyDeviceToHost));   /* também sincroniza */
    for (int e = 0; e < 3; e++)
        serie[3 * ((size_t)t + 1) + e] = (long long)c[e];
    unsigned char *troca = d_atual; d_atual = d_proxima; d_proxima = troca;
}
double t2 = agora();
CUDA_OK(cudaMemcpy(atual, d_atual, total, cudaMemcpyDeviceToHost));     /* GPU -> CPU */
double t3 = agora();
/* "Tempo da simulação (s)" = t2 - t1 (comparável às outras versões)
   imprima também "Tempo de transferência CPU<->GPU (s)" = (t1 - t0) + (t3 - t2) */
```

- `cudaMemset(..., 0, ...)` deixa a grade `d_proxima` toda S, inclusive a borda fantasma, que o kernel nunca escreve.
- O `cudaMemcpy` das contagens a cada iteração é a sincronização do superpasso BSP. Para medir o custo dele, compare com uma variante que copia as contagens só no final (sem a série temporal).
- No fim, libere a memória com `cudaFree`.

### Passo 5: validar

```bash
bash ../sequencial/validar.sh ./sir_cuda ../sequencial/sir_seq
```

Rode dentro de um job na partição de GPU (seção 6.6). Todos os testes, inclusive o 6 (CSV e imagem idênticos aos da versão sequencial), devem passar.

## 6.3 Pré-requisitos

- CUDA Toolkit (`nvcc`), módulo `compilers/nvidia/cuda/12.6`.
- Partição com GPU (sugestão: `gpu-4-a100`, a mesma da versão OpenMP em GPU). O nó de login não tem GPU, mas pode ser usado para compilar.
- A versão sequencial compilada em `sequencial/sir_seq` (referência para a validação).

## 6.4 Preparação do ambiente

```bash
module load compilers/nvidia/cuda/12.6
nvcc --version
```

Se o `nvcc` reclamar da versão do GCC (`unsupported GNU version`), não carregue o `compilers/gnu/14.2.0` junto. Use o GCC padrão do sistema ou uma versão mais antiga listada em `module avail`.

## 6.5 Compilação

```bash
cd cuda
nvcc -O3 -arch=sm_80 -o sir_cuda sir_cuda.cu
```

`-arch=sm_80` é a arquitetura da A100. Use `sm_70` para a V100 e `sm_90` para a H100/H200.

## 6.6 Execução

Job Slurm (exemplo `job_cuda.sh`):

```bash
#!/bin/bash
#SBATCH --job-name=sir-cuda
#SBATCH --partition=gpu-4-a100
#SBATCH --gpus-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --time=0-0:30
#SBATCH --output=sir-cuda-%j.out
#SBATCH --error=sir-cuda-%j.err

module load compilers/nvidia/cuda/12.6

nvidia-smi                                   # registra qual GPU foi usada
bash ../sequencial/validar.sh ./sir_cuda ../sequencial/sir_seq || exit 1
for N in 1024 2048 4096 8192 16384; do
    ./sir_cuda $N 500
done
```

As mesmas observações da versão OpenMP em GPU valem aqui: sem `--exclusive`, e `--cpus-per-task=2` se a grade passar de ~2 GB.

## 6.7 Parâmetros

Os mesmos da versão sequencial: `N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]`. O tamanho do bloco (32 × 8) é fixo no código; para testar outros tamanhos, ele pode virar um argumento extra no fim da linha de comando.

## 6.8 Verificação e obtenção dos resultados

- No `.out`: a GPU listada pelo `nvidia-smi`, os testes com `[PASSOU]` e as linhas "Estado final" e "Tempo da simulação".
- A linha "Estado final" deve ser idêntica à da versão sequencial para a mesma entrada.
- Registre o tempo do laço e o total com as transferências.
- Copie os resultados com `scp` (docs/01, seção 1.9).

## 6.9 Experimentos específicos desta versão

- N de 1024 a 16384: tempo do kernel × tempo de transferência.
- Tamanho do bloco: 8 × 8, 16 × 16, 32 × 8, 32 × 32.
- Custo da sincronização por iteração (cópia das contagens), como descrito no passo 4.
- Comparação com a versão OpenMP em GPU **na mesma GPU e com os mesmos N**, exigida pela especificação do projeto (docs/07).
