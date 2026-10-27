/*
 * sweet boot shim entry: arm64 Linux Image header so the bootloader treats
 * us like a kernel. MMU/D-cache off, x0 = DTB pointer.
 * AGM H6: the Unisoc bootloader and PSCI CPU_ON enter at EL2, so both entry
 * points first drop to EL1h, where the rest of the shim (and xnu) runs.
 */

/* EL2 -> EL1h if needed; keeps x0..x8, clobbers x9. */
.macro el2_to_el1
	mrs	x9, CurrentEL
	cmp	x9, #(2 << 2)
	b.ne	9f
	mov	x9, #(1 << 31)		/* HCR_EL2.RW: EL1 is AArch64, nothing routed to EL2 */
	msr	hcr_el2, x9
	mrs	x9, cnthctl_el2
	orr	x9, x9, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x9
	msr	cntvoff_el2, xzr
	mov	x9, #0x33ff		/* CPTR_EL2: no FP/SIMD traps */
	msr	cptr_el2, x9
	mrs	x9, icc_sre_el2
	orr	x9, x9, #0x1		/* SRE */
	orr	x9, x9, #0x8		/* Enable: EL1 may use the GIC sysregs */
	msr	icc_sre_el2, x9
	msr	ich_hcr_el2, xzr
	movz	x9, #0x0800
	movk	x9, #0x30d0, lsl #16	/* SCTLR_EL1: RES1 bits, MMU/caches off */
	msr	sctlr_el1, x9
	adr	x9, vectors		/* only reached by an unexpected EL2 trap */
	msr	vbar_el2, x9
	mov	x9, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x9
	adr	x9, 9f
	msr	elr_el2, x9
	isb
	eret
9:
.endm
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0x80000			/* text_offset, as the stock kernel */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 (PE offset) */

	.text
	.globl _start
_start:
	el2_to_el1
	msr	daifset, #0xf
	mov	x19, x0			/* DTB pointer from ABL */
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	/* zero .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
	bl	shim_main
3:	wfe
	b	3b

/* PSCI CPU_ON target. x0 = context id = cpu index (1..7). */
	.globl secondary_entry
secondary_entry:
	el2_to_el1
	msr	daifset, #0xf
	msr	spsel, #1
	adr	x1, _sec_stacks
	add	x1, x1, x0, lsl #12
	add	x1, x1, #0x1000
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	bl	secondary_main
4:	wfe
	b	4b

/*
 * AGM H6: xnu's secondaries. The firmware's PSCI CPU_ON takes no entry point above 4 GB, and
 * xnu lives above 4 GB, so xnu points CPU_ON here (the address is in its boot-args as
 * agm_tramp=, see loader.c) after writing its own entry point (a physical address) into
 * xnu_secondary_target. The core comes in at EL2 like the boot core, drops to EL1h the same
 * way and goes on to xnu's entry with x0 (the context id) as CPU_ON gave it.
 */
	.balign	64
	.globl	xnu_secondary_target
xnu_secondary_target:		/* xnu_secondary_tramp - 0x40, whatever the length of the code */
	.quad	0
	.balign	64
	.globl	xnu_secondary_tramp
xnu_secondary_tramp:
	el2_to_el1
	adr	x9, xnu_secondary_target
	ldr	x9, [x9]
	cbz	x9, 5f
	br	x9
5:	wfe
	b	5b

/*
 * Exception vectors. Each slot stashes x29/x30, loads its slot number
 * and jumps to the common path, which saves x0-x28 and calls
 * exc_handler(slot, regs). IRQs return; everything else ends in the
 * on-screen panic.
 */
.macro vent slot
	.balign	0x80
	stp	x29, x30, [sp, #-16]!
	mov	x29, #\slot
	b	exc_common
.endm

	.balign	0x800
	.globl vectors
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	sub	sp, sp, #(30 * 8)
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	str	x28, [sp, #224]
	mov	x0, x29
	mov	x1, sp
	bl	exc_handler
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldr	x28, [sp, #224]
	add	sp, sp, #(30 * 8)
	ldp	x29, x30, [sp], #16
	eret
