#ifndef __ALPHA_UNALIGNED_H
#define __ALPHA_UNALIGNED_H

/*
 * inline functions to do unaligned accesses.. See entUna in traps.c
 */
extern inline unsigned long ldq_u(unsigned long * r11)
{
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extql %0,%2,%0\n\t"
		"extqh %1,%2,%1\n\t"
		"bis %1,%0,%0"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(unsigned long *)(7+(char *) r11)));
	return r1;
}

extern inline unsigned long ldl_u(unsigned int * r11)
{
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extll %0,%2,%0\n\t"
		"extlh %1,%2,%1\n\t"
		"bis %1,%0,%0"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(unsigned long *)(3+(char *) r11)));
	return r1;
}

extern inline unsigned long ldw_u(unsigned short * r11)
{
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extwl %0,%2,%0\n\t"
		"extwh %1,%2,%1\n\t"
		"bis %1,%0,%0"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(unsigned long *)(1+(char *) r11)));
	return r1;
}

extern inline void stq_u(unsigned long r5, unsigned long * r11)
{
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"insqh %6,%7,%5\n\t"
		"insql %6,%7,%4\n\t"
		"mskqh %3,%7,%3\n\t"
		"mskql %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(7+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
}

extern inline void stl_u(unsigned long r5, unsigned int * r11)
{
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"inslh %6,%7,%5\n\t"
		"insll %6,%7,%4\n\t"
		"msklh %3,%7,%3\n\t"
		"mskll %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(3+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
}

extern inline void stw_u(unsigned long r5, unsigned short * r11)
{
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"inswh %6,%7,%5\n\t"
		"inswl %6,%7,%4\n\t"
		"mskwh %3,%7,%3\n\t"
		"mskwl %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(1+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
}

#endif
