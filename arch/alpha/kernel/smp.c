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

struct ipi_msg_flush_tb_struct ipi_msg_flush_tb;

struct cpuinfo_alpha cpu_data[NR_CPUS];

/* Processor holding kernel spinlock */
klock_info_t klock_info = { KLOCK_CLEAR, 0 };

spinlock_t ticker_lock = SPIN_LOCK_UNLOCKED;

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

unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];

volatile int ipi_bits[NR_CPUS];

unsigned long boot_cpu_palrev;

volatile int smp_commenced = 0;
volatile int smp_processors_ready = 0;

volatile int cpu_number_map[NR_CPUS];
volatile int cpu_logical_map[NR_CPUS];

extern int cpu_idle(void *unused);
extern void calibrate_delay(void);
extern struct hwrpb_struct *hwrpb;
extern struct thread_struct * original_pcb_ptr;
extern void __start_cpu(unsigned long);

static void smp_setup_percpu_timer(void);
static void secondary_cpu_start(int, struct task_struct *);
static void send_cpu_msg(char *, int);

/* process bootcommand SMP options, like "nosmp" and "maxcpus=" */
__initfunc(void smp_setup(char *str, int *ints))
{
	if (ints && ints[0] > 0)
		max_cpus = ints[1];
        else
		max_cpus = 0;
}

void smp_store_cpu_info(int id)
{
	/* This is it on Alpha, so far. */
        cpu_data[id].loops_per_sec = loops_per_sec;
}

void smp_commence(void)
{
	/* Lets the callin's below out of their loop. */
	mb();
	smp_commenced = 1;
}

void smp_callin(void)
{
        int cpuid = hard_smp_processor_id();

#if 0
	printk("CALLIN %d state 0x%lx\n", cpuid, current->state);
#endif
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
#endif /* NOT_YET */

#if 0
        /* Fix idle thread fields. */
        __asm__ __volatile__("ld [%0], %%g6\n\t"
                             : : "r" (&current_set[cpuid])
                             : "memory" /* paranoid */);
        current->mm->mmap->vm_page_prot = PAGE_SHARED;
        current->mm->mmap->vm_start = PAGE_OFFSET;
        current->mm->mmap->vm_end = init_task.mm->mmap->vm_end;
#endif
        
#ifdef HUH
        local_flush_cache_all();
        local_flush_tlb_all();
#endif
#if 0
        __sti();
#endif
}

asmlinkage int start_secondary(void *unused)
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
        return cpu_idle(NULL);
}

/*
 *      Cycle through the processors sending START msgs to boot each.
 */
