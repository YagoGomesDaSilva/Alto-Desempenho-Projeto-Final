/*
 * sir_seq.c - Simulador de propagação de infecção em grade (modelo SIR)
 * Versão SEQUENCIAL (baseline) do Projeto Final de Computação de Alto Desempenho (DIM1116).
 *
 * Cada célula de uma grade N x N está em um de três estados:
 *     SUSCETIVEL (S)  ->  INFECTADO (I)  ->  RECUPERADO (R)
 *
 * A cada iteração, todas as células são atualizadas "ao mesmo tempo", olhando
 * apenas para o estado da iteração anterior (buffer duplo, como na difusão de calor):
 *   - S vira I com probabilidade 1 - (1 - beta)^k, onde k = número de vizinhas
 *     infectadas (vizinhança de Moore: as 8 células ao redor);
 *   - I vira R com probabilidade gama;
 *   - R não muda.
 *
 * Compilar:  gcc -O3 -std=c11 -Wall -o sir_seq sir_seq.c
 * Executar:  ./sir_seq N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]
 * Exemplo:   ./sir_seq 512 300 0.3 0.1 42 resultado
 */
#define _POSIX_C_SOURCE 199309L /* para clock_gettime */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { SUSCETIVEL = 0, INFECTADO = 1, RECUPERADO = 2 };

typedef struct {
    int n;                   /* lado da grade (N x N células)                 */
    int iteracoes;           /* número de iterações simuladas                  */
    double beta;             /* prob. de transmissão por vizinha infectada     */
    double gama;             /* prob. de recuperação por iteração              */
    uint64_t semente;        /* semente do gerador pseudoaleatório             */
    double prob_infeccao[9]; /* prob_infeccao[k] = 1 - (1 - beta)^k, k = 0..8  */
} Parametros;

/* ------------------------------------------------------------------------
 * Gerador pseudoaleatório SEM ESTADO, baseado em hash.
 * O sorteio depende apenas de (semente, iteração, linha, coluna), e não da
 * ordem em que as células são processadas. Por isso as versões paralelas
 * conseguem reproduzir EXATAMENTE o resultado desta versão, desde que usem
 * estas mesmas funções com as coordenadas GLOBAIS da célula (base 0).
 * ---------------------------------------------------------------------- */
static uint64_t misturar(uint64_t x) /* função de mistura do splitmix64 */
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* Número uniforme em [0, 1). */
static double aleatorio(uint64_t semente, int iteracao, int linha, int coluna)
{
    uint64_t x = misturar(semente ^ (uint64_t)iteracao);
    x = misturar(x ^ (uint64_t)linha);
    x = misturar(x ^ (uint64_t)coluna);
    return (double)(x >> 11) / 9007199254740992.0; /* 53 bits aleatórios / 2^53 */
}

/* ------------------------------------------------------------------------
 * A grade é guardada em um vetor de (N + 2) x (N + 2) bytes: em volta das
 * N x N células existe uma BORDA FANTASMA de 1 célula, sempre SUSCETIVEL e
 * nunca atualizada. Ela representa "fora do mapa" (onde não há infectados) e
 * dispensa testes de limite no laço principal. A célula (i, j) do domínio,
 * com 1 <= i, j <= N, fica na posição i * (N + 2) + j.
 * Na versão MPI, as linhas fantasmas de cima e de baixo de cada processo
 * passam a guardar as bordas recebidas dos vizinhos (as "halo cells").
 * ---------------------------------------------------------------------- */
static int vizinhas_infectadas(const unsigned char *grade, int largura, int i, int j)
{
    int k = 0;
    for (int di = -1; di <= 1; di++)
        for (int dj = -1; dj <= 1; dj++)
            if ((di != 0 || dj != 0) && grade[(size_t)(i + di) * largura + (j + dj)] == INFECTADO)
                k++;
    return k;
}

/* Uma iteração (um "superpasso" BSP): lê somente 'atual', escreve somente em
 * 'proxima' e conta quantas células ficaram em cada estado (agregação global). */
static void passo(const unsigned char *atual, unsigned char *proxima,
                  const Parametros *p, int iteracao, long long contagem[3])
{
    const int n = p->n, largura = n + 2;
    long long s = 0, inf = 0, r = 0;

    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= n; j++) {
            const size_t id = (size_t)i * largura + j;
            unsigned char estado = atual[id];

            if (estado == SUSCETIVEL) {
                const int k = vizinhas_infectadas(atual, largura, i, j);
                if (k > 0 && aleatorio(p->semente, iteracao, i - 1, j - 1) < p->prob_infeccao[k])
                    estado = INFECTADO;
            } else if (estado == INFECTADO) {
                if (aleatorio(p->semente, iteracao, i - 1, j - 1) < p->gama)
                    estado = RECUPERADO;
            }

            proxima[id] = estado;
            if (estado == SUSCETIVEL)
                s++;
            else if (estado == INFECTADO)
                inf++;
            else
                r++;
        }
    }
    contagem[SUSCETIVEL] = s;
    contagem[INFECTADO] = inf;
    contagem[RECUPERADO] = r;
}

