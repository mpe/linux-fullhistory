#ifndef __irq_h
#define __irq_h

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

void mask_irq(unsigned int irq);
void unmask_irq(unsigned int irq);
void enable_IO_APIC_irq (unsigned int irq);
void disable_IO_APIC_irq (unsigned int irq);
void set_8259A_irq_mask(unsigned int irq);
void ack_APIC_irq (void);
void setup_IO_APIC (void);
void init_IO_APIC_traps(void);
int IO_APIC_get_PCI_irq_vector (int bus, int slot, int fn);
void make_8259A_irq (unsigned int irq);
void send_IPI (int dest, int vector);
void init_pic_mode (void);

extern unsigned int io_apic_irqs;

extern inline int IO_APIC_VECTOR (int irq)
{
	return (0x51+(irq<<3));
}

#define MAX_IRQ_SOURCES 128
#define MAX_MP_BUSSES 32
enum mp_bustype {
	MP_BUS_ISA,
	MP_BUS_PCI
};
extern int mp_bus_id_to_type [MAX_MP_BUSSES];
extern int mp_bus_id_to_pci_bus [MAX_MP_BUSSES];
extern char ioapic_OEM_ID [16];
extern char ioapic_Product_ID [16];

extern spinlock_t irq_controller_lock; /*
					* Protects both the 8259 and the
					* IO-APIC
					*/


#ifdef __SMP__

#include <asm/atomic.h>

static inline void irq_enter(int cpu, unsigned int irq)
{
	hardirq_enter(cpu);
	while (test_bit(0,&global_irq_lock)) {
		/* nothing */;
	}
}

static inline void irq_exit(int cpu, unsigned int irq)
{
	hardirq_exit(cpu);
	release_irqlock(cpu);
}

#define IO_APIC_IRQ(x) ((1<<x) & io_apic_irqs)

#else

#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])

/* Make these no-ops when not using SMP */
#define enable_IO_APIC_irq(x)	do { } while (0)
#define disable_IO_APIC_irq(x)	do { } while (0)

#define IO_APIC_IRQ(x)	(0)

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
	"movl $" STR(__KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

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

#define BUILD_IRQ(nr) \
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
		/*
		 * Dont ignore out-of-bounds EIP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
		if (eip > prof_len-1)
			eip = prof_len-1;
		atomic_inc((atomic_t *)&prof_buffer[eip]);
	}
}

#endif