void smp_boot_cpus(void)
{
        int cpucount = 0;
        int i, first, prev;

        printk("smp_boot_cpus: Entering SMP Mode...\n");

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
        klock_info.akp = boot_cpu_id;

        smp_store_cpu_info(boot_cpu_id);
#ifdef NOT_YET
        printk("CPU%d: ", boot_cpu_id);
        print_cpu_info(&cpu_data[boot_cpu_id]);
        set_irq_udt(mid_xlate[boot_cpu_id]);
#endif /* NOT_YET */
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
#endif /* NOT_YET */

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

#if 0
printk("smp_boot_cpus: CPU %d state 0x%lx flags 0x%lx\n",
       i, idle->state, idle->flags);
#endif

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

__initfunc(void ioapic_pirq_setup(char *str, int *ints))
{
  /* this is prolly INTEL-specific */
}

static void smp_setup_percpu_timer(void)
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

void smp_percpu_timer_interrupt(struct pt_regs *regs)
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

int setup_profiling_timer(unsigned int multiplier)
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

/* Only broken Intel needs this, thus it should not even be referenced globally.
*/
__initfunc(void initialize_secondary(void))
{
	printk("initialize_secondary: entry\n");
}

static void
secondary_cpu_start(int cpuid, struct task_struct *idle)
{
	struct percpu_struct *cpu;
        int timeout;
	  
	cpu = (struct percpu_struct *)
		((char*)hwrpb
			+ hwrpb->processor_offset
			+ cpuid * hwrpb->processor_size);

	/* set context to idle thread this CPU will use when running */
	/* assumption is that the idle thread is all set to go... ??? */
	memcpy(&cpu->hwpcb[0], &idle->tss, sizeof(struct pcb_struct));
	cpu->hwpcb[4] = cpu->hwpcb[0]; /* UNIQUE set to KSP ??? */
#if 0
printk("KSP 0x%lx PTBR 0x%lx VPTBR 0x%lx\n",
       cpu->hwpcb[0], cpu->hwpcb[2], hwrpb->vptb);
printk("Starting secondary cpu %d: state 0x%lx pal_flags 0x%lx\n",
       cpuid, idle->state, idle->tss.pal_flags);
#endif

	/* setup HWRPB fields that SRM uses to activate secondary CPU */
	 hwrpb->CPU_restart = __start_cpu;
	 hwrpb->CPU_restart_data = (unsigned long) idle;

	 /* recalculate and update the HWRPB checksum */
	 {
	   unsigned long sum, *lp1, *lp2;
	   sum = 0;
	   lp1 = (unsigned long *)hwrpb;
	   lp2 = &hwrpb->chksum;
	   while (lp1 < lp2)
	     sum += *lp1++;
	   *lp2 = sum;
	 }

	/*
	 * Send a "start" command to the specified processor.
	 */

	/* SRM III 3.4.1.3 */
	cpu->flags |= 0x22; /* turn on Context Valid and Restart Capable */
	cpu->flags &= ~1;/* turn off Bootstrap In Progress */
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
#if 0
	printk("secondary_cpu_start: SUCCESS for CPU %d!!!\n", cpuid);
#endif
}

static void
send_cpu_msg(char *str, int cpuid)
{
	struct percpu_struct *cpu;
        register char *cp1, *cp2;
        unsigned long cpumask;
        int timeout;

	  
	cpu = (struct percpu_struct *)
		((char*)hwrpb
			+ hwrpb->processor_offset
			+ cpuid * hwrpb->processor_size);

        cpumask = (1L << cpuid);
        for (timeout = 10000; (hwrpb->txrdy & cpumask); timeout--) {
                if (timeout <= 0) {
                        printk("Processor %x not ready\n", cpuid);
                        return;
                }
                mdelay(1);
        }

        cp1 = (char *) &cpu->ipc_buffer[1];
        cp2 = str;
        while (*cp2) *cp1++ = *cp2++;
        *(unsigned int *)&cpu->ipc_buffer[0] = cp2 - str; /* hack */

        /* atomic test and set */
        set_bit(cpuid, &hwrpb->rxrdy);

        for (timeout = 10000; (hwrpb->txrdy & cpumask); timeout--) {
                if (timeout <= 0) {
                        printk("Processor %x not ready\n", cpuid);
                        return;
                }
                mdelay(1);
        }
}

/*
 * setup_smp()
 *
 * called from arch/alpha/kernel/setup.c:setup_arch() when __SMP__ defined
 */
__initfunc(void setup_smp(void))
{
	struct percpu_struct *cpubase, *cpu;
	int i;
	  
	boot_cpu_id = hard_smp_processor_id();
	if (boot_cpu_id != 0) {
		printk("setup_smp: boot_cpu_id != 0 (%d).\n", boot_cpu_id);
	}

	if (hwrpb->nr_processors > 1) {
#if 0
printk("setup_smp: nr_processors 0x%lx\n",
       hwrpb->nr_processors);
#endif
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
#if 0
printk("setup_smp: CPU %d: flags 0x%lx type 0x%lx\n",
       i, cpu->flags, cpu->type);
 printk("setup_smp: CPU %d: PAL rev 0x%lx\n",
	i, cpu->pal_revision);
#endif
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

        mycpu = hard_smp_processor_id();

#if 0
printk("secondary_console_message: TXRDY 0x%lx.\n", txrdy);
#endif
	 for (i = 0; i < NR_CPUS; i++) {
	   if (txrdy & (1L << i)) {
#if 0
printk("secondary_console_message: TXRDY contains CPU %d.\n", i);
#endif
	     cpu = (struct percpu_struct *)
	       ((char*)hwrpb
		+ hwrpb->processor_offset
		+ i * hwrpb->processor_size);
#if 1
	     printk("secondary_console_message: on %d from %d"
		    " HALT_REASON 0x%lx FLAGS 0x%lx\n",
		    mycpu, i, cpu->halt_reason, cpu->flags);
#endif
	     cnt = cpu->ipc_buffer[0] >> 32;
	     if (cnt <= 0 || cnt >= 80)
	       strcpy(buf,"<<< BOGUS MSG >>>");
	     else {
	       cp1 = (char *) &cpu->ipc_buffer[11];
	       cp2 = buf;
	       while (cnt--) {
		 if (*cp1 == '\r' || *cp1 == '\n') {
		   *cp2++ = ' '; cp1++;
		 } else
		   *cp2++ = *cp1++;
	       }
	       *cp2 = 0;
	     }
#if 1
	     printk("secondary_console_message: on %d message is '%s'\n",
		    mycpu, buf);
#endif
	   }
		}
	 hwrpb->txrdy = 0;
	 return;
}

static int
halt_on_panic(unsigned int this_cpu)
{
	halt();
	return 0;
}

static int
local_flush_tlb_all(unsigned int this_cpu)
{
	tbia();
	clear_bit(this_cpu, &ipi_msg_flush_tb.flush_tb_mask);
	mb();
	return 0;
}

static int
local_flush_tlb_mm(unsigned int this_cpu)
{
	struct mm_struct * mm = ipi_msg_flush_tb.p.flush_mm;
	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current(mm);
	clear_bit(this_cpu, &ipi_msg_flush_tb.flush_tb_mask);
	mb();
	return 0;
}

static int
local_flush_tlb_page(unsigned int this_cpu)
{
	struct vm_area_struct * vma = ipi_msg_flush_tb.p.flush_vma;
	struct mm_struct * mm = vma->vm_mm;

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current_page(mm, vma, ipi_msg_flush_tb.flush_addr);
	clear_bit(this_cpu, &ipi_msg_flush_tb.flush_tb_mask);
	mb();
	return 0;
}

static int
wrapper_local_flush_tlb_page(unsigned int this_cpu)
{
#if 0
	int cpu = smp_processor_id();

	if (cpu) {
	  printk("wrapper: ipi_msg_flush_tb.flush_addr 0x%lx [%d]\n",
		 ipi_msg_flush_tb.flush_addr, atomic_read(&global_irq_count));
	}
#endif
	local_flush_tlb_page(this_cpu);
	return 0;
}

static int
unknown_ipi(unsigned int this_cpu)
{
	printk("unknown_ipi() on CPU %d:  ", this_cpu);
	return 1;
}

enum ipi_message_type {
  CPU_STOP,
  TLB_ALL,
  TLB_MM,
  TLB_PAGE,
  TLB_RANGE
};

static int (* ipi_func[32])(unsigned int) = {
  halt_on_panic,
  local_flush_tlb_all,
  local_flush_tlb_mm,
  wrapper_local_flush_tlb_page,
  local_flush_tlb_mm,		/* a.k.a. local_flush_tlb_range */
  unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi,
  unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi,
  unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi,
  unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi, unknown_ipi,
  unknown_ipi, unknown_ipi, unknown_ipi
};

void
handle_ipi(struct pt_regs *regs)
{
	int this_cpu = smp_processor_id();
	volatile int * pending_ipis = &ipi_bits[this_cpu];
	int ops;

	mb();
#if 0
	printk("handle_ipi: on CPU %d ops 0x%x PC 0x%lx\n",
	       this_cpu, *pending_ipis, regs->pc);
#endif
	while ((ops = *pending_ipis)) {
		int first;
		for (first = 0; (ops & 1) == 0; ++first, ops >>= 1)
			; /* look for the first thing to do */
		clear_bit(first, pending_ipis);
		mb();
		if ((*ipi_func[first])(this_cpu))
		  printk("%d\n", first);
		mb();
	}
	if (hwrpb->txrdy)
	  secondary_console_message();
}

void
send_ipi_message(long to_whom, enum ipi_message_type operation)
{
	int i;
	unsigned int j;

	for (i = 0, j = 1; i < NR_CPUS; ++i, j += j) {
		if ((to_whom & j) == 0)
			continue;
		set_bit(operation, &ipi_bits[i]);
		mb();
		wripir(i);
	}
}

static char smp_buf[256];

char *smp_info(void)
{
        sprintf(smp_buf, "CPUs probed %d active %d map 0x%x AKP %d\n",
		smp_num_probed, smp_num_cpus, cpu_present_map,
		klock_info.akp);

        return smp_buf;
}

/* wrapper for call from panic() */
void
smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	int me = smp_processor_id();

	if (msg != MSG_STOP_CPU)
		goto barf;

	send_ipi_message(CPU_STOP, cpu_present_map ^ (1 << me));
	return;
barf:
	printk("Yeeee, trying to send SMP msg(%d) on CPU %d\n", msg, me);
	panic("Bogon SMP message pass.");
}

void
flush_tlb_all(void)
{
	unsigned int to_whom = cpu_present_map ^ (1 << smp_processor_id());
	int timeout = 10000;

#if 1
	if (!kernel_lock_held()) {
	  printk("flush_tlb_all: kernel_flag %d (cpu %d akp %d)!\n",
		 klock_info.kernel_flag, smp_processor_id(), klock_info.akp);
	}
#endif
	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	send_ipi_message(to_whom, TLB_ALL);
	tbia();

	while (ipi_msg_flush_tb.flush_tb_mask) {
	  if (--timeout < 0) {
	    printk("flush_tlb_all: STUCK on CPU %d mask 0x%x\n",
		   smp_processor_id(), ipi_msg_flush_tb.flush_tb_mask);
	    ipi_msg_flush_tb.flush_tb_mask = 0;
	    break;
	  }
	  udelay(100);
		; /* Wait for all clear from other CPUs. */
	}
}

void
flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int to_whom = cpu_present_map ^ (1 << smp_processor_id());
	int timeout = 10000;

#if 1
	if (!kernel_lock_held()) {
	  printk("flush_tlb_mm: kernel_flag %d (cpu %d akp %d)!\n",
		 klock_info.kernel_flag, smp_processor_id(), klock_info.akp);
	}
#endif
	ipi_msg_flush_tb.p.flush_mm = mm;
	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	send_ipi_message(to_whom, TLB_MM);

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current(mm);

	while (ipi_msg_flush_tb.flush_tb_mask) {
	  if (--timeout < 0) {
	    printk("flush_tlb_mm: STUCK on CPU %d mask 0x%x\n",
		   smp_processor_id(), ipi_msg_flush_tb.flush_tb_mask);
	    ipi_msg_flush_tb.flush_tb_mask = 0;
	    break;
	  }
	  udelay(100);
		; /* Wait for all clear from other CPUs. */
	}
}

