/*
 *	linux/arch/alpha/kernel/smp.c
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/spinlock.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

#define __KERNEL_SYSCALLS__
#include <asm/unistd.h>

#include "proto.h"

#define DEBUG_SMP 0
#if DEBUG_SMP
#define DBGS(args)	printk args
#else
#define DBGS(args)
#endif

struct ipi_msg_flush_tb_struct ipi_msg_flush_tb __cacheline_aligned;

struct cpuinfo_alpha cpu_data[NR_CPUS];

spinlock_t ticker_lock = SPIN_LOCK_UNLOCKED;
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

unsigned int boot_cpu_id = 0;
static int smp_activated = 0;

int smp_found_config = 0; /* Have we found an SMP box */
static int max_cpus = -1;

unsigned int cpu_present_map = 0;

int smp_num_cpus = 1;
int smp_num_probed = 0; /* Internal processor count */

int smp_threads_ready = 0;
volatile unsigned long cpu_callin_map[NR_CPUS] = {0,};
volatile unsigned long smp_spinning[NR_CPUS] = { 0, };

cycles_t cacheflush_time;

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

volatile int ipi_bits[NR_CPUS] __cacheline_aligned;

unsigned long boot_cpu_palrev;

volatile int smp_commenced = 0;
volatile int smp_processors_ready = 0;

volatile int cpu_number_map[NR_CPUS];
volatile int cpu_logical_map[NR_CPUS];

extern void calibrate_delay(void);
extern struct thread_struct * original_pcb_ptr;

static void smp_setup_percpu_timer(void);
static void secondary_cpu_start(int, struct task_struct *);
static void send_cpu_msg(char *, int);

/* Process bootcommand SMP options, like "nosmp" and "maxcpus=" */
void __init
smp_setup(char *str, int *ints)
{
	if (ints && ints[0] > 0)
		max_cpus = ints[1];
	else
		max_cpus = 0;
}

static void __init
smp_store_cpu_info(int id)
{
	/* This is it on Alpha, so far. */
	cpu_data[id].loops_per_sec = loops_per_sec;
}

void __init
smp_commence(void)
{
	/* Lets the callin's below out of their loop. */
	mb();
	smp_commenced = 1;
}

void __init
smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	DBGS(("CALLIN %d state 0x%lx\n", cpuid, current->state));
#ifdef HUH
	local_flush_cache_all();
	local_flush_tlb_all();
#endif
#if 0
	set_irq_udt(mid_xlate[boot_cpu_id]);
#endif

	/* Get our local ticker going. */
	smp_setup_percpu_timer();

#if 0
	calibrate_delay();
#endif
	smp_store_cpu_info(cpuid);
#ifdef HUH
	local_flush_cache_all();
	local_flush_tlb_all();
#endif

	/* Allow master to continue. */
	set_bit(cpuid, (unsigned long *)&cpu_callin_map[cpuid]);
#ifdef HUH
	local_flush_cache_all();
	local_flush_tlb_all();
#endif

#ifdef NOT_YET
	while(!task[cpuid] || current_set[cpuid] != task[cpuid])
	        barrier();
#endif

#ifdef HUH
	local_flush_cache_all();
	local_flush_tlb_all();
#endif
#if 0
	__sti();
#endif
}

asmlinkage int __init
start_secondary(void *unused)
{
	extern asmlinkage void entInt(void);
	extern void paging_init_secondary(void);

	wrmces(7);
	paging_init_secondary();
	trap_init();
	wrent(entInt, 0);

	smp_callin();
	while (!smp_commenced)
		barrier();
#if 1
	printk("start_secondary: commencing CPU %d current %p\n",
	       hard_smp_processor_id(), current);
#endif
	cpu_idle(NULL);
}

