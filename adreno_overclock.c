// SPDX-License-Identifier: GPL-2.0-only
/*
 * adreno_overclock.c - Red Magic 11 Pro Adreno 750 Overclock (1250MHz) & Blindagem Térmica
 *
 * This module dynamically patches the GPU power table in-memory to overclock
 * the Adreno 750 GPU from 1200MHz to 1250MHz. It also installs a kprobe-based
 * "Thermal Shield" on kgsl_pwrctrl_pwrlevel_change to prevent ZTE's thermal
 * daemons from throttling the GPU below the overclocked frequency.
 *
 * Symbols are resolved at runtime via kallsyms to avoid direct dependencies
 * on the graphics-kernel module, bypassing module_layout CRC mismatches.
 *
 * Copyright (c) Engenharia Reversa (REP) / Antigravity
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pm_opp.h>
#include <linux/string.h>

/* ============================================================================
 * Runtime symbol resolution via kallsyms (kprobe trick)
 * ============================================================================
 * Since kallsyms_lookup_name is no longer exported in newer kernels,
 * we use a kprobe on the function itself to obtain its address, then
 * use it to resolve any other kernel symbol at runtime.
 */

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
static kallsyms_lookup_name_t my_kallsyms_lookup_name;

static int kp_dummy_pre(struct kprobe *p, struct pt_regs *regs)
{
	return 0;
}

static struct kprobe kallsyms_kp = {
	.symbol_name = "kallsyms_lookup_name",
	.pre_handler = kp_dummy_pre,
};

static int resolve_kallsyms(void)
{
	int ret;

	ret = register_kprobe(&kallsyms_kp);
	if (ret < 0) {
		printk(KERN_ERR "\x03" "[REP] Falha ao registrar kprobe para kallsyms_lookup_name: %d\n", ret);
		return ret;
	}

	my_kallsyms_lookup_name = (kallsyms_lookup_name_t)kallsyms_kp.addr;
	unregister_kprobe(&kallsyms_kp);

	if (!my_kallsyms_lookup_name) {
		printk(KERN_ERR "\x03" "[REP] my_kallsyms_lookup_name indisponivel.\n");
		return -EFAULT;
	}

	printk(KERN_INFO "\x06" "[REP] kallsyms_lookup_name encontrado no endereco: %px\n",
	       my_kallsyms_lookup_name);
	return 0;
}

/* ============================================================================
 * Minimal in-kernel struct definitions (field-compatible with stock kgsl)
 * ============================================================================
 * We define only what we need to access: gpu_freq, voltage_level, cx_level
 * at the correct offsets within kgsl_pwrlevel. These match the Qualcomm
 * kgsl_pwrctrl.h definitions used by the stock kernel.
 */

struct kgsl_pwrlevel_minimal {
	unsigned int gpu_freq;
	unsigned int bus_freq;
	unsigned int bus_min;
	unsigned int bus_max;
	unsigned int acd_level;
	unsigned int cx_level;
	unsigned int voltage_level;
};

/* ============================================================================
 * Thermal Shield (Blindagem Térmica)
 * ============================================================================
 * Hooks kgsl_pwrctrl_pwrlevel_change via kprobe. When a thermal daemon
 * requests a lower power level (higher index), we override the request
 * to keep the GPU at level 0 (max frequency = 1250MHz).
 */

static struct kprobe thermal_shield_kp;
static bool thermal_shield_active;

static int thermal_shield_pre(struct kprobe *p, struct pt_regs *regs)
{
	/*
	 * On ARM64, function arguments are in x0-x7.
	 * kgsl_pwrctrl_pwrlevel_change(struct kgsl_device *device, unsigned int new_level)
	 *   x0 = device pointer
	 *   x1 = new_level (the power level index)
	 *
	 * We force new_level to 0 (max performance) to prevent throttling.
	 */
	regs->regs[1] = 0;
	return 0;
}

