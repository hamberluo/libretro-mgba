/* SPDX-License-Identifier: MIT */
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
	rom   : ORIGIN = 0x08000000, LENGTH = 32M
	iwram : ORIGIN = 0x03000000, LENGTH = 0x7900
}
SECTIONS {
	.text : {
		KEEP(*(.crt0))
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > rom
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.*) *(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > iwram
	/DISCARD/ : { *(.data .data.* .ARM.exidx*) }
}