static void __init
smp_tune_scheduling (void)
{
	/*
	 * Rough estimation for SMP scheduling, this is the number of
	 * cycles it takes for a fully memory-limited process to flush
	 * the SMP-local cache.
	 *
	 * We are not told how much cache there is, so we have to guess.
	 */

	struct percpu_struct *cpu;
	unsigned long on_chip_cache;
	unsigned long freq;

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	switch (cpu->type)
	{
	case EV45_CPU:
		on_chip_cache = 16 + 16;
		break;

	case EV5_CPU:
	case EV56_CPU:
		on_chip_cache = 8 + 8 + 96;
		break;

	case PCA56_CPU:
		on_chip_cache = 16 + 8;
		break;

	case EV6_CPU:
		on_chip_cache = 64 + 64;
		break;

	default:
		on_chip_cache = 8 + 8;
		break;
	}

	freq = hwrpb->cycle_freq ? : est_cycle_freq;

	/* Magic estimation stolen from x86 port.  */
	cacheflush_time = freq / 1024 * on_chip_cache / 5000;
}


/*
 *      Cycle through the processors sending START msgs to boot each.
 */
void __init
smp_boot_cpus(void)
{
	int cpucount = 0;
	int i, first, prev;

	printk("Entering SMP Mode.\n");

#if 0
	__sti();
#endif

	for(i=0; i < NR_CPUS; i++) {
		cpu_number_map[i] = -1;
		cpu_logical_map[i] = -1;
	        prof_counter[i] = 1;
	        prof_multiplier[i] = 1;
		ipi_bits[i] = 0;
	}

	cpu_number_map[boot_cpu_id] = 0;
	cpu_logical_map[0] = boot_cpu_id;
	current->processor = boot_cpu_id; /* ??? */

	smp_store_cpu_info(boot_cpu_id);
	smp_tune_scheduling();
#ifdef NOT_YET
	printk("CPU%d: ", boot_cpu_id);
	print_cpu_info(&cpu_data[boot_cpu_id]);
	set_irq_udt(mid_xlate[boot_cpu_id]);
#endif
	smp_setup_percpu_timer();
#ifdef HUH
	local_flush_cache_all();
#endif
	if (smp_num_probed == 1)
		return;  /* Not an MP box. */

#if NOT_YET
	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus)
	{
		smp_found_config = 0;
	        printk(KERN_INFO "SMP mode deactivated.\n");
	}
#endif

	for (i = 0; i < NR_CPUS; i++) {

		if (i == boot_cpu_id)
			continue;

	        if (cpu_present_map & (1 << i)) {
	                struct task_struct *idle;
	                int timeout;

	                /* Cook up an idler for this guy. */
	                kernel_thread(start_secondary, NULL, CLONE_PID);
	                idle = task[++cpucount];
			if (!idle)
				panic("No idle process for CPU %d", i);
	                idle->processor = i;

			DBGS(("smp_boot_cpus: CPU %d state 0x%lx flags 0x%lx\n",
			      i, idle->state, idle->flags));

	                /* whirrr, whirrr, whirrrrrrrrr... */
#ifdef HUH
	                local_flush_cache_all();
#endif
	                secondary_cpu_start(i, idle);

	                /* wheee... it's going... wait for 5 secs...*/
	                for (timeout = 0; timeout < 50000; timeout++) {
				if (cpu_callin_map[i])
					break;
	                        udelay(100);
	                }
	                if (cpu_callin_map[i]) {
				/* Another "Red Snapper". */
				cpu_number_map[i] = cpucount;
	                        cpu_logical_map[cpucount] = i;
	                } else {
				cpucount--;
	                        printk("smp_boot_cpus: Processor %d"
				       " is stuck 0x%lx.\n", i, idle->flags);
	                }
	        }
	        if (!(cpu_callin_map[i])) {
			cpu_present_map &= ~(1 << i);
	                cpu_number_map[i] = -1;
	        }
	}
#ifdef HUH
	local_flush_cache_all();
