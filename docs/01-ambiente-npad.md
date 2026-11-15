# 1. Preparação do ambiente no NPAD

Resumo, a partir dos tutoriais oficiais do NPAD ([github.com/NPAD-UFRN/Tutorials](https://github.com/NPAD-UFRN/Tutorials)), do que é preciso para compilar e executar o projeto no supercomputador da UFRN. Vale para todas as versões.

## 1.1 Pré-requisitos

- Conta no NPAD, criada na [página de cadastro](http://npad.ufrn.br/primeirospassos.php) com uma chave pública SSH do tipo RSA.
- Cliente SSH. O Windows 11 já traz o OpenSSH (use o Windows Terminal ou o PowerShell); alternativas: MobaXterm ou PuTTY. No Linux: pacote `openssh-client`.
- Uma forma de copiar arquivos: `scp` (Windows 11 e Linux), `rsync` (Linux), WinSCP ou MobaXterm.
- `git` (opcional), para clonar o repositório direto no NPAD.

## 1.2 Chave SSH (somente na primeira vez)

```bash
ssh-keygen -t rsa          # pressione Enter em todas as perguntas
cat ~/.ssh/id_rsa.pub      # Linux, macOS ou Git Bash: mostra a chave pública
```

No Windows (PowerShell, na pasta do usuário): `type .ssh\id_rsa.pub`. A chave pública é colada no cadastro do NPAD. O acesso só funciona a partir do computador que tem a chave privada correspondente (`~/.ssh/id_rsa`).

## 1.3 Acesso ao supercomputador

Os dados de acesso chegam por e-mail na criação da conta: servidor `sc2.npad.ufrn.br`, porta `4422` e o seu nome de usuário.

```bash
ssh -p4422 SEU_USUARIO@sc2.npad.ufrn.br
```

Para não repetir porta e usuário, crie o arquivo `~/.ssh/config` (no Windows: `C:\Users\<você>\.ssh\config`):

```
Host npad
  HostName sc2.npad.ufrn.br
  Port 4422
  User SEU_USUARIO
```

Depois disso, basta `ssh npad` (e `scp arquivo npad:~/` para copiar).

Observações: usuários não têm permissão de `sudo`; a pasta pessoal (`~`) é vista por todos os nós; `~/scratch` é um armazenamento temporário (arquivos antigos podem ser apagados pela equipe do NPAD).

## 1.4 Levar o código para o NPAD

**Opção A, com git (no nó de login):**

```bash
git --version      # confirme que o git está disponível
git clone https://github.com/YagoGomesDaSilva/Alto-Desempenho-Projeto-Final.git
cd Alto-Desempenho-Projeto-Final
git pull           # depois, para atualizar
```

Se o repositório for privado, o GitHub pede autenticação (um *personal access token* no lugar da senha).

**Opção B, com scp ou rsync (no seu computador):**

```bash
scp -P4422 -r Alto-Desempenho-Projeto-Final SEU_USUARIO@sc2.npad.ufrn.br:~/
rsync -aP --rsh='ssh -p4422' Alto-Desempenho-Projeto-Final SEU_USUARIO@sc2.npad.ufrn.br:~/
```

Atenção: no `scp` a porta é `-P` (maiúsculo); no `ssh`, `-p` (minúsculo).

> **Arquivos vindos do Windows.** Scripts `.sh` com quebra de linha CRLF falham no Linux (`/bin/bash^M: bad interpreter`), e o `sbatch` recusa scripts com "DOS line breaks". O `.gitattributes` do repositório força LF nos `.sh`. Se copiar arquivos por outro meio, converta com `sed -i 's/\r$//' arquivo.sh`.

## 1.5 Nó de login × nós de computação

- **Nó de login** (`service0`, 2× Intel Xeon Silver 4114): serve para editar, compilar e fazer testes curtos. O limite é de 30 minutos de uso de 1 núcleo a 100%; um processo que use 10 núcleos é encerrado em 3 minutos. Não tem GPU.
- **Nós de computação**: onde os experimentos devem rodar, sempre por meio do Slurm (`sbatch`).
- O processador do nó de login é diferente do processador dos nós de computação (ex.: AMD EPYC na partição `amd-512`). **Não compile com `-march=native` no nó de login**: o executável pode usar instruções que o nó de computação não tem. Compile sem essa opção (como neste projeto) ou compile dentro do próprio job.

## 1.6 Módulos (compiladores e bibliotecas)

```bash
module avail                 # lista os módulos disponíveis
module load <módulo>         # carrega um módulo
module list                  # mostra os módulos carregados
```

Dentro de um script de job, carregue os módulos antes de executar o programa.

Módulos relevantes para o projeto. Os nomes foram vistos na saída de `module avail` mostrada na apresentação do PaScal Suite (outubro de 2026) e, no caso do CUDA, no material de softwares do NPAD. **Confirme com `module avail` antes de usar**, pois podem mudar:

| Uso | Módulo |
|---|---|
| GCC (versões de CPU) | `compilers/gnu/14.2.0` |
| Open MPI | `libraries/openmpi/5.0.10-gnu14-ucxmt` |
| CUDA (`nvcc`) | `compilers/nvidia/cuda/12.6` |
| NVIDIA HPC SDK (`nvc`, OpenMP em GPU) | `compilers/nvidia/nvhpc/24.11` |
| PaScal Suite (análise de escalabilidade) | `softwares/pascalsuite/2025-07-08` |

## 1.7 Partições

| Partição | Nós | Hardware de cada nó | Uso no projeto |
|---|---|---|---|
| `amd-512` | 56 | 2× AMD EPYC 7713 (128 núcleos), 512 GB | sequencial e MPI + OpenMP |
| `intel-128` | 55 | 2× Intel Xeon E5-2698v3 (16 núcleos cada), 128 GB | alternativa de CPU |
| `gpu-4-a100` | 1 | 2× AMD EPYC 7713 + 4× NVIDIA A100 80 GB | OpenMP em GPU e CUDA |
| `gpu-8-v100` | 2 | 2× Intel Xeon E5-2683 v4 + 8× NVIDIA V100 16 GB | alternativa de GPU |
| `gpu-8-h100` | 1 | 2× AMD EPYC 9654 + 8× NVIDIA H100 80 GB | alternativa de GPU |
| `gpu-4-h200` | 2 | 2× Intel Xeon Gold 6548N + 4× NVIDIA H200 141 GB | alternativa de GPU |

Para comparações justas, rode **todas as medições de CPU na mesma partição** (sugestão: `amd-512`) e **todas as de GPU na mesma partição** (sugestão: `gpu-4-a100`).

## 1.8 Slurm: o essencial

Esqueleto de um script de job:

```bash
#!/bin/bash
#SBATCH --job-name=nome           # nome que aparece na fila
#SBATCH --partition=amd-512       # partição
#SBATCH --time=0-0:30             # tempo máximo (dias-horas:minutos)
#SBATCH --output=saida-%j.out     # %j = número do job
#SBATCH --error=erro-%j.err

module load compilers/gnu/14.2.0
./programa argumentos
```

Diretivas úteis:

| Diretiva | Para quê |
|---|---|
| `--cpus-per-task=T` | T núcleos por processo (threads OpenMP); use junto `export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK` |
| `--ntasks=P`, `--nodes=K`, `--ntasks-per-node=Q` | número de processos MPI e de nós |
| `--hint=compute_bound` | uma thread por núcleo físico (sem ela, o Slurm conta cada núcleo como 2 processadores) |
| `--exclusive` | nó inteiro para o job: medições de tempo sem interferência de outros jobs (por padrão os nós são compartilhados) |
| `--gpus-per-node=G` | número de GPUs |
| `--mem=4G`, `--mem-per-cpu=MB` | memória; na `amd-512` o padrão é 2000 MB por CPU (máximo 4000); na `gpu-4-a100`, 2000 MB por CPU |

Comandos:

```bash
sbatch job.sh            # submete; responde "Submitted batch job 123456"
squeue -u $USER          # acompanha (PD = pendente, R = executando)
scancel 123456           # cancela o job
cat saida-123456.out     # saída do programa
sinfo                    # estado das partições
```

Escolha um `--time` realista: jobs com tempo menor podem começar antes (*backfill*). Jobs de usuários comuns podem ser interrompidos (preempção) depois de 6 horas nas partições de GPU ou de 2 dias nas demais, o que não deve afetar este projeto.

## 1.9 Trazer os resultados para o seu computador

No seu computador (não no NPAD):

```bash
scp -P4422 -r SEU_USUARIO@sc2.npad.ufrn.br:~/Alto-Desempenho-Projeto-Final/sequencial/resultados .
```

Também é possível usar o WinSCP ou o navegador de arquivos do MobaXterm.
