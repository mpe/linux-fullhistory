/* ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka
 */
#include <linux/config.h>
#ifdef CONFIG_LITTLE_ENDIAN
OUTPUT_FORMAT("elf32-shl", "elf32-shl", "elf32-shl")
#else
OUTPUT_FORMAT("elf32-sh", "elf32-sh", "elf32-sh")
#endif
OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000 + CONFIG_MEMORY_START + 0x1000;
  __text = .;			/* Text and read-only data */
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} = 0
  .text.lock : { *(.text.lock) }	/* out-of-line lock text */
  .rodata : { *(.rodata) }
  .kstrtab : { *(.kstrtab) }

  . = ALIGN(16);		/* Exception table */
  ___start___ex_table = .;
  ___ex_table : { *(__ex_table) }
  ___stop___ex_table = .;

  ___start___ksymtab = .;	/* Kernel symbol table */
  ___ksymtab : { *(__ksymtab) }
  ___stop___ksymtab = .;

  __etext = .;			/* End of text section */

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  __edata = .;			/* End of data section */

  . = ALIGN(8192);		/* init_task */
  .data.init_task : { *(.data.init_task) }
  /* stack */
  .stack : { _stack = .;  __stack = .; }

  . = ALIGN(4096);		/* Init code and data */
  ___init_begin = .;
  .text.init : { *(.text.init) }
  .data.init : { *(.data.init) }
  . = ALIGN(16);
  ___setup_start = .;
  .setup.init : { *(.setup.init) }
  ___setup_end = .;
  ___initcall_start = .;
  .initcall.init : { *(.initcall.init) }
  ___initcall_end = .;
  . = ALIGN(4096);
  ___init_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  . = ALIGN(4096);
  .data.disk_image : { *(.data.disk_image) }

  . = ALIGN(4);
  ___bss_start = .;		/* BSS */
  .bss : {
	*(.bss)
	}
  . = ALIGN(4);
  __end = . ;

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging section are relative to the beginning
     of the section so we begin .debug at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* These must appear regardless of  .  */
}
