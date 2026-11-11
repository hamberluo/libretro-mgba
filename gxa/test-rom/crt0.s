@ SPDX-License-Identifier: MIT
@ GXA conformance ROM: the cartridge header (logo and checksum filled in by build.sh), then stacks, .bss, main.
	.section .crt0, "ax"
	.arm
	.global _start
_start:
	b	reset
	.fill	156, 1, 0		@ logo
	.ascii	"GXA CONFORM\0"		@ title, 12 bytes
	.ascii	"GXAC"			@ game code
	.ascii	"01"			@ maker
	.byte	0x96, 0, 0		@ fixed value, unit, device
	.fill	7, 1, 0
	.byte	0			@ version
	.byte	0			@ complement checksum
	.fill	2, 1, 0
reset:
	mov	r0, #0x12		@ IRQ mode
	msr	cpsr_c, r0
	ldr	sp, =0x03007FA0
	mov	r0, #0x1F		@ system mode
	msr	cpsr_c, r0
	ldr	sp, =0x03007F00
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b
	ldr	r0, =main
	mov	lr, pc
	bx	r0
	b	.
