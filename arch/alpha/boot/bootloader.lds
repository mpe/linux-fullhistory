OUTPUT_FORMAT("ecoff-littlealpha")
ENTRY(__start)
SECTIONS
{
  .text 0x20000000: {
     _ftext = . ;
     __istart = . ;
     eprol  =  .;
    *(.text)
     __fstart = . ;
     _etext  =  .;
  }
  .rdata : {
    *(.rdata)
  }
  .pdata : {
     _fpdata = .;
    *(.pdata)
  }
  .data : {
     _fdata = .;
    *(.data)
    CONSTRUCTORS
  }
  .xdata : {
    *(.xdata)
  }
   _gp = ALIGN (16) + 0x8000;
  .lit8 : {
    *(.lit8)
  }
  .lita : {
    *(.lita)
  }
  .sdata : {
    *(.sdata)
  }
   _EDATA  =  .;
   _FBSS = .;
  .sbss : {
    *(.sbss)
    *(.scommon)
  }
  .bss : {
    *(.bss)
    *(COMMON)
  }
   _end = .;
}