void
flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	int cpu = smp_processor_id();
	unsigned int to_whom = cpu_present_map ^ (1 << cpu);
	struct mm_struct * mm = vma->vm_mm;
	int timeout = 10000;

#if 1
	if (!kernel_lock_held()) {
	  printk("flush_tlb_page: kernel_flag %d (cpu %d akp %d)!\n",
		 klock_info.kernel_flag, cpu, klock_info.akp);
	}
#endif
	ipi_msg_flush_tb.p.flush_vma = vma;
	ipi_msg_flush_tb.flush_addr = addr;
	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	send_ipi_message(to_whom, TLB_PAGE);

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current_page(mm, vma, addr);

	while (ipi_msg_flush_tb.flush_tb_mask) {
	  if (--timeout < 0) {
	    printk("flush_tlb_page: STUCK on CPU %d [0x%x,0x%lx,%d,%d]\n",
		   cpu, ipi_msg_flush_tb.flush_tb_mask, addr,
		   klock_info.akp, global_irq_holder);
	    ipi_msg_flush_tb.flush_tb_mask = 0;
	    break;
	  }
	  udelay(100);
		; /* Wait for all clear from other CPUs. */
	}
}

void
flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#if 0
	flush_tlb_mm(mm);
#else
	unsigned int to_whom;
	int timeout;
        unsigned long where;

        __asm__("mov $26, %0" : "=r" (where));

	timeout = 10000;
	to_whom = cpu_present_map ^ (1 << smp_processor_id());

