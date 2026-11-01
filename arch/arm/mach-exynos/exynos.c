// SPDX-License-Identifier: GPL-2.0
//
// Samsung Exynos Flattened Device Tree enabled machine
//
// Copyright (c) 2010-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/tick.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

#define S3C_ADDR_BASE	0xF6000000
#define S3C_ADDR(x)	((void __iomem __force *)S3C_ADDR_BASE + (x))
#define S5P_VA_CHIPID	S3C_ADDR(0x02000000)

static struct platform_device exynos_cpuidle = {
	.name              = "exynos_cpuidle",
#ifdef CONFIG_ARM_EXYNOS_CPUIDLE
	.dev.platform_data = exynos_enter_aftr,
#endif
	.id                = -1,
};

void __iomem *sysram_base_addr __ro_after_init;
phys_addr_t sysram_base_phys __ro_after_init;
void __iomem *sysram_ns_base_addr __ro_after_init;

unsigned long exynos_cpu_id;
static unsigned int exynos_cpu_rev;

unsigned int exynos_rev(void)
{
	return exynos_cpu_rev;
}

void __init exynos_sysram_init(void)
{
	struct device_node *node;

	for_each_compatible_node(node, NULL, "samsung,exynos4210-sysram") {
		struct resource res;
		if (!of_device_is_available(node))
			continue;

		of_address_to_resource(node, 0, &res);
		sysram_base_addr = ioremap(res.start, resource_size(&res));
		sysram_base_phys = res.start;
		of_node_put(node);
		break;
	}

	for_each_compatible_node(node, NULL, "samsung,exynos4210-sysram-ns") {
		if (!of_device_is_available(node))
			continue;
		sysram_ns_base_addr = of_iomap(node, 0);
		of_node_put(node);
		break;
	}
}

static int __init exynos_fdt_map_chipid(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	const __be32 *reg;
	int len;

	if (!of_flat_dt_is_compatible(node, "samsung,exynos4210-chipid"))
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}

static void __init exynos_init_io(void)
{
	debug_ll_io_init();

	of_scan_flat_dt(exynos_fdt_map_chipid, NULL);

	/* detect cpu id and rev. */
	exynos_cpu_id = readl_relaxed(S5P_VA_CHIPID);
	exynos_cpu_rev = exynos_cpu_id & 0xFF;

	pr_info("Samsung CPU ID: 0x%08lx\n", exynos_cpu_id);

}

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option. Used by smp code
 * and suspend.
 *
 * This is necessary only on Exynos4 SoCs. When system is running
 * USE_DELAYED_RESET_ASSERTION should be set so the ARM CLK clock down
 * feature could properly detect global idle state when secondary CPU is
 * powered down.
 *
 * However this should not be set when such system is going into suspend.
 */
void exynos_set_delayed_reset_assertion(bool enable)
{
	if (of_machine_is_compatible("samsung,exynos4")) {
		unsigned int tmp, core_id;

		for (core_id = 0; core_id < num_possible_cpus(); core_id++) {
			tmp = pmu_raw_readl(EXYNOS_ARM_CORE_OPTION(core_id));
			if (enable)
				tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
			else
				tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
			pmu_raw_writel(tmp, EXYNOS_ARM_CORE_OPTION(core_id));
		}
	}
}

/*
 * Apparently, these SoCs are not able to wake-up from suspend using
 * the PMU. Too bad. Should they suddenly become capable of such a
 * feat, the matches below should be moved to suspend.c.
 */
static const struct of_device_id exynos_dt_pmu_match[] = {
	{ .compatible = "samsung,exynos5260-pmu" },
	{ .compatible = "samsung,exynos5410-pmu" },
	{ /*sentinel*/ },
};

static void exynos_map_pmu(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, exynos_dt_pmu_match);
	if (np)
		pmu_base_addr = of_iomap(np, 0);
	of_node_put(np);
}

static void __init exynos_init_irq(void)
{
	irqchip_init();
	/*
	 * Since platsmp.c needs pmu base address by the time
	 * DT is not unflatten so we can't use DT APIs before
	 * init_irq
	 */
	exynos_map_pmu();
}

static void __init exynos_dt_machine_init(void)
{
	/*
	 * This is called from smp_prepare_cpus if we've built for SMP, but
	 * we still need to set it up for PM and firmware ops if not.
	 */
	if (!IS_ENABLED(CONFIG_SMP))
		exynos_sysram_init();

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYNOS_CPUIDLE)
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos3250"))
		exynos_cpuidle.dev.platform_data = &cpuidle_coupled_exynos_data;
#endif
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos4212") ||
	    (of_machine_is_compatible("samsung,exynos4412") &&
	     (of_machine_is_compatible("samsung,trats2") ||
		  of_machine_is_compatible("samsung,midas") ||
		  of_machine_is_compatible("samsung,p4note"))) ||
	    of_machine_is_compatible("samsung,exynos3250") ||
	    of_machine_is_compatible("samsung,exynos5250"))
		platform_device_register(&exynos_cpuidle);
}