#endif
	if (cpucount == 0) {
		printk("smp_boot_cpus: ERROR - only one Processor found.\n");
	        cpu_present_map = (1 << smp_processor_id());
	} else {
		unsigned long bogosum = 0;
	        for (i = 0; i < NR_CPUS; i++) {
			if (cpu_present_map & (1 << i))
				bogosum += cpu_data[i].loops_per_sec;
	        }
	        printk("smp_boot_cpus: Total of %d Processors activated"
		       " (%lu.%02lu BogoMIPS).\n",
	               cpucount + 1,
	               (bogosum + 2500)/500000,
	               ((bogosum + 2500)/5000)%100);
	        smp_activated = 1;
	        smp_num_cpus = cpucount + 1;
	}

	/* Setup CPU list for IRQ distribution scheme. */
	first = prev = -1;
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_present_map & (1 << i)) {
			if (first == -1)
				first = i;
			if (prev != -1)
				cpu_data[i].next = i;
	                prev = i;
	        }
	}
	cpu_data[prev].next = first;

	/* Ok, they are spinning and ready to go. */
	smp_processors_ready = 1;
}

static void __init
smp_setup_percpu_timer(void)
{
	int cpu = smp_processor_id();

	prof_counter[cpu] = prof_multiplier[cpu] = 1;
#ifdef NOT_YET
	load_profile_irq(mid_xlate[cpu], lvl14_resolution);
	if (cpu == boot_cpu_id)
		enable_pil_irq(14);
#endif
}

extern void update_one_process(struct task_struct *p, unsigned long ticks,
	                       unsigned long user, unsigned long system,
			       int cpu);

void
smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

#ifdef NOT_YET
	clear_profile_irq(mid_xlate[cpu]);
	if(!user_mode(regs))
		alpha_do_profile(regs->pc);
#endif

	if (!--prof_counter[cpu]) {
		int user = user_mode(regs);
	        if (current->pid) {
			update_one_process(current, 1, user, !user, cpu);

	                if (--current->counter < 0) {
				current->counter = 0;
	                        current->need_resched = 1;
	                }

	                spin_lock(&ticker_lock);
	                if (user) {
				if (current->priority < DEF_PRIORITY) {
					kstat.cpu_nice++;
					kstat.per_cpu_nice[cpu]++;
				} else {
					kstat.cpu_user++;
					kstat.per_cpu_user[cpu]++;
				}
	                } else {
				kstat.cpu_system++;
				kstat.per_cpu_system[cpu]++;
	                }
	                spin_unlock(&ticker_lock);
	        }
	        prof_counter[cpu] = prof_multiplier[cpu];
	}
}

int __init
setup_profiling_timer(unsigned int multiplier)
{
#ifdef NOT_YET
	int i;
	unsigned long flags;

	/* Prevent level14 ticker IRQ flooding. */
	if((!multiplier) || (lvl14_resolution / multiplier) < 500)
	        return -EINVAL;

	save_and_cli(flags);
	for(i = 0; i < NR_CPUS; i++) {
	        if(cpu_present_map & (1 << i)) {
	                load_profile_irq(mid_xlate[i], lvl14_resolution / multip
lier);
	                prof_multiplier[i] = multiplier;
	        }
	}
	restore_flags(flags);

	return 0;

#endif
  return -EINVAL;
}

/* Only broken Intel needs this, thus it should not even be
   referenced globally.  */

void __init
initialize_secondary(void)
{
}

static void __init
secondary_cpu_start(int cpuid, struct task_struct *idle)
{
	struct percpu_struct *cpu;
	int timeout;
	  
	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);

	/* Set context to idle thread this CPU will use when running
	   assumption is that the idle thread is all set to go... ??? */
	memcpy(&cpu->hwpcb[0], &idle->tss, sizeof(struct pcb_struct));
	cpu->hwpcb[4] = cpu->hwpcb[0]; /* UNIQUE set to KSP ??? */

	DBGS(("KSP 0x%lx PTBR 0x%lx VPTBR 0x%lx\n",
	      cpu->hwpcb[0], cpu->hwpcb[2], hwrpb->vptb));
	DBGS(("Starting secondary cpu %d: state 0x%lx pal_flags 0x%lx\n",
	      cpuid, idle->state, idle->tss.pal_flags));

	/* Setup HWRPB fields that SRM uses to activate secondary CPU */
	hwrpb->CPU_restart = __start_cpu;
	hwrpb->CPU_restart_data = (unsigned long) idle;

	/* Recalculate and update the HWRPB checksum */
	hwrpb_update_checksum(hwrpb);

	/*
	 * Send a "start" command to the specified processor.
	 */

	/* SRM III 3.4.1.3 */
	cpu->flags |= 0x22;	/* turn on Context Valid and Restart Capable */
	cpu->flags &= ~1;	/* turn off Bootstrap In Progress */
	mb();

	send_cpu_msg("START\r\n", cpuid);

	/* now, we wait... */
	for (timeout = 10000; !(cpu->flags & 1); timeout--) {
		if (timeout <= 0) {
			printk("Processor %d failed to start\n", cpuid);
	                        /* needed for pset_info to work */
#if 0
			ipc_processor_enable(cpu_to_processor(cpunum));
#endif
			return;
		}
		mdelay(1);
	}
	DBGS(("secondary_cpu_start: SUCCESS for CPU %d!!!\n", cpuid));
}

