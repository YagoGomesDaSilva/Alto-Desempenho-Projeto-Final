# 2. Modelo SIR em grade e algoritmo

Este documento define o modelo e as regras que **todas as versões** devem seguir. Como todas fazem exatamente o mesmo cálculo, todas devem produzir exatamente a mesma saída para a mesma entrada. É isso que permite validar as versões paralelas comparando-as com a sequencial.

## 2.1 O problema

Da ata da reunião de 16/10/2026: um simulador de propagação que combina características de modelos de propagação de incêndio e de difusão de temperatura, inspirado no jogo *Plague Inc.* O domínio é uma grade de regiões; a cada iteração, o estado de cada célula (suscetível, infectada ou recuperada) é atualizado de acordo com o estado das vizinhas e com parâmetros de transmissão.

Como as ideias da ata aparecem no modelo, mantido o mais simples possível:

| Ideia da ata | Como aparece no modelo |
|---|---|
| Propagação de incêndio | uma célula infectada "queima" e contamina as vizinhas com certa probabilidade; depois "apaga" (se recupera). S = árvore, I = em chamas, R = queimada. |
| Difusão de temperatura | o padrão de computação é o mesmo (*stencil*): cada célula é atualizada a partir das vizinhas na iteração anterior, usando dois buffers. |
| Proximidade (DBSCAN) | a vizinhança são as células a até 1 célula de distância (as 8 ao redor, vizinhança de Moore); a infecção se expande de vizinho em vizinho, como a expansão de um *cluster* no DBSCAN. |
| Dados de COVID-19 | ainda em avaliação pela equipe. `BETA` e `GAMA` são parâmetros de linha de comando e poderiam ser calibrados com esses dados, mas isso não faz parte desta implementação. |

Não estão no modelo: a movimentação de pessoas em ambientes fechados e o algoritmo DBSCAN em si. A ata os cita como referências e possíveis extensões.

## 2.2 Regras

- Grade de N × N células. Coordenadas globais começam em 0: linha e coluna vão de 0 a N − 1.
- Estado inicial: todas as células suscetíveis, exceto o **paciente zero** na linha `N/2`, coluna `N/2` (divisão inteira).
- A iteração `t` (t = 0, 1, …, ITERACOES − 1) calcula o estado `t + 1` a partir do estado `t`, para todas as células ao mesmo tempo:

| Estado em `t` | Condição | Estado em `t + 1` |
|---|---|---|
| S | `u < 1 − (1 − β)^k`, onde `k` = infectadas entre as 8 vizinhas | I |
| I | `u < γ` | R |
| R | (nunca muda) | R |

- `u` é um número pseudoaleatório em [0, 1) sorteado para aquela célula naquela iteração (ver 2.4).
- `1 − (1 − β)^k` é a probabilidade de pelo menos uma das `k` vizinhas infectadas transmitir, se cada uma transmite de forma independente com probabilidade β.
- Fora da grade não há infectados (borda fixa).
- Parâmetros: β (`BETA`, padrão 0.3), γ (`GAMA`, padrão 0.1; a infecção dura em média 1/γ = 10 iterações) e a semente (`SEMENTE`, padrão 42).

## 2.3 Estrutura BSP

Cada iteração é um **superpasso** BSP:

```
inicialização (grade + paciente zero)
para t = 0 .. ITERACOES-1:                         <- superpasso
    1. computação local: cada célula calcula seu novo estado lendo só a grade "atual"
    2. comunicação: (versões paralelas) troca das bordas entre subdomínios ("halo")
    3. agregação global: soma de S, I e R (redução)
    4. sincronização e troca de buffers: "próxima" passa a ser "atual"
fim: tempo, contagens finais e arquivos de saída
```

Na versão sequencial a etapa 2 não existe: a grade inteira está na mesma memória, e a borda fantasma (2.5) faz o papel do halo.

## 2.4 Aleatoriedade reprodutível

O sorteio de cada célula é `u = aleatorio(semente, t, linha, coluna)`, uma função **sem estado** baseada no misturador do *splitmix64*, com `linha` e `coluna` **globais** (base 0). Como o resultado não depende da ordem em que as células são processadas, a divisão do trabalho entre processos, threads ou blocos de GPU não altera a simulação.

Regras para as versões paralelas:

1. Copie `misturar()` e `aleatorio()` de `sequencial/sir_seq.c` sem alterações. Em CUDA, marque-as com `__host__ __device__`; em OpenMP para GPU, coloque-as entre `#pragma omp declare target` e `#pragma omp end declare target`.
2. Use as coordenadas **globais** da célula. No MPI, a linha global é `início do bloco do processo + linha local − 1` (ver docs/04).
3. Use as mesmas comparações: `u < prob_infeccao[k]` e `u < gama` (menor estrito).
4. Calcule a tabela `prob_infeccao[0..8]` na CPU, do mesmo jeito que a versão sequencial, e envie-a pronta para a GPU. Não a recalcule com `pow` na GPU: diferenças mínimas de arredondamento poderiam mudar sorteios raros.

## 2.5 Representação da grade

- Vetor de `(N + 2) × (N + 2)` bytes (`unsigned char`): 0 = S, 1 = I, 2 = R.
- Em volta das N × N células há uma **borda fantasma** de 1 célula, sempre S e nunca atualizada. Ela representa "fora do mapa" e evita testes de limite no laço principal.
- A célula da linha global `l` e coluna global `c` fica na posição `(l + 1) × (N + 2) + (c + 1)`.
- Dois buffers (`atual` e `proxima`): cada iteração lê de um e escreve no outro, e depois os ponteiros são trocados.
- Memória: cerca de `2 × (N + 2)²` bytes. N = 8192 ocupa ~134 MB; N = 16384, ~537 MB; N = 32768, ~2,1 GB (acima do padrão de 2000 MB por CPU: peça mais com `--mem`).

## 2.6 Custo computacional

- Tempo proporcional a `N² × ITERACOES`.
- O trabalho por célula depende do estado. Uma célula S sem vizinhas infectadas só lê as 8 vizinhas; as que precisam de sorteio (S na frente de infecção e I) também calculam 3 funções de mistura; R só é copiada.
- Com um único foco no centro, a região "cara" é a frente de infecção, um anel que cresce cerca de 1 célula por iteração. Na versão MPI com divisão por linhas, os processos com as linhas centrais tendem a ter mais trabalho: um desbalanceamento de carga que vale medir e discutir.

## 2.7 Saídas (contrato comum)

- Saída padrão: entre outras, as linhas `Estado final: S = … | I = … | R = …` e `Tempo da simulação (s): …`.
- `PREFIXO_serie.csv`: uma linha por iteração (a 0 é o estado inicial), com cabeçalho `iteracao,suscetiveis,infectados,recuperados`.
- `PREFIXO_final.ppm`: imagem do estado final (PPM binário, 1 pixel por célula): S em cinza claro, I em vermelho, R em azul.
- O tempo medido é só o do laço principal (iterações), sem alocação, inicialização ou gravação de arquivos.
- Como a simulação é determinística, rodar com `ITERACOES` = 100, 200, 300… gera quadros da mesma simulação (útil para montar uma animação).