static char const *const exynos_dt_compat[] __initconst = {
	"samsung,exynos3",
	"samsung,exynos3250",
	"samsung,exynos4",
	"samsung,exynos4210",
	"samsung,exynos4212",
	"samsung,exynos4412",
	"samsung,exynos5",
	"samsung,exynos5250",
	"samsung,exynos5260",
	"samsung,exynos5420",
	"samsung,exynos5433",
	NULL
};

static void __init exynos_dt_fixup(void)
{
	/*
	 * Some versions of uboot pass garbage entries in the memory node,
	 * use the old CONFIG_ARM_NR_BANKS
	 */
	of_fdt_limit_memory(8);
}


/* ---- M1 bring-up diagnostics: is the GIC / arch timer / MCT usable from NS SVC? ---- */
static inline u64 m1_cntvct(void)
{
	u32 lo, hi;

	asm volatile("mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi));
	return ((u64)hi << 32) | lo;
}

static void __init m1_wait_us(unsigned int us)
{
	/* crude busy loop: the ARM arch counter turned out not to run */
	volatile unsigned int i;

	for (i = 0; i < us * 200; i++)
		;
}

static void __init m1_diag(void)
{
	void __iomem *gd = ioremap(0x11001000, 0x1000);
	void __iomem *gc = ioremap(0x11002000, 0x2000);
	void __iomem *mct = ioremap(0x101c0000, 0x800);
	u32 frq, a, b;
	u64 v0, v1;

	pr_info("M1DIAG gicd ctlr=%08x typer=%08x iidr=%08x igroupr0=%08x isenabler0=%08x\n",
		readl(gd), readl(gd + 4), readl(gd + 8), readl(gd + 0x80), readl(gd + 0x100));
	pr_info("M1DIAG gicc ctlr=%08x pmr=%08x bpr=%08x iidr=%08x\n",
		readl(gc), readl(gc + 4), readl(gc + 8), readl(gc + 0xfc));

	/* is the ARM generic counter running at all? */
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(frq));
	v0 = m1_cntvct();
	m1_wait_us(5000);
	v1 = m1_cntvct();
	pr_info("M1DIAG cntfrq=%u cntvct %llu -> %llu (%s)\n", frq, v0, v1,
		v0 == v1 ? "FROZEN" : "running");

	/* MCT global counter + comparator 0 (SPI 102, GIC id 134) */
	pr_info("M1DIAG mct: starting counter\n");
	writel(readl(mct + 0x240) | (1 << 8), mct + 0x240);
	m1_wait_us(200);
	a = readl(mct + 0x100);
	m1_wait_us(200);
	b = readl(mct + 0x100);
	pr_info("M1DIAG mct cnt %08x -> %08x tcon=%08x\n", a, b, readl(mct + 0x240));
	if (a == b) {
		pr_info("M1DIAG mct counter not counting, skipping compare test\n");
		return;
	}
	writel(b + 24000, mct + 0x200);
	writel(0, mct + 0x204);
	writel(1, mct + 0x248);
	writel(readl(mct + 0x240) | 1, mct + 0x240);
	m1_wait_us(3000);
	pr_info("M1DIAG mct cstat=%08x gic ispendr4=%08x (bit6=SPI102)\n",
		readl(mct + 0x244), readl(gd + 0x210));
	writel(readl(mct + 0x240) & ~1, mct + 0x240);
	writel(1, mct + 0x244);
	writel(0, mct + 0x248);
	writel(1 << 6, gd + 0x290);
	pr_info("M1DIAG done\n");
}

static void __init m1_init_time(void)
{
	m1_diag();
	of_clk_init(NULL);
	timer_probe();
	tick_setup_hrtimer_broadcast();
}

static noinline int m2_smc(unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3)
{
	register unsigned long r0 asm("r0") = a0;
	register unsigned long r1 asm("r1") = a1;
	register unsigned long r2 asm("r2") = a2;
	register unsigned long r3 asm("r3") = a3;

	asm volatile(".arch_extension sec\n\tsmc #0"
		     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : : "memory");
	return r0;
}

/* Samsung S-Boot monitor: SMC_CMD_REG(-101) SFR write to the eMMC FMP block */
static int __init m2_fmp_unlock(void)
{
	int r, r2;

	if (!of_machine_is_compatible("samsung,gts210wifi"))
		return 0;
	r = m2_smc((unsigned long)-101, (1UL << 30) | (0x15541000 >> 2), 0, 0);
	r2 = m2_smc((unsigned long)-101, (3UL << 30) | (0x15541000 >> 2), 0, 0);
	pr_info("M2 FMP smc write ret=%d, read ret=0x%x\n", r, r2);
	return 0;
}
arch_initcall(m2_fmp_unlock);

DT_MACHINE_START(EXYNOS_DT, "Samsung Exynos (Flattened Device Tree)")
	.l2c_aux_val	= 0x08400000,
	.l2c_aux_mask	= 0xf60fffff,
	.smp		= smp_ops(exynos_smp_ops),
	.map_io		= exynos_init_io,
	.init_early	= exynos_firmware_init,
	.init_irq	= exynos_init_irq,
	.init_time	= m1_init_time,
	.init_machine	= exynos_dt_machine_init,
	.init_late	= exynos_pm_init,
	.dt_compat	= exynos_dt_compat,
	.dt_fixup	= exynos_dt_fixup,
MACHINE_END
