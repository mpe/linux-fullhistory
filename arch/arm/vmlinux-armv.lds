/* ld script to make ARM Linux kernel
 * taken from the i386 version
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */
OUTPUT_FORMAT("elf32-arm", "elf32-arm", "elf32-arm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090
  .text.lock : { *(.text.lock) }	/* out-of-line lock text */
  .rodata : { *(.rodata) }
  .kstrtab : { *(.kstrtab) }

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  __start___ksymtab = .;	/* Kernel symbol table */
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;

  _etext = .;			/* End of text section */

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  _edata = .;			/* End of data section */

  /* This has to be aligned to a page boundary to do us any good.  This
     alignment is overkill for ARM6 up but needed for ARM3.  Since all this
     data will be thrown away I don't think the extra padding will hurt.
       -- pb */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .text.init : { *(.text.init) }
  .data.init : { *(.data.init) }
  . = ALIGN(4096);
  __init_end = .;

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss)
	}
  _end = . ;

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
