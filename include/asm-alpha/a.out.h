#ifndef __ALPHA_A_OUT_H__
#define __ALPHA_A_OUT_H__

/* OSF/1 pseudo-a.out header */
struct exec
{
	/* OSF/1 "file" header */
	unsigned short f_magic, f_nscns;
	unsigned int f_timdat;
	unsigned long f_symptr;
	unsigned int f_nsyms;
	unsigned short f_opthdr, f_flags;
	/* followed by a more normal "a.out" header */
	unsigned long a_info;		/* after that it looks quite normal.. */
	unsigned long a_text;
	unsigned long a_data;
	unsigned long a_bss;
	unsigned long a_entry;
	unsigned long a_textstart;	/* with a few additions that actually make sense */
	unsigned long a_datastart;
	unsigned long a_bssstart;
	unsigned int  a_gprmask, a_fprmask;	/* but what are these? */
	unsigned long a_gpvalue;
};
#define N_TXTADDR(x) ((x).a_textstart)
#define N_DATADDR(x) ((x).a_datastart)
#define N_BSSADDR(x) ((x).a_bssstart)
#define N_DRSIZE(x) 0
#define N_TRSIZE(x) 0
#define N_SYMSIZE(x) 0

#define SCNHSZ		64		/* XXX should be sizeof(scnhdr) */
#define SCNROUND	16

#define N_TXTOFF(x) \
  ((long) N_MAGIC(x) == ZMAGIC ? 0 : \
   (sizeof(struct exec) + (x).f_nscns*SCNHSZ + SCNROUND - 1) & ~(SCNROUND - 1))

#ifdef __KERNEL__

#define STACK_TOP (0x00120000000UL)

#endif

#endif /* __A_OUT_GNU_H__ */