#if 1
	if (!kernel_lock_held()) {
	  printk("flush_tlb_range: kernel_flag %d (cpu %d akp %d) @ 0x%lx\n",
		 klock_info.kernel_flag, smp_processor_id(), klock_info.akp,
		 where);
	}
#endif
	ipi_msg_flush_tb.p.flush_mm = mm;
	ipi_msg_flush_tb.flush_tb_mask = to_whom;
	send_ipi_message(to_whom, TLB_MM);

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current(mm);

	while (ipi_msg_flush_tb.flush_tb_mask) {
	  if (--timeout < 0) {
	    printk("flush_tlb_range: STUCK on CPU %d mask 0x%x\n",
		   smp_processor_id(), ipi_msg_flush_tb.flush_tb_mask);
	    ipi_msg_flush_tb.flush_tb_mask = 0;
	    break;
	  }
	  udelay(100);
		; /* Wait for all clear from other CPUs. */
	}
#endif
}

#ifdef DEBUG_KERNEL_LOCK
void ___lock_kernel(klock_info_t *klip, int cpu, long ipl)
{
	long regx;
	int stuck_lock;
	unsigned long inline_pc;

        __asm__("mov $26, %0" : "=r" (inline_pc));

 try_again:

	stuck_lock = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	or	%1,1,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	mov	%5,$16;"
	"	call_pal %4;"
	"7:	ldl	%1,%0;"
	"	blt	%2,4b	# debug\n"
	"	subl	%2,1,%2	# debug\n"
	"	blbs	%1,7b;"
	"	bis	$31,7,$16;"
	"	call_pal %4;"
	"	br	1b\n"
	".previous"
	: "=m,=m" (__dummy_lock(klip)), "=&r,=&r" (regx),
	  "=&r,=&r" (stuck_lock)
	: "0,0" (__dummy_lock(klip)), "i,i" (PAL_swpipl),
	  "i,r" (ipl), "2,2" (stuck_lock)
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25", "memory");

	if (stuck_lock < 0) {
		printk("___kernel_lock stuck at %lx(%d) held %lx(%d)\n",
		       inline_pc, cpu, klip->pc, klip->cpu);
		goto try_again;
	} else {
		klip->pc = inline_pc;
		klip->cpu = cpu;
	}
}
#endif