static void
send_cpu_msg(char *str, int cpuid)
{
	struct percpu_struct *cpu;
	register char *cp1, *cp2;
	unsigned long cpumask;
	size_t len;
	int timeout;

	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);

	cpumask = (1L << cpuid);
	if (hwrpb->txrdy & cpumask)
		goto delay1;
	ready1:

	cp2 = str;
	len = strlen(cp2);
	*(unsigned int *)&cpu->ipc_buffer[0] = len;
	cp1 = (char *) &cpu->ipc_buffer[1];
	memcpy(cp1, cp2, len);

	/* atomic test and set */
	set_bit(cpuid, &hwrpb->rxrdy);

	if (hwrpb->txrdy & cpumask)
		goto delay2;
	ready2:
	return;

delay1:
	for (timeout = 10000; timeout > 0; --timeout) {
		if (!(hwrpb->txrdy & cpumask))
			goto ready1;
		udelay(100);
	}
	goto timeout;

delay2:
	for (timeout = 10000; timeout > 0; --timeout) {
		if (!(hwrpb->txrdy & cpumask))
			goto ready2;
		udelay(100);
	}
	goto timeout;

timeout:
	printk("Processor %x not ready\n", cpuid);
	return;
}

/*
 * setup_smp()
 *
 * called from arch/alpha/kernel/setup.c:setup_arch() when __SMP__ defined
 */
void __init
setup_smp(void)
{
	struct percpu_struct *cpubase, *cpu;
	int i;
	  
	boot_cpu_id = hard_smp_processor_id();
	if (boot_cpu_id != 0) {
		printk("setup_smp: boot_cpu_id != 0 (%d).\n", boot_cpu_id);
	}

	if (hwrpb->nr_processors > 1) {

		DBGS(("setup_smp: nr_processors %ld\n",
		      hwrpb->nr_processors));

		cpubase = (struct percpu_struct *)
			((char*)hwrpb + hwrpb->processor_offset);
		boot_cpu_palrev = cpubase->pal_revision;

		for (i = 0; i < hwrpb->nr_processors; i++ ) {
			cpu = (struct percpu_struct *)
				((char *)cpubase + i*hwrpb->processor_size);
			if ((cpu->flags & 0x1cc) == 0x1cc) {
				smp_num_probed++;
				/* assume here that "whami" == index */
				cpu_present_map |= (1 << i);
				if (i != boot_cpu_id)
				  cpu->pal_revision = boot_cpu_palrev;
			}

			DBGS(("setup_smp: CPU %d: flags 0x%lx type 0x%lx\n",
			      i, cpu->flags, cpu->type));
			DBGS(("setup_smp: CPU %d: PAL rev 0x%lx\n",
			      i, cpu->pal_revision));
		}
	} else {
		smp_num_probed = 1;
		cpu_present_map = (1 << boot_cpu_id);
	}
	printk("setup_smp: %d CPUs probed, cpu_present_map 0x%x,"
	       " boot_cpu_id %d\n",
	       smp_num_probed, cpu_present_map, boot_cpu_id);
}

