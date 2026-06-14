#!/system/bin/sh
# Red Magic 11 Pro (NX809J) - post-fs-data script para KernelSU
MODDIR=${0%/*}

# Ajustes iniciais de permissão e preparação
chmod 755 "$MODDIR/service.sh" 2>/dev/null
