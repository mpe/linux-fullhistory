#ifndef __irq_h
#define __irq_h

/*
 * Various low-level irq details needed by irq.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

#ifdef __SMP__

#undef INIT_STUCK
#define INIT_STUCK 200000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("irq_enter stuck (irq=%d, cpu=%d, global=%d)\n",irq,cpu,global_irq_holder); stuck = INIT_STUCK;}

static inline void irq_enter(int cpu, int irq)
{
	int stuck = INIT_STUCK;

	hardirq_enter(cpu);
	while (test_bit(0,&global_irq_lock)) {
		if ((unsigned char) cpu == global_irq_holder) {
			printk("BAD! Local interrupts enabled, global disabled\n");
			break;
		}
		STUCK;
		/* nothing */;
	}
}

static inline void irq_exit(int cpu, int irq)
{
	hardirq_exit(cpu);
	release_irqlock(cpu);
}

#else

#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])

#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#define SAVE_ALL \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

/*
 * These are used just for the "bad" interrupt handlers, 
 * which just clear the mask and return..
 */
#define SAVE_MOST \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

#define RESTORE_MOST \
	"popl %ecx\n\t" \
	"popl %edx\n\t" \
	"popl %eax\n\t" \
	"pop %ds\n\t" \
	"pop %es\n\t" \
	"iret"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
#define BAD_IRQ_NAME(nr) IRQ_NAME2(bad_IRQ##nr)

#define GET_CURRENT \
	"movl %esp, %ebx\n\t" \
	"andl $-8192, %ebx\n\t"

#ifdef __SMP__

/*
 *	SMP has a few special interrupts for IPI messages
 */

#define BUILD_SMP_INTERRUPT(x) \
asmlinkage void x(void); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $-1\n\t" \
	SAVE_ALL \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"jmp ret_from_intr\n");

#define BUILD_SMP_TIMER_INTERRUPT(x) \
asmlinkage void x(struct pt_regs * regs); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $-1\n\t" \
	SAVE_ALL \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"addl $4,%esp\n\t" \
	"jmp ret_from_intr\n");

#endif /* __SMP__ */

#define BUILD_COMMON_IRQ() \
__asm__( \
	"\n" __ALIGN_STR"\n" \
	"common_interrupt:\n\t" \
	SAVE_ALL \
	"pushl $ret_from_intr\n\t" \
	"jmp "SYMBOL_NAME_STR(do_IRQ));

#define BUILD_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $"#nr"-256\n\t" \
	"jmp common_interrupt");

/*
 * x86 profiling function, SMP safe. We might want to do this in
 * assembly totally?
 */
static inline void x86_do_profile (unsigned long eip)
{
	if (prof_buffer && current->pid) {
		extern int _stext;
		eip -= (unsigned long) &_stext;
		eip >>= prof_shift;
		if (eip < prof_len)
			atomic_inc((atomic_t *)&prof_buffer[eip]);
		else
		/*
		 * Dont ignore out-of-bounds EIP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
			atomic_inc((atomic_t *)&prof_buffer[prof_len-1]);
	}
}

#endif
