OUTPUT_FORMAT("elf64-alpha")
ENTRY(__start)
SECTIONS
{
   . = 0xfffffc0000310000;
   _text = .;
   .text : { *(.text) }
   .text2 : { *(.text2) }
   _etext = .;

  /* Exception table */
  . = ALIGN(16);
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  /* Kernel symbol table */
  . = ALIGN(8);
  __start___ksymtab = .;
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;
  .kstrtab : { *(.kstrtab) }

  /* Startup code */
  . = ALIGN(8192);
  __init_begin = .;
  .text.init : { *(.text.init) }
  .data.init : { *(.data.init) }
  . = ALIGN(2*8192);	/* Align double page for init_task_union */
  __init_end = .;

  /* The initial task and kernel stack */
  init_task : { *(init_task) }

  /* Global data */
  _data = .;
  .rodata : { *(.rodata) }
  .data : { *(.data) CONSTRUCTORS }
  .got : { *(.got) }
  .sdata : { *(.sdata) }
  _edata = .;
  _bss = .;
  .sbss : { *(.sbss) *(.scommon) }
  .bss : { *(.bss) *(COMMON) }
  _end = .;

  .mdebug 0 : { *(.mdebug) }
  .note 0 : { *(.note) }
  .comment 0 : { *(.comment) }
}
