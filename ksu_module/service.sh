#!/system/bin/sh
# Red Magic 11 Pro (NX809J) - GPU Overclock Service Script (KernelSU)
# Executado no late_start (boot completo)

MODDIR=${0%/*}

echo "[REP] Iniciando servico de Overclock da GPU Adreno (1250MHz @ 1.075V)..." > /dev/kmsg

# 1. Carregar o módulo de kernel customizado para injeção da tabela de 1250MHz e Overvolt em memória
if [ -f "$MODDIR/adreno_overclock.ko" ]; then
    insmod "$MODDIR/adreno_overclock.ko"
    echo "[REP] Módulo adreno_overclock.ko carregado com sucesso." > /dev/kmsg
else
    echo "[REP] AVISO: adreno_overclock.ko nao encontrado no diretorio do modulo." > /dev/kmsg
fi

# 2. Desativar daemons proprietários da ZTE de Thermal e Performance
stop com.zte.thermald
stop perfservice
stop thermal-engine-v2
stop thermald

# Garantindo que os processos residuais sejam encerrados
for pid in $(pidof com.zte.thermald perfservice thermal-engine-v2 thermald); do
    kill -9 $pid 2>/dev/null
done

# 3. Configurar os parâmetros do sysfs do Adreno (KGSL) de forma segura
KGSL_PATH="/sys/class/kgsl/kgsl-3d0"

if [ -d "$KGSL_PATH" ]; then
    # Definir o teto de limitação (0 = máxima performance)
    echo 0 > "$KGSL_PATH/max_pwrlevel" 2>/dev/null
    echo "[REP] GPU Adreno configurada para max_pwrlevel 0." > /dev/kmsg
fi

# 4. Travar as permissões do sysfs para impedir que daemons da ZTE modifiquem o valor
chmod 444 "$KGSL_PATH/max_pwrlevel" 2>/dev/null

echo "[REP] Overclock de 1250MHz e Overvolt aplicados com sucesso via KernelSU." > /dev/kmsg
