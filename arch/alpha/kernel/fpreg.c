/*
 * kernel/fpreg.c
 *
 * (C) Copyright 1998 Linus Torvalds
 */

#ifdef __alpha_cix__
#define STT(reg,val)  asm volatile ("ftoit $f"#reg",%0" : "=r"(val));
#else
#define STT(reg,val)  asm volatile ("stt $f"#reg",%0" : "=m"(val));
#endif

unsigned long
alpha_read_fp_reg (unsigned long reg)
{
	unsigned long val;

	switch (reg) {
	      case  0: STT( 0, val); break;
	      case  1: STT( 1, val); break;
	      case  2: STT( 2, val); break;
	      case  3: STT( 3, val); break;
	      case  4: STT( 4, val); break;
	      case  5: STT( 5, val); break;
	      case  6: STT( 6, val); break;
	      case  7: STT( 7, val); break;
	      case  8: STT( 8, val); break;
	      case  9: STT( 9, val); break;
	      case 10: STT(10, val); break;
	      case 11: STT(11, val); break;
	      case 12: STT(12, val); break;
	      case 13: STT(13, val); break;
	      case 14: STT(14, val); break;
	      case 15: STT(15, val); break;
	      case 16: STT(16, val); break;
	      case 17: STT(17, val); break;
	      case 18: STT(18, val); break;
	      case 19: STT(19, val); break;
	      case 20: STT(20, val); break;
	      case 21: STT(21, val); break;
	      case 22: STT(22, val); break;
	      case 23: STT(23, val); break;
	      case 24: STT(24, val); break;
	      case 25: STT(25, val); break;
	      case 26: STT(26, val); break;
	      case 27: STT(27, val); break;
	      case 28: STT(28, val); break;
	      case 29: STT(29, val); break;
	      case 30: STT(30, val); break;
	      case 31: STT(31, val); break;
	}
	return val;
}

#ifdef __alpha_cix__
#define LDT(reg,val)  asm volatile ("itoft %0,$f"#reg : : "r"(val));
#else
#define LDT(reg,val)  asm volatile ("ldt $f"#reg",%0" : : "m"(val));
#endif

void
alpha_write_fp_reg (unsigned long reg, unsigned long val)
{
	switch (reg) {
	      case  0: LDT( 0, val); break;
	      case  1: LDT( 1, val); break;
	      case  2: LDT( 2, val); break;
	      case  3: LDT( 3, val); break;
	      case  4: LDT( 4, val); break;
	      case  5: LDT( 5, val); break;
	      case  6: LDT( 6, val); break;
	      case  7: LDT( 7, val); break;
	      case  8: LDT( 8, val); break;
	      case  9: LDT( 9, val); break;
	      case 10: LDT(10, val); break;
	      case 11: LDT(11, val); break;
	      case 12: LDT(12, val); break;
	      case 13: LDT(13, val); break;
	      case 14: LDT(14, val); break;
	      case 15: LDT(15, val); break;
	      case 16: LDT(16, val); break;
	      case 17: LDT(17, val); break;
	      case 18: LDT(18, val); break;
	      case 19: LDT(19, val); break;
	      case 20: LDT(20, val); break;
	      case 21: LDT(21, val); break;
	      case 22: LDT(22, val); break;
	      case 23: LDT(23, val); break;
	      case 24: LDT(24, val); break;
	      case 25: LDT(25, val); break;
	      case 26: LDT(26, val); break;
	      case 27: LDT(27, val); break;
	      case 28: LDT(28, val); break;
	      case 29: LDT(29, val); break;
	      case 30: LDT(30, val); break;
	      case 31: LDT(31, val); break;
	}
}