#ifdef DEBUG_SPINLOCK
void spin_lock(spinlock_t * lock)
{
	long tmp;
	long stuck;
	unsigned long inline_pc;

        __asm__("mov $26, %0" : "=r" (inline_pc));

 try_again:

	stuck = 0x10000000; /* was 4G, now 256M */

	/* Use sub-sections to put the actual loop at the end
	   of this object file's text section so as to perfect
	   branch prediction.  */
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	subq	%2,1,%2\n"
	"	blbs	%0,2f\n"
	"	or	%0,1,%0\n"
	"	stq_c	%0,%1\n"
	"	beq	%0,3f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"2:	ldq	%0,%1\n"
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
		printk("spinlock stuck at %lx (cur=%lx, own=%lx)\n",
		       inline_pc,
#if 0
		       lock->previous, lock->task
#else
		       (unsigned long) current, lock->task
#endif
		       );
		goto try_again;
	} else {
		lock->previous = (unsigned long) inline_pc;
		lock->task = (unsigned long) current;
	}
}
#endif /* DEBUG_SPINLOCK */

#ifdef DEBUG_RWLOCK
void write_lock(rwlock_t * lock)
{
	long regx, regy;
	int stuck_lock, stuck_reader;
	unsigned long inline_pc;

        __asm__("mov $26, %0" : "=r" (inline_pc));

 try_again:

	stuck_lock = 1<<26;
	stuck_reader = 1<<26;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	or	%1,1,%2;"
	"	stl_c	%2,%0;"
	"	beq	%2,6f;"
	"	blt	%1,8f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	ldl	%1,%0;"
	"	blt	%3,4b	# debug\n"
	"	subl	%3,1,%3	# debug\n"
	"	blbs	%1,6b;"
	"	br	1b;"
	"8:	ldl	%1,%0;"
	"	blt	%4,4b	# debug\n"
	"	subl	%4,1,%4	# debug\n"
	"	blt	%1,8b;"
	"9:	br	4b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (regy)
	, "=&r" (stuck_lock), "=&r" (stuck_reader)
	: "0" (__dummy_lock(lock))
	, "3" (stuck_lock), "4" (stuck_reader)
	);

	if (stuck_lock < 0) {
		printk("write_lock stuck at %lx\n", inline_pc);
		goto try_again;
	}
	if (stuck_reader < 0) {
		printk("write_lock stuck on readers at %lx\n", inline_pc);
		goto try_again;
	}
}

void _read_lock(rwlock_t * lock)
{
	long regx;
	int stuck_lock;
	unsigned long inline_pc;

        __asm__("mov $26, %0" : "=r" (inline_pc));

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
		printk("_read_lock stuck at %lx\n", inline_pc);
		goto try_again;
	}
}
#endif /* DEBUG_RWLOCK */