static void
secondary_console_message(void)
{
	int mycpu, i, cnt;
	unsigned long txrdy = hwrpb->txrdy;
	char *cp1, *cp2, buf[80];
	struct percpu_struct *cpu;

	DBGS(("secondary_console_message: TXRDY 0x%lx.\n", txrdy));

	mycpu = hard_smp_processor_id();

	for (i = 0; i < NR_CPUS; i++) {
		if (!(txrdy & (1L << i)))
			continue;

		DBGS(("secondary_console_message: "
		      "TXRDY contains CPU %d.\n", i));

		cpu = (struct percpu_struct *)
		  ((char*)hwrpb
		   + hwrpb->processor_offset
		   + i * hwrpb->processor_size);

 		printk("secondary_console_message: on %d from %d"
		       " HALT_REASON 0x%lx FLAGS 0x%lx\n",
		       mycpu, i, cpu->halt_reason, cpu->flags);

		cnt = cpu->ipc_buffer[0] >> 32;
		if (cnt <= 0 || cnt >= 80)
			strcpy(buf, "<<< BOGUS MSG >>>");
		else {
			cp1 = (char *) &cpu->ipc_buffer[11];
			cp2 = buf;
			strcpy(cp2, cp1);
			
			while ((cp2 = strchr(cp2, '\r')) != 0) {
				*cp2 = ' ';
				if (cp2[1] == '\n')
					cp2[1] = ' ';
			}
		}

		printk("secondary_console_message: on %d message is '%s'\n",
		       mycpu, buf);
	}

	hwrpb->txrdy = 0;
}

enum ipi_message_type {
	IPI_TLB_ALL,
	IPI_TLB_MM,
	IPI_TLB_PAGE,
	IPI_RESCHEDULE,
	IPI_CPU_STOP
};

void
handle_ipi(struct pt_regs *regs)
{
	int this_cpu = smp_processor_id();
	volatile int * pending_ipis = &ipi_bits[this_cpu];
	unsigned long ops;

	DBGS(("handle_ipi: on CPU %d ops 0x%x PC 0x%lx\n",
	      this_cpu, *pending_ipis, regs->pc));

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
	  mb();	/* Order bit clearing and data access. */
	  do {
		unsigned long which;

		which = ops & -ops;
		ops &= ~which;
		which = ffz(~which);

		if (which < IPI_RESCHEDULE) {
			if (which == IPI_TLB_ALL)
				tbia();
			else if (which == IPI_TLB_MM) {
				struct mm_struct * mm;
				mm = ipi_msg_flush_tb.p.flush_mm;
				if (mm == current->mm)
					flush_tlb_current(mm);
			}
			else /* IPI_TLB_PAGE */ {
				struct vm_area_struct * vma;
				struct mm_struct * mm;
				unsigned long addr;

				vma = ipi_msg_flush_tb.p.flush_vma;
				mm = vma->vm_mm;
				addr = ipi_msg_flush_tb.flush_addr;

				if (mm == current->mm)
					flush_tlb_current_page(mm, vma, addr);
			}
			clear_bit(this_cpu, &ipi_msg_flush_tb.flush_tb_mask);
		}
		else if (which == IPI_RESCHEDULE) {
			/* Reschedule callback.  Everything to be done
			   is done by the interrupt return path.  */
		}
		else if (which == IPI_CPU_STOP) {
			halt();
		}
		else {
			printk(KERN_CRIT "unknown_ipi() on CPU %d: %lu\n",
			       this_cpu, which);
		}
	  } while (ops);
	  mb();	/* Order data access and bit testing. */
	}

	cpu_data[this_cpu].ipi_count++;

	if (hwrpb->txrdy)
		secondary_console_message();
}

static void
send_ipi_message(unsigned long to_whom, enum ipi_message_type operation)
{
	long i, j;

	/* Reduce the number of memory barriers by doing two loops,
	   one to set the bits, one to invoke the interrupts.  */

	mb();	/* Order out-of-band data and bit setting. */

	for (i = 0, j = 1; i < NR_CPUS; ++i, j <<= 1) {
		if (to_whom & j)
			set_bit(operation, &ipi_bits[i]);
	}

	mb();	/* Order bit setting and interrupt. */

	for (i = 0, j = 1; i < NR_CPUS; ++i, j <<= 1) {
		if (to_whom & j)
			wripir(i);
	}
}

