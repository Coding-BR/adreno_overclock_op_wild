# RM11 Pro Adreno 750 Overclock Standalone Compiler

Este repositório contém o driver compilável out-of-tree para Overclock (1250MHz @ 1.075V) e Blindagem Térmica da GPU Adreno 750 no RedMagic 11 Pro (NX809J).

Projetado especificamente para rodar no kernel **OP-WILD** (OxygenOS 16 / Android 16 / 6.12).

## Como Funciona o Build
O fluxo de trabalho do GitHub Actions (`.github/workflows/compile.yml`) realiza uma compilação ultra-rápida (menos de 2 minutos) do módulo driver sem precisar compilar o kernel completo:
1. Faz o download do compilador oficial Android Clang (`r547379`).
2. Clona a árvore de código aberto da OnePlus 15 (`android_kernel_common_oneplus_sm8850`).
3. Configura a árvore do kernel e copia o arquivo `Module.symvers` personalizado da pasta `symvers/`.
4. Compila o driver `adreno_overclock.ko` e empacota-o em um arquivo ZIP compatível com **KernelSU** e **KernelSU-Next**.

## Como Usar
1. Obtenha o arquivo `Module.symvers` do seu kernel **OP-WILD** ativo:
   - Execute a pipeline do seu repositório `OnePlus_KernelSU_SUSFS` com a opção **`debug`** marcada como **`true`**.
   - Baixe o artefato `debug_OP15_OOS16.zip` resultante e extraia o arquivo `Module.symvers`.
2. Cole o arquivo `Module.symvers` na pasta `symvers/` deste repositório.
3. Faça commit e envie para a branch `main` do GitHub.
4. O GitHub Actions iniciará automaticamente a compilação do módulo `adreno_overclock.ko` com os hashes corretos de símbolos.
5. Baixe o artefato `rm11pro_gpu_oc_1250mhz.zip` gerado nas Actions e instale-o como um módulo no aplicativo KernelSU.
