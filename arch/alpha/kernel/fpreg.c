/*
 * kernel/fpreg.c
 *
 * (C) Copyright 1998 Linus Torvalds
 */

unsigned long
alpha_read_fp_reg (unsigned long reg)
{
	unsigned long r;

	switch (reg) {
	      case  0: asm ("stt  $f0,%0" : "m="(r)); break;
	      case  1: asm ("stt  $f1,%0" : "m="(r)); break;
	      case  2: asm ("stt  $f2,%0" : "m="(r)); break;
	      case  3: asm ("stt  $f3,%0" : "m="(r)); break;
	      case  4: asm ("stt  $f4,%0" : "m="(r)); break;
	      case  5: asm ("stt  $f5,%0" : "m="(r)); break;
	      case  6: asm ("stt  $f6,%0" : "m="(r)); break;
	      case  7: asm ("stt  $f7,%0" : "m="(r)); break;
	      case  8: asm ("stt  $f8,%0" : "m="(r)); break;
	      case  9: asm ("stt  $f9,%0" : "m="(r)); break;
	      case 10: asm ("stt $f10,%0" : "m="(r)); break;
	      case 11: asm ("stt $f11,%0" : "m="(r)); break;
	      case 12: asm ("stt $f12,%0" : "m="(r)); break;
	      case 13: asm ("stt $f13,%0" : "m="(r)); break;
	      case 14: asm ("stt $f14,%0" : "m="(r)); break;
	      case 15: asm ("stt $f15,%0" : "m="(r)); break;
	      case 16: asm ("stt $f16,%0" : "m="(r)); break;
	      case 17: asm ("stt $f17,%0" : "m="(r)); break;
	      case 18: asm ("stt $f18,%0" : "m="(r)); break;
	      case 19: asm ("stt $f19,%0" : "m="(r)); break;
	      case 20: asm ("stt $f20,%0" : "m="(r)); break;
	      case 21: asm ("stt $f21,%0" : "m="(r)); break;
	      case 22: asm ("stt $f22,%0" : "m="(r)); break;
	      case 23: asm ("stt $f23,%0" : "m="(r)); break;
	      case 24: asm ("stt $f24,%0" : "m="(r)); break;
	      case 25: asm ("stt $f25,%0" : "m="(r)); break;
	      case 26: asm ("stt $f26,%0" : "m="(r)); break;
	      case 27: asm ("stt $f27,%0" : "m="(r)); break;
	      case 28: asm ("stt $f28,%0" : "m="(r)); break;
	      case 29: asm ("stt $f29,%0" : "m="(r)); break;
	      case 30: asm ("stt $f30,%0" : "m="(r)); break;
	      case 31: asm ("stt $f31,%0" : "m="(r)); break;
	      default:
		break;
	}
	return r;
}

#if 1
/*
 * This is IMHO the better way of implementing LDT().  But it
 * has the disadvantage that gcc 2.7.0 refuses to compile it
 * (invalid operand constraints), so instead, we use the uglier
 * macro below.
 */
# define LDT(reg,val)	\
  asm volatile ("ldt $f"#reg",%0" : : "m"(val));
#else
# define LDT(reg,val)	\
  asm volatile ("ldt $f"#reg",0(%0)" : : "r"(&val));
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
	      default:
		break;
	}
}