int
smp_info(char *buffer)
{
	long i;
	unsigned long sum = 0;
	for (i = 0; i < NR_CPUS; i++)
		sum += cpu_data[i].ipi_count;

	return sprintf(buffer, "CPUs probed %d active %d map 0x%x IPIs %ld\n",
		       smp_num_probed, smp_num_cpus, cpu_present_map, sum);
}

void
smp_send_reschedule(int cpu)
{
	send_ipi_message(1 << cpu, IPI_RESCHEDULE);
}

void
smp_send_stop(void)
{
	unsigned long to_whom = cpu_present_map ^ (1 << smp_processor_id());
	send_ipi_message(to_whom, IPI_CPU_STOP);
}

void
flush_tlb_all(void)
{
	unsigned long to_whom = cpu_present_map ^ (1 << smp_processor_id());
	long timeout = 1000000;

	spin_lock_own(&kernel_flag, "flush_tlb_all");

	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	send_ipi_message(to_whom, IPI_TLB_ALL);
	tbia();

	while (ipi_msg_flush_tb.flush_tb_mask && --timeout) {
		udelay(1);
		barrier();
	}

	if (timeout == 0) {
		printk("flush_tlb_all: STUCK on CPU %d mask 0x%x\n",
		       smp_processor_id(),
		       ipi_msg_flush_tb.flush_tb_mask);
		ipi_msg_flush_tb.flush_tb_mask = 0;
	}
}

void
flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long to_whom = cpu_present_map ^ (1 << smp_processor_id());
	long timeout = 1000000;

	spin_lock_own(&kernel_flag, "flush_tlb_mm");

	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	ipi_msg_flush_tb.p.flush_mm = mm;
	send_ipi_message(to_whom, IPI_TLB_MM);

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current(mm);

	while (ipi_msg_flush_tb.flush_tb_mask && --timeout) {
		udelay(1);
		barrier();
	}

	if (timeout == 0) {
		printk("flush_tlb_mm: STUCK on CPU %d mask 0x%x\n",
		       smp_processor_id(),
		       ipi_msg_flush_tb.flush_tb_mask);
		ipi_msg_flush_tb.flush_tb_mask = 0;
	}
}

void
flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	int cpu = smp_processor_id();
	unsigned long to_whom = cpu_present_map ^ (1 << cpu);
	struct mm_struct * mm = vma->vm_mm;
	int timeout = 1000000;

	spin_lock_own(&kernel_flag, "flush_tlb_page");

	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	ipi_msg_flush_tb.p.flush_vma = vma;
	ipi_msg_flush_tb.flush_addr = addr;
	send_ipi_message(to_whom, IPI_TLB_PAGE);

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current_page(mm, vma, addr);

	while (ipi_msg_flush_tb.flush_tb_mask && --timeout) {
		udelay(1);
		barrier();
	}

	if (timeout == 0) {
		printk("flush_tlb_page: STUCK on CPU %d mask 0x%x\n",
		       smp_processor_id(),
		       ipi_msg_flush_tb.flush_tb_mask);
		ipi_msg_flush_tb.flush_tb_mask = 0;
	}
}

void
flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	/* On the Alpha we always flush the whole user tlb.  */
	flush_tlb_mm(mm);
}

#if DEBUG_SPINLOCK

#ifdef MANAGE_SPINLOCK_IPL

static inline long 
spinlock_raise_ipl(spinlock_t * lock)
{
 	long min_ipl = lock->target_ipl;
	long last_ipl = swpipl(7);
	if (last_ipl < 7 && min_ipl < 7)
		setipl(min_ipl < last_ipl ? last_ipl : min_ipl);
	return last_ipl;
}

static inline void
spinlock_restore_ipl(long prev)
{
	setipl(prev);
}

#else

#define spinlock_raise_ipl(LOCK)	((void)(LOCK), 0)
#define spinlock_restore_ipl(PREV)	((void)(PREV))

#endif /* MANAGE_SPINLOCK_IPL */

void
spin_unlock(spinlock_t * lock)
{
	long old_ipl = lock->saved_ipl;
	mb();
	lock->lock = 0;
	spinlock_restore_ipl(old_ipl);
}