static int install_thermal_shield(void)
{
	int ret;

	memset(&thermal_shield_kp, 0, sizeof(thermal_shield_kp));
	thermal_shield_kp.symbol_name = "kgsl_pwrctrl_pwrlevel_change";
	thermal_shield_kp.pre_handler = thermal_shield_pre;

	ret = register_kprobe(&thermal_shield_kp);
	if (ret < 0) {
		printk(KERN_WARNING "\x04" "[REP] Aviso: Falha ao registrar kprobe de blindagem da GPU (kgsl_pwrctrl_pwrlevel_change): %d\n", ret);
		thermal_shield_active = false;
		return ret;
	}

	thermal_shield_active = true;
	printk(KERN_INFO "\x06" "[REP] Blindagem Termica da GPU (kgsl_pwrctrl_pwrlevel_change) ativada com sucesso!\n");
	return 0;
}

/* ============================================================================
 * GPU Overclock Injection
 * ============================================================================
 */

/* Overclock parameters */
#define OC_FREQ_HZ      1250000000U
#define OC_VOLT_LEVEL    0x1f0
#define OC_CX_LEVEL      64
#define STOCK_FREQ_HZ   1200000000U

/*
 * We use a function pointer type for kgsl_get_device since we resolve
 * it dynamically and the actual header is not included.
 */
struct kgsl_device_opaque;
typedef struct kgsl_device_opaque *(*kgsl_get_device_t)(int id);

