OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  . = 0;
  _text = .;
  .text : {
   _stext = .;
   _start = .;
   init.o(.start)
   kernel_start = .;
   kernel.o
   kernel_len = . - kernel_start;
   . = ALIGN(32);
   *(.text)
   initrd_start = .;
   initrd.o
   initrd_len = . - initrd_start;
   . = ALIGN(32);
   _etext = .;
  }
  
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