void
spin_lock(spinlock_t * lock)
{
	long tmp;
	long stuck = 1<<27;
	void *inline_pc = __builtin_return_address(0);
	unsigned long started = jiffies;
	int printed = 0;
	int cpu = smp_processor_id();
	long old_ipl = spinlock_raise_ipl(lock);

 try_again:

	stuck = 0x10000000; /* was 4G, now 256M */

	/* Use sub-sections to put the actual loop at the end
	   of this object file's text section so as to perfect
	   branch prediction.  */
	__asm__ __volatile__(
	"1:	ldl_l	%0,%1\n"
	"	subq	%2,1,%2\n"
	"	blbs	%0,2f\n"
	"	or	%0,1,%0\n"
	"	stl_c	%0,%1\n"
	"	beq	%0,3f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"2:	ldl	%0,%1\n"
	"	subq	%2,1,%2\n"
	"3:	blt	%2,4b\n"
	"	blbs	%0,2b\n"
	"	br	1b\n"
	".previous"
	: "=r" (tmp),
	  "=m" (__dummy_lock(lock)),
	  "=r" (stuck)
	: "2" (stuck));

	if (stuck < 0) {
		if (!printed) {
			printk("spinlock stuck at %p(%d) owner %s at %p\n",
			       inline_pc, cpu, lock->task->comm,
			       lock->previous);
			printed = 1;
		}
		stuck = 1<<30;
		goto try_again;
	}

	/* Exiting.  Got the lock.  */
	lock->saved_ipl = old_ipl;
	lock->on_cpu = cpu;
	lock->previous = inline_pc;
	lock->task = current;

	if (printed) {
		printk("spinlock grabbed at %p(%d) %ld ticks\n",
		       inline_pc, cpu, jiffies - started);
	}
}

int
spin_trylock(spinlock_t * lock)
{
	long old_ipl = spinlock_raise_ipl(lock);
	int ret;
	if ((ret = !test_and_set_bit(0, lock))) {
		mb();
		lock->saved_ipl = old_ipl;
		lock->on_cpu = smp_processor_id();
		lock->previous = __builtin_return_address(0);
		lock->task = current;
	} else {
		spinlock_restore_ipl(old_ipl);
	}
	return ret;
}
#endif /* DEBUG_SPINLOCK */

#if DEBUG_RWLOCK
void write_lock(rwlock_t * lock)
{
	long regx, regy;
	int stuck_lock, stuck_reader;
	void *inline_pc = __builtin_return_address(0);

 try_again:

	stuck_lock = 1<<26;
	stuck_reader = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	blbs	%1,6f\n"
	"	blt	%1,8f\n"
	"	mov	1,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	blt	%3,4b	# debug\n"
	"	subl	%3,1,%3	# debug\n"
	"	ldl	%1,%0\n"
	"	blbs	%1,6b\n"
	"8:	blt	%4,4b	# debug\n"
	"	subl	%4,1,%4	# debug\n"
	"	ldl	%1,%0\n"
	"	blt	%1,8b\n"
	"	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (regy)
	, "=&r" (stuck_lock), "=&r" (stuck_reader)
	: "0" (__dummy_lock(lock))
	, "3" (stuck_lock), "4" (stuck_reader)
	);

	if (stuck_lock < 0) {
		printk("write_lock stuck at %p\n", inline_pc);
		goto try_again;
	}
	if (stuck_reader < 0) {
		printk("write_lock stuck on readers at %p\n", inline_pc);
		goto try_again;
	}
}

void read_lock(rwlock_t * lock)
{
	long regx;
	int stuck_lock;
	void *inline_pc = __builtin_return_address(0);

 try_again:

	stuck_lock = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	subl	%1,2,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	ldl	%1,%0;"
	"	blt	%2,4b	# debug\n"
	"	subl	%2,1,%2	# debug\n"
	"	blbs	%1,6b;"
	"	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (stuck_lock)
	: "0" (__dummy_lock(lock)), "2" (stuck_lock)
	);

	if (stuck_lock < 0) {
		printk("read_lock stuck at %p\n", inline_pc);
		goto try_again;
	}
}
#endif /* DEBUG_RWLOCK */
