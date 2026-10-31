// SPDX-License-Identifier: GPL-2.0-only
/*
 * Early console that appends to Samsung's "sec_log" RAM ring buffer.
 *
 * Samsung Exynos 5433 devices (Galaxy Tab S2 and friends) keep the kernel log
 * of the previous boot in RAM and show it as /proc/last_kmsg.  The buffer is
 * reserved by the bootloader (sec_log=0x200000@0x26000000 on the Tab S2):
 *
 *   base - 8   u32  magic 0x4d474f4c ("LOGM")
 *   base - 4   u32  byte counter, the write position is counter & (size - 1)
 *   base       data, size bytes (2 MiB)
 *
 * With no UART reachable on the board this gives readable boot logs after a
 * hang or crash: reset, boot TWRP/Android/Alpine and "cat /proc/last_kmsg".
 *
 * Use:   earlycon=seclog,0x25fffff8      (the address of the magic word)
 *
 * The ARM fixmap provides a single early I/O page, so every access remaps it,
 * and the pages are mapped as device memory, so every byte reaches RAM at once
 * (no cache maintenance needed before a reset).
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/serial_core.h>
#include <asm/fixmap.h>

#define SECLOG_HDR_PA	0x25fffff8UL
#define SECLOG_DATA_PA	0x26000000UL
#define SECLOG_SIZE	0x200000UL
#define SECLOG_MAGIC	0x4d474f4cU

static void __iomem *seclog_map(unsigned long pa)
{
	set_fixmap_io(FIX_EARLYCON_MEM_BASE, pa & PAGE_MASK);
	return (void __iomem *)__fix_to_virt(FIX_EARLYCON_MEM_BASE) +
	       (pa & ~PAGE_MASK);
}

static void seclog_write(struct console *con, const char *s, unsigned int n)
{
	void __iomem *hdr = seclog_map(SECLOG_HDR_PA);
	u32 counter = readl_relaxed(hdr + 4);

	while (n) {
		unsigned long pos = counter & (SECLOG_SIZE - 1);
		unsigned long pa = SECLOG_DATA_PA + pos;
		unsigned long chunk = min3((unsigned long)n,
					   PAGE_SIZE - (pa & ~PAGE_MASK),
					   SECLOG_SIZE - pos);

		memcpy_toio(seclog_map(pa), s, chunk);
		s += chunk;
		n -= chunk;
		counter += chunk;
	}

	hdr = seclog_map(SECLOG_HDR_PA);
	writel(counter, hdr + 4);
}

static int __init seclog_earlycon_setup(struct earlycon_device *dev,
					const char *opt)
{
	void __iomem *hdr = seclog_map(SECLOG_HDR_PA);

	if (readl_relaxed(hdr) != SECLOG_MAGIC) {
		writel_relaxed(SECLOG_MAGIC, hdr);
		writel(0, hdr + 4);
	}

	dev->con->write = seclog_write;
	return 0;
}
EARLYCON_DECLARE(seclog, seclog_earlycon_setup);