static double agora(void) /* relógio de parede, em segundos */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* Série temporal: uma linha por iteração (a iteração 0 é o estado inicial). */
static int salvar_csv(const char *arquivo, const long long *serie, int iteracoes)
{
    FILE *f = fopen(arquivo, "w");
    if (!f)
        return -1;
    fprintf(f, "iteracao,suscetiveis,infectados,recuperados\n");
    for (int t = 0; t <= iteracoes; t++)
        fprintf(f, "%d,%lld,%lld,%lld\n", t, serie[3 * t], serie[3 * t + 1], serie[3 * t + 2]);
    return fclose(f);
}

/* Imagem do estado final (PPM colorido): S = cinza claro, I = vermelho, R = azul. */
static int salvar_ppm(const char *arquivo, const unsigned char *grade, int n)
{
    static const unsigned char cor[3][3] = {{230, 230, 230}, {200, 30, 30}, {40, 90, 200}};
    FILE *f = fopen(arquivo, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", n, n);
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= n; j++)
            fwrite(cor[grade[(size_t)i * (n + 2) + j]], 1, 3, f);
    return fclose(f);
}

static void uso(const char *programa)
{
    fprintf(stderr,
            "Uso: %s N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]\n"
            "  N              lado da grade (N x N células), N >= 1\n"
            "  ITERACOES      número de iterações, >= 0\n"
            "  BETA           prob. de transmissão por vizinha infectada, em [0, 1] (padrão: 0.3)\n"
            "  GAMA           prob. de recuperação por iteração, em [0, 1] (padrão: 0.1)\n"
            "  SEMENTE        semente do gerador pseudoaleatório (padrão: 42)\n"
            "  PREFIXO_SAIDA  se informado, grava PREFIXO_serie.csv e PREFIXO_final.ppm\n",
            programa);
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 7) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    Parametros p;
    p.n = atoi(argv[1]);
    p.iteracoes = atoi(argv[2]);
    p.beta = (argc > 3) ? atof(argv[3]) : 0.3;
    p.gama = (argc > 4) ? atof(argv[4]) : 0.1;
    p.semente = (argc > 5) ? strtoull(argv[5], NULL, 10) : 42;
    const char *prefixo = (argc > 6) ? argv[6] : NULL;

    if (p.n < 1 || p.iteracoes < 0 || p.beta < 0.0 || p.beta > 1.0 || p.gama < 0.0 || p.gama > 1.0) {
        fprintf(stderr, "Erro: parâmetros inválidos.\n");
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    /* Cada vizinha infectada transmite, de forma independente, com prob. beta:
       P(não ser infectada por nenhuma das k vizinhas) = (1 - beta)^k. */
    double escapa = 1.0;
    for (int k = 0; k <= 8; k++) {
        p.prob_infeccao[k] = 1.0 - escapa;
        escapa *= 1.0 - p.beta;
    }

    const int n = p.n, largura = n + 2;
    const size_t total = (size_t)largura * largura;
    unsigned char *atual = calloc(total, 1); /* calloc zera: tudo SUSCETIVEL, inclusive a borda */
    unsigned char *proxima = calloc(total, 1);
    long long *serie = malloc(3 * ((size_t)p.iteracoes + 1) * sizeof(long long));
    if (!atual || !proxima || !serie) {
        fprintf(stderr, "Erro: memória insuficiente para N = %d.\n", n);
        return EXIT_FAILURE;
    }

    /* Condição inicial: um único "paciente zero" no centro da grade. */
    const int centro = n / 2; /* coordenada global, base 0 */
    atual[(size_t)(centro + 1) * largura + (centro + 1)] = INFECTADO;
    serie[SUSCETIVEL] = (long long)n * n - 1;
    serie[INFECTADO] = 1;
    serie[RECUPERADO] = 0;

    /* Laço principal: somente ele é cronometrado. */
    const double inicio = agora();
    for (int t = 0; t < p.iteracoes; t++) {
        passo(atual, proxima, &p, t, &serie[3 * ((size_t)t + 1)]);
        unsigned char *troca = atual; /* o novo estado passa a ser o atual */
        atual = proxima;
        proxima = troca;
    }
    const double tempo = agora() - inicio;

    const long long *ultimo = &serie[3 * (size_t)p.iteracoes];
    printf("Simulação SIR em grade - versão sequencial\n");
    printf("Grade: %d x %d | Iterações: %d\n", n, n, p.iteracoes);
    printf("Parâmetros: beta = %g | gama = %g | semente = %llu\n", p.beta, p.gama,
           (unsigned long long)p.semente);
    printf("Estado final: S = %lld | I = %lld | R = %lld\n", ultimo[SUSCETIVEL],
           ultimo[INFECTADO], ultimo[RECUPERADO]);
    printf("Tempo da simulação (s): %.6f\n", tempo);

    int status = EXIT_SUCCESS;
    if (prefixo) {
        char arquivo[1024];
        snprintf(arquivo, sizeof arquivo, "%s_serie.csv", prefixo);
        if (salvar_csv(arquivo, serie, p.iteracoes) == 0) {
            printf("Arquivo gerado: %s\n", arquivo);
        } else {
            fprintf(stderr, "Erro ao gravar %s\n", arquivo);
            status = EXIT_FAILURE;
        }
        snprintf(arquivo, sizeof arquivo, "%s_final.ppm", prefixo);
        if (salvar_ppm(arquivo, atual, n) == 0) {
            printf("Arquivo gerado: %s\n", arquivo);
        } else {
            fprintf(stderr, "Erro ao gravar %s\n", arquivo);
            status = EXIT_FAILURE;
        }
    }

    free(atual);
    free(proxima);
    free(serie);
    return status;
}