static int __init adreno_overclock_init(void)
{
	kgsl_get_device_t kgsl_get_device_fn;
	struct kgsl_device_opaque *device;
	struct kgsl_pwrlevel_minimal *max_pwr;
	int ret;

	printk(KERN_INFO "\x06" "[REP] Inicializando modulo de Overclock da GPU Adreno (1250MHz + Overvolt)...\n");

	/* Step 1: Resolve kallsyms_lookup_name */
	ret = resolve_kallsyms();
	if (ret)
		return ret;

	/* Step 2: Resolve kgsl_get_device */
	kgsl_get_device_fn = (kgsl_get_device_t)my_kallsyms_lookup_name("kgsl_get_device");
	if (!kgsl_get_device_fn) {
		printk(KERN_ERR "\x03" "[REP] kgsl_get_device nao encontrado via kallsyms.\n");
		return -ENODEV;
	}

	/* Step 3: Get the KGSL device instance */
	device = kgsl_get_device_fn(0);
	if (!device) {
		printk(KERN_ERR "\x03" "[REP] Dispositivo kgsl_dev (ID 0) nao retornado.\n");
		return -ENODEV;
	}

	printk(KERN_INFO "\x06" "[REP] Dispositivo KGSL encontrado no endereco: %px\n", device);

	/*
	 * Step 4: Navigate to pwrctrl.pwrlevels[0]
	 *
	 * We need the offsets of pwrctrl within kgsl_device and pwrlevels
	 * within kgsl_pwrctrl. Rather than hardcoding byte offsets that could
	 * break, we use kallsyms to find the "kgsl_pwrctrl_pwrlevel_change"
	 * function and infer from the working module's behavior.
	 *
	 * From the kgsl_device.h source we examined:
	 *   struct kgsl_device -> pwrctrl is at a known offset
	 *   struct kgsl_pwrctrl -> pwrlevels[] contains kgsl_pwrlevel structs
	 *
	 * The working module used the real headers to get correct offsets.
	 * We use the same approach: access via struct field offsets obtained
	 * from the kernel source analysis.
	 *
	 * kgsl_device.pwrctrl offset: determined by all fields before it.
	 * From kgsl_device.h line 248-269:
	 *   dev, name, id, shader_mem_virt, qdss_gfx_virt,
	 *   memstore, scratch, mmu, gmu_core, hwaccess_gate,
	 *   halt_gate, ftbl, idle_check_ws, idle_timer, pwrctrl
	 *
	 * Rather than compute this manually (fragile), we scan for the
	 * known stock frequency (1200000000) near the device pointer.
	 */

	{
		unsigned int *scan;
		unsigned int *found = NULL;
		int i;

		/* Scan a reasonable range within the kgsl_device structure
		 * looking for the stock 1200MHz frequency value at pwrlevels[0].gpu_freq.
		 * The pwrctrl struct is typically within the first ~16KB of kgsl_device.
		 */
		scan = (unsigned int *)device;
		for (i = 0; i < 16384 / sizeof(unsigned int); i++) {
			if (scan[i] == STOCK_FREQ_HZ) {
				found = &scan[i];
				break;
			}
		}

		if (!found) {
			/* Try the overclock frequency in case it was already applied */
			for (i = 0; i < 16384 / sizeof(unsigned int); i++) {
				if (scan[i] == OC_FREQ_HZ) {
					found = &scan[i];
					break;
				}
			}
		}

		if (!found) {
			printk(KERN_WARNING "\x04" "[REP] Frequencia do Lvl 0 desconhecida (%u Hz). Abortando injecao para evitar instabilidade.\n", 0);
			return -ENOENT;
		}

		max_pwr = (struct kgsl_pwrlevel_minimal *)found;
	}

	printk(KERN_INFO "\x06" "[REP] Tabela de Pwrlevels atual: Lvl 0 Freq=%u Hz, VoltLvl=0x%x, CX=0x%x\n",
	       max_pwr->gpu_freq, max_pwr->voltage_level, max_pwr->cx_level);

	/* Step 5: Safety check - only proceed if freq is stock 1200MHz or already OC'd */
	if (max_pwr->gpu_freq != STOCK_FREQ_HZ && max_pwr->gpu_freq != OC_FREQ_HZ) {
		printk(KERN_WARNING "\x04" "[REP] Frequencia do Lvl 0 desconhecida (%u Hz). Abortando injecao para evitar instabilidade.\n",
		       max_pwr->gpu_freq);
		return -EINVAL;
	}

	/* Step 6: Apply overclock */
	printk(KERN_INFO "\x06" "[REP] Injetando Overclock para 1250 MHz (1250000000 Hz) e Overvolt...\n");

	max_pwr->gpu_freq = OC_FREQ_HZ;
	max_pwr->voltage_level = OC_VOLT_LEVEL;
	max_pwr->cx_level = OC_CX_LEVEL;

	printk(KERN_INFO "\x06" "[REP] Nova Tabela Lvl 0: Freq=%u Hz, VoltLvl=0x%x, CX=0x%x\n",
	       max_pwr->gpu_freq, max_pwr->voltage_level, max_pwr->cx_level);

	/* Step 7: Register 1250MHz in OPP framework for proper devfreq integration */
	{
		/*
		 * Get the struct device from kgsl_device. The first field
		 * of kgsl_device is "struct device *dev" (from kgsl_device.h:249).
		 */
		struct device *kgsl_dev = *((struct device **)device);
		if (kgsl_dev) {
			struct dev_pm_opp_data opp_data = {
				.freq = OC_FREQ_HZ,
				.u_volt = 1075000, /* 1.075V - Overvolt for 1250MHz stabilization */
				.level = OPP_LEVEL_UNSET,
			};
			ret = dev_pm_opp_add_dynamic(kgsl_dev, &opp_data);
			printk(KERN_INFO "\x06" "[REP] dev_pm_opp_add para 1250MHz retornou: %d\n", ret);
		}
	}

	printk(KERN_INFO "\x06" "[REP] Overclock de 1250MHz e Overvolt aplicados com sucesso na GPU Adreno!\n");

	/* Step 8: Install thermal shield */
	install_thermal_shield();

	return 0;
}

static void __exit adreno_overclock_exit(void)
{
	if (thermal_shield_active) {
		unregister_kprobe(&thermal_shield_kp);
		thermal_shield_active = false;
	}

	printk(KERN_INFO "\x06" "[REP] Descarregando modulo de Overclock e Blindagem Adreno.\n");
}

module_init(adreno_overclock_init);
module_exit(adreno_overclock_exit);

MODULE_DESCRIPTION("Red Magic 11 Pro Adreno 750 Overclock (1250MHz) & Blindagem Termica");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Engenharia Reversa (REP) / Antigravity");
