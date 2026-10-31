/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/debug/seclog.S
 *
 * DEBUG_LL "UART" that appends to Samsung's sec_log ring buffer (Exynos 5433,
 * Galaxy Tab S2).  Layout, from drivers/staging/samsung/sec_log.c of the
 * Samsung kernels:
 *
 *   0x25fffff8  u32  magic 0x4d474f4c ("LOGM")
 *   0x25fffffc  u32  write counter (position = counter & 0x1fffff)
 *   0x26000000  data, 2 MiB ring
 *
 * The next kernel to boot (TWRP, Android, Alpine) shows it in /proc/last_kmsg.
 *
 * Only the zImage decompressor uses this.  It runs with the MMU either off or
 * identity mapped (the Tab S2 bootloader leaves it on), so physical addresses
 * work directly.  In the kernel proper addruart returns 0, so nothing maps or
 * touches the buffer there; use earlycon=seclog,0x25fffff8 instead.
 *
 * The decompressor's putc() calls, per character:
 *   addruart r1, r2, r3 ; waituarttxrdy r3, r1 ; senduart r0, r1 ; busyuart r3, r1
 * and addruart is evaluated again for every character, so the only state is
 * the counter in memory.  Every store is cleaned to RAM, so the text survives
 * a hang followed by a forced reset.
 */

#ifdef ZIMAGE
		.macro	addruart, rp, rv, tmp
		ldr	\rp, =0x25fffff8
		ldr	\tmp, [\rp, #4]		@ write counter
		add	\rp, \rp, #8		@ start of data
		add	\rp, \rp, \tmp		@ next free byte
		mov	\rv, \rp
		.endm
#else
		.macro	addruart, rp, rv, tmp
		mov	\rp, #0
		mov	\rv, #0
		.endm
#endif

		.macro	senduart, rd, rx
		strb	\rd, [\rx]
		.endm

		.macro	waituartcts, rd, rx
		.endm

		.macro	waituarttxrdy, rd, rx
		.endm

		/*
		 * rx = address the character was just stored to = 0x26000000 + counter.
		 * New counter = rx - 0x25ffffff.  Store it at 0x25fffffc, which is
		 * (rx - 3) - new counter, then clean the data and header lines to RAM.
		 * Leaves rx pointing at the next free byte, because head.S's DEBUG helpers
		 * (puts/phex) load the base once per string and reuse it for every character.
		 */
		.macro	busyuart, rd, rx
		ldr	\rd, =0x25ffffff
		sub	\rd, \rx, \rd		@ rd = new counter
		sub	\rx, \rx, #3
		str	\rd, [\rx, -\rd]	@ -> 0x25fffffc
#ifdef ZIMAGE
		sub	\rd, \rx, \rd		@ rd = 0x25fffffc
		mcr	p15, 0, \rd, c7, c10, 1	@ DCCMVAC: header line
		add	\rx, \rx, #3
		mcr	p15, 0, \rx, c7, c10, 1	@ DCCMVAC: data line
		dsb
		add	\rx, \rx, #1
#else
		add	\rx, \rx, #4
#endif
		.endm
