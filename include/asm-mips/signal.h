#ifndef __ASM_MIPS_SIGNAL_H
#define __ASM_MIPS_SIGNAL_H

struct sigcontext_struct {
	unsigned long	       sc_at, sc_v0, sc_v1, sc_a0, sc_a1, sc_a2, sc_a3;
	unsigned long	sc_t0, sc_t1, sc_t2, sc_t3, sc_t4, sc_t5, sc_t6, sc_t7;
	unsigned long	sc_s0, sc_s1, sc_s2, sc_s3, sc_s4, sc_s5, sc_s6, sc_s7;
	unsigned long	sc_t8, sc_t9,               sc_gp, sc_sp, sc_fp, sc_ra;

	unsigned long	sc_epc;
	unsigned long	sc_cause;

	unsigned long	oldmask;
};

#endif /* __ASM_MIPS_SIGNAL_H */
