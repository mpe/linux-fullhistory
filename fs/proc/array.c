/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 * Fixes:
 * Michael. K. Johnson: stat,statm extensions.
 *                      <johnsonm@stolaf.edu>
 *
 * Pauline Middelink :  Made cmdline,envline only break at '\0's, to
 *                      make sure SET_PROCTITLE works. Also removed
 *                      bad '!' which forced address recalculation for
 *                      EVERY character on the current page.
 *                      <middelin@polyware.iaf.nl>
 *
 * Danny ter Haar    :	added cpuinfo
 *			<dth@cistron.nl>
 *
 * Alessandro Rubini :  profile extension.
 *                      <rubini@ipvvis.unipv.it>
 *
 * Jeff Tranter      :  added BogoMips field to cpuinfo
 *                      <Jeff_Tranter@Mitel.COM>
 *
 * Bruno Haible      :  remove 4K limit for the maps file
 * 			<haible@ma2s2.mathematik.uni-karlsruhe.de>
 *
 * Yves Arrouye      :  remove removal of trailing spaces in get_array.
 *			<Yves.Arrouye@marin.fdn.fr>
 *
 * Jerome Forissier  :  added per-CPU time information to /proc/stat
 *                      and /proc/<pid>/cpu extension
 *                      <forissier@isia.cma.fr>
 *			- Incorporation and non-SMP safe operation
 *			of forissier patch in 2.1.78 by
 *			Hans Marcus <crowbar@concepts.nl>
 *
 * aeb@cwi.nl        :  /proc/partitions
 *
 *
 * Alan Cox	     :  security fixes.
 *			<Alan.Cox@linux.org>
 *
 * Andi Kleen	     :  Race Fixes. 	
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

#ifdef CONFIG_DEBUG_MALLOC
int get_malloc(char * buffer);
#endif


static ssize_t read_core(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos, memsize;
	ssize_t read;
	ssize_t count1;
	char * pnt;
	struct user dump;
#if defined (__i386__) || defined (__mc68000__)
#	define FIRST_MAPPED	PAGE_SIZE	/* we don't have page 0 mapped on x86.. */
#else
#	define FIRST_MAPPED	0
#endif

	memset(&dump, 0, sizeof(struct user));
	dump.magic = CMAGIC;
	dump.u_dsize = max_mapnr;
#ifdef __alpha__
	dump.start_data = PAGE_OFFSET;
#endif

	memsize = (max_mapnr + 1) << PAGE_SHIFT;
	if (p >= memsize)
		return 0;
	if (count > memsize - p)
		count = memsize - p;
	read = 0;

	if (p < sizeof(struct user) && count > 0) {
		count1 = count;
		if (p + count1 > sizeof(struct user))
			count1 = sizeof(struct user)-p;
		pnt = (char *) &dump + p;
		copy_to_user(buf,(void *) pnt, count1);
		buf += count1;
		p += count1;
		count -= count1;
		read += count1;
	}

	if (count > 0 && p < PAGE_SIZE + FIRST_MAPPED) {
		count1 = PAGE_SIZE + FIRST_MAPPED - p;
		if (count1 > count)
			count1 = count;
		clear_user(buf, count1);
		buf += count1;
		p += count1;
		count -= count1;
		read += count1;
	}
	if (count > 0) {
		copy_to_user(buf, (void *) (PAGE_OFFSET+p-PAGE_SIZE), count);
		read += count;
	}
	*ppos += read;
	return read;
}

static struct file_operations proc_kcore_operations = {
	NULL,           /* lseek */
	read_core,
};

struct inode_operations proc_kcore_inode_operations = {
	&proc_kcore_operations,
};

/*
 * This function accesses profiling information. The returned data is
 * binary: the sampling step and the actual contents of the profile
 * buffer. Use of the program readprofile is recommended in order to
 * get meaningful info out of these data.
 */
static ssize_t read_profile(struct file *file, char *buf,
			    size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read;
	char * pnt;
	unsigned int sample_step = 1 << prof_shift;

	if (p >= (prof_len+1)*sizeof(unsigned int))
		return 0;
	if (count > (prof_len+1)*sizeof(unsigned int) - p)
		count = (prof_len+1)*sizeof(unsigned int) - p;
	read = 0;

	while (p < sizeof(unsigned int) && count > 0) {
		put_user(*((char *)(&sample_step)+p),buf);
		buf++; p++; count--; read++;
	}
	pnt = (char *)prof_buffer + p - sizeof(unsigned int);
	copy_to_user(buf,(void *)pnt,count);
	read += count;
	*ppos += read;
	return read;
}

/*
 * Writing to /proc/profile resets the counters
 *
 * Writing a 'profiling multiplier' value into it also re-sets the profiling
 * interrupt frequency, on architectures that support this.
 */
static ssize_t write_profile(struct file * file, const char * buf,
			     size_t count, loff_t *ppos)
{
#ifdef __SMP__
	extern int setup_profiling_timer (unsigned int multiplier);

	if (count==sizeof(int)) {
		unsigned int multiplier;

		if (copy_from_user(&multiplier, buf, sizeof(int)))
			return -EFAULT;

		if (setup_profiling_timer(multiplier))
			return -EINVAL;
	}
#endif

	memset(prof_buffer, 0, prof_len * sizeof(*prof_buffer));
	return count;
}

static struct file_operations proc_profile_operations = {
	NULL,           /* lseek */
	read_profile,
	write_profile,
};

struct inode_operations proc_profile_inode_operations = {
	&proc_profile_operations,
};


static int get_loadavg(char * buffer)
{
	int a, b, c;

	a = avenrun[0] + (FIXED_1/200);
	b = avenrun[1] + (FIXED_1/200);
	c = avenrun[2] + (FIXED_1/200);
	return sprintf(buffer,"%d.%02d %d.%02d %d.%02d %d/%d %d\n",
		LOAD_INT(a), LOAD_FRAC(a),
		LOAD_INT(b), LOAD_FRAC(b),
		LOAD_INT(c), LOAD_FRAC(c),
		nr_running, nr_tasks, last_pid);
}

static int get_kstat(char * buffer)
{
	int i, len;
	unsigned sum = 0;
	extern unsigned long total_forks;
	unsigned long ticks;

	ticks = jiffies * smp_num_cpus;
	for (i = 0 ; i < NR_IRQS ; i++)
		sum += kstat_irqs(i);

#ifdef __SMP__
	len = sprintf(buffer,
		"cpu  %u %u %u %lu\n",
		kstat.cpu_user,
		kstat.cpu_nice,
		kstat.cpu_system,
		jiffies*smp_num_cpus - (kstat.cpu_user + kstat.cpu_nice + kstat.cpu_system));
	for (i = 0 ; i < smp_num_cpus; i++)
		len += sprintf(buffer + len, "cpu%d %u %u %u %lu\n",
			i,
			kstat.per_cpu_user[cpu_logical_map(i)],
			kstat.per_cpu_nice[cpu_logical_map(i)],
			kstat.per_cpu_system[cpu_logical_map(i)],
			jiffies - (  kstat.per_cpu_user[cpu_logical_map(i)] \
			           + kstat.per_cpu_nice[cpu_logical_map(i)] \
			           + kstat.per_cpu_system[cpu_logical_map(i)]));
	len += sprintf(buffer + len,
		"disk %u %u %u %u\n"
		"disk_rio %u %u %u %u\n"
		"disk_wio %u %u %u %u\n"
		"disk_rblk %u %u %u %u\n"
		"disk_wblk %u %u %u %u\n"
		"page %u %u\n"
		"swap %u %u\n"
		"intr %u",
#else
	len = sprintf(buffer,
		"cpu  %u %u %u %lu\n"
		"disk %u %u %u %u\n"
		"disk_rio %u %u %u %u\n"
		"disk_wio %u %u %u %u\n"
		"disk_rblk %u %u %u %u\n"
		"disk_wblk %u %u %u %u\n"
		"page %u %u\n"
		"swap %u %u\n"
		"intr %u",
		kstat.cpu_user,
		kstat.cpu_nice,
		kstat.cpu_system,
		ticks - (kstat.cpu_user + kstat.cpu_nice + kstat.cpu_system),
#endif
		kstat.dk_drive[0], kstat.dk_drive[1],
		kstat.dk_drive[2], kstat.dk_drive[3],
		kstat.dk_drive_rio[0], kstat.dk_drive_rio[1],
		kstat.dk_drive_rio[2], kstat.dk_drive_rio[3],
		kstat.dk_drive_wio[0], kstat.dk_drive_wio[1],
		kstat.dk_drive_wio[2], kstat.dk_drive_wio[3],
		kstat.dk_drive_rblk[0], kstat.dk_drive_rblk[1],
		kstat.dk_drive_rblk[2], kstat.dk_drive_rblk[3],
		kstat.dk_drive_wblk[0], kstat.dk_drive_wblk[1],
		kstat.dk_drive_wblk[2], kstat.dk_drive_wblk[3],
		kstat.pgpgin,
		kstat.pgpgout,
		kstat.pswpin,
		kstat.pswpout,
		sum);
	for (i = 0 ; i < NR_IRQS ; i++)
		len += sprintf(buffer + len, " %u", kstat_irqs(i));
	len += sprintf(buffer + len,
		"\nctxt %u\n"
		"btime %lu\n"
		"processes %lu\n",
		kstat.context_swtch,
		xtime.tv_sec - jiffies / HZ,
		total_forks);
	return len;
}


static int get_uptime(char * buffer)
{
	unsigned long uptime;
	unsigned long idle;

	uptime = jiffies;
	idle = task[0]->times.tms_utime + task[0]->times.tms_stime;

	/* The formula for the fraction parts really is ((t * 100) / HZ) % 100, but
	   that would overflow about every five days at HZ == 100.
	   Therefore the identity a = (a / b) * b + a % b is used so that it is
	   calculated as (((t / HZ) * 100) + ((t % HZ) * 100) / HZ) % 100.
	   The part in front of the '+' always evaluates as 0 (mod 100). All divisions
	   in the above formulas are truncating. For HZ being a power of 10, the
	   calculations simplify to the version in the #else part (if the printf
	   format is adapted to the same number of digits as zeroes in HZ.
	 */
#if HZ!=100
	return sprintf(buffer,"%lu.%02lu %lu.%02lu\n",
		uptime / HZ,
		(((uptime % HZ) * 100) / HZ) % 100,
		idle / HZ,
		(((idle % HZ) * 100) / HZ) % 100);
#else
	return sprintf(buffer,"%lu.%02lu %lu.%02lu\n",
		uptime / HZ,
		uptime % HZ,
		idle / HZ,
		idle % HZ);
#endif
}

static int get_meminfo(char * buffer)
{
	struct sysinfo i;
	int len;

	si_meminfo(&i);
	si_swapinfo(&i);
	len = sprintf(buffer, "        total:    used:    free:  shared: buffers:  cached:\n"
		"Mem:  %8lu %8lu %8lu %8lu %8lu %8lu\n"
		"Swap: %8lu %8lu %8lu\n",
		i.totalram, i.totalram-i.freeram, i.freeram, i.sharedram, i.bufferram, page_cache_size*PAGE_SIZE,
		i.totalswap, i.totalswap-i.freeswap, i.freeswap);
	/*
	 * Tagged format, for easy grepping and expansion. The above will go away
	 * eventually, once the tools have been updated.
	 */
	return len + sprintf(buffer+len,
		"MemTotal:  %8lu kB\n"
		"MemFree:   %8lu kB\n"
		"MemShared: %8lu kB\n"
		"Buffers:   %8lu kB\n"
		"Cached:    %8lu kB\n"
		"SwapTotal: %8lu kB\n"
		"SwapFree:  %8lu kB\n",
		i.totalram >> 10,
		i.freeram >> 10,
		i.sharedram >> 10,
		i.bufferram >> 10,
		page_cache_size << (PAGE_SHIFT - 10),
		i.totalswap >> 10,
		i.freeswap >> 10);
}

static int get_version(char * buffer)
{
	extern char *linux_banner;

	strcpy(buffer, linux_banner);
	return strlen(buffer);
}

static int get_cmdline(char * buffer)
{
	extern char saved_command_line[];

	return sprintf(buffer, "%s\n", saved_command_line);
}

/*
 * Caller must release_mm the mm_struct later.
 * You don't get any access to init_mm.
 */
static struct mm_struct *get_mm_and_lock(int pid)
{
	struct mm_struct *mm = NULL;
	struct task_struct *tsk;

	read_lock(&tasklist_lock);
	tsk = find_task_by_pid(pid);
	if (tsk && tsk->mm && tsk->mm != &init_mm)
		mmget(mm = tsk->mm);
	read_unlock(&tasklist_lock);
	if (mm != NULL)
		down(&mm->mmap_sem);
	return mm;
}

static void release_mm(struct mm_struct *mm)
{
	up(&mm->mmap_sem);
	mmput(mm);
}

static unsigned long get_phys_addr(struct mm_struct *mm, unsigned long ptr)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;

	if (ptr >= TASK_SIZE)
		return 0;
	/* Check for NULL pgd .. shouldn't happen! */
	if (!mm->pgd) {
		printk(KERN_DEBUG "missing pgd for mm %p\n", mm);
		return 0;
	}

	page_dir = pgd_offset(mm,ptr);
	if (pgd_none(*page_dir))
		return 0;
	if (pgd_bad(*page_dir)) {
		printk("bad page directory entry %08lx\n", pgd_val(*page_dir));
		pgd_clear(page_dir);
		return 0;
	}
	page_middle = pmd_offset(page_dir,ptr);
	if (pmd_none(*page_middle))
		return 0;
	if (pmd_bad(*page_middle)) {
		printk("bad page middle entry %08lx\n", pmd_val(*page_middle));
		pmd_clear(page_middle);
		return 0;
	}
	pte = *pte_offset(page_middle,ptr);
	if (!pte_present(pte))
		return 0;
	return pte_page(pte) + (ptr & ~PAGE_MASK);
}

static int get_array(struct mm_struct *mm, unsigned long start, unsigned long end, char * buffer)
{
	unsigned long addr;
	int size = 0, result = 0;
	char c;

	if (start >= end)
		return result;
	for (;;) {
		addr = get_phys_addr(mm, start);
		if (!addr)
			return result;
		do {
			c = *(char *) addr;
			if (!c)
				result = size;
			if (size < PAGE_SIZE)
				buffer[size++] = c;
			else
				return result;
			addr++;
			start++;
			if (!c && start >= end)
				return result;
		} while (addr & ~PAGE_MASK);
	}
	return result;
}

static int get_env(int pid, char * buffer)
{
	struct mm_struct *mm;
	int res = 0;

	mm = get_mm_and_lock(pid);
	if (mm) {
		res = get_array(mm, mm->env_start, mm->env_end, buffer);
		release_mm(mm);
	}
	return res;
}

static int get_arg(int pid, char * buffer)
{
	struct mm_struct *mm;
	int res = 0;

	mm = get_mm_and_lock(pid);
	if (mm) {
		res = get_array(mm, mm->arg_start, mm->arg_end, buffer);
		release_mm(mm);
	}
	return res;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

static unsigned long get_wchan(struct task_struct *p)
{
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
#if defined(__i386__)
	{
		unsigned long ebp, esp, eip;
		unsigned long stack_page;
		int count = 0;

		stack_page = (unsigned long)p;
		esp = p->tss.esp;
		if (!stack_page || esp < stack_page || esp >= 8188+stack_page)
			return 0;
		/* include/asm-i386/system.h:switch_to() pushes ebp last. */
		ebp = *(unsigned long *) esp;
		do {
			if (ebp < stack_page || ebp >= 8188+stack_page)
				return 0;
			eip = *(unsigned long *) (ebp+4);
			if (eip < first_sched || eip >= last_sched)
				return eip;
			ebp = *(unsigned long *) ebp;
		} while (count++ < 16);
	}
#elif defined(__alpha__)
	/*
	 * This one depends on the frame size of schedule().  Do a
	 * "disass schedule" in gdb to find the frame size.  Also, the
	 * code assumes that sleep_on() follows immediately after
	 * interruptible_sleep_on() and that add_timer() follows
	 * immediately after interruptible_sleep().  Ugly, isn't it?
	 * Maybe adding a wchan field to task_struct would be better,
	 * after all...
	 */
	{
	    unsigned long schedule_frame;
	    unsigned long pc;

	    pc = thread_saved_pc(&p->tss);
	    if (pc >= first_sched && pc < last_sched) {
		schedule_frame = ((unsigned long *)p->tss.ksp)[6];
		return ((unsigned long *)schedule_frame)[12];
	    }
	    return pc;
	}	
#elif defined(__mc68000__)
	{
	    unsigned long fp, pc;
	    unsigned long stack_page;
	    int count = 0;

	    stack_page = (unsigned long)p;
	    fp = ((struct switch_stack *)p->tss.ksp)->a6;
	    do {
		    if (fp < stack_page+sizeof(struct task_struct) ||
			fp >= 8184+stack_page)
			    return 0;
		    pc = ((unsigned long *)fp)[1];
		/* FIXME: This depends on the order of these functions. */
		    if (pc < first_sched || pc >= last_sched)
		      return pc;
		    fp = *(unsigned long *) fp;
	    } while (count++ < 16);
	}
#elif defined(__powerpc__)
	return (p->tss.wchan);
#elif defined (CONFIG_ARM)
	{
		unsigned long fp, lr;
		unsigned long stack_page;
		int count = 0;

		stack_page = 4096 + (unsigned long)p;
		fp = get_css_fp (&p->tss);
		do {
			if (fp < stack_page || fp > 4092+stack_page)
				return 0;
			lr = pc_pointer (((unsigned long *)fp)[-1]);
			if (lr < first_sched || lr > last_sched)
				return lr;
			fp = *(unsigned long *) (fp - 12);
		} while (count ++ < 16);
	}
#elif defined (__sparc__)
	{
		unsigned long pc, fp, bias = 0;
		unsigned long task_base = (unsigned long) p;
		struct reg_window *rw;
		int count = 0;

#ifdef __sparc_v9__
		bias = STACK_BIAS;
#endif
	    	fp = p->tss.ksp + bias;
		do {
			/* Bogus frame pointer? */
			if (fp < (task_base + sizeof(struct task_struct)) ||
			    fp >= (task_base + (2 * PAGE_SIZE)))
				break;
			rw = (struct reg_window *) fp;
			pc = rw->ins[7];
			if (pc < first_sched || pc >= last_sched)
				return pc;
			fp = rw->ins[6] + bias;
		} while (++count < 16);
	}
#endif
	return 0;
}

#if defined(__i386__)
# define KSTK_EIP(tsk)	(((unsigned long *)(4096+(unsigned long)(tsk)))[1019])
# define KSTK_ESP(tsk)	(((unsigned long *)(4096+(unsigned long)(tsk)))[1022])
#elif defined(__alpha__)
  /*
   * See arch/alpha/kernel/ptrace.c for details.
   */
# define PT_REG(reg)		(PAGE_SIZE - sizeof(struct pt_regs)	\
				 + (long)&((struct pt_regs *)0)->reg)
# define KSTK_EIP(tsk) \
    (*(unsigned long *)(PT_REG(pc) + PAGE_SIZE + (unsigned long)(tsk)))
# define KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->tss.usp)
#elif defined(CONFIG_ARM)
# define KSTK_EIP(tsk)	(((unsigned long *)(4096+(unsigned long)(tsk)))[1022])
# define KSTK_ESP(tsk)	(((unsigned long *)(4096+(unsigned long)(tsk)))[1020])
#elif defined(__mc68000__)
#define	KSTK_EIP(tsk)	\
    ({			\
	unsigned long eip = 0;	 \
 	if ((tsk)->tss.esp0 > PAGE_SIZE && \
	    MAP_NR((tsk)->tss.esp0) < max_mapnr) \
	      eip = ((struct pt_regs *) (tsk)->tss.esp0)->pc;	 \
	eip; })
#define	KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->tss.usp)
#elif defined(__powerpc__)
#define KSTK_EIP(tsk)	((tsk)->tss.regs->nip)
#define KSTK_ESP(tsk)	((tsk)->tss.regs->gpr[1])
#elif defined (__sparc_v9__)
# define KSTK_EIP(tsk)  ((tsk)->tss.kregs->tpc)
# define KSTK_ESP(tsk)  ((tsk)->tss.kregs->u_regs[UREG_FP])
#elif defined(__sparc__)
# define KSTK_EIP(tsk)  ((tsk)->tss.kregs->pc)
# define KSTK_ESP(tsk)  ((tsk)->tss.kregs->u_regs[UREG_FP])
#endif

/* Gcc optimizes away "strlen(x)" for constant x */
#define ADDBUF(buffer, string) \
do { memcpy(buffer, string, strlen(string)); \
     buffer += strlen(string); } while (0)

static inline char * task_name(struct task_struct *p, char * buf)
{
	int i;
	char * name;

	ADDBUF(buf, "Name:\t");
	name = p->comm;
	i = sizeof(p->comm);
	do {
		unsigned char c = *name;
		name++;
		i--;
		*buf = c;
		if (!c)
			break;
		if (c == '\\') {
			buf[1] = c;
			buf += 2;
			continue;
		}
		if (c == '\n') {
			buf[0] = '\\';
			buf[1] = 'n';
			buf += 2;
			continue;
		}
		buf++;
	} while (i);
	*buf = '\n';
	return buf+1;
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char *task_state_array[] = {
	"R (running)",		/*  0 */
	"S (sleeping)",		/*  1 */
	"D (disk sleep)",	/*  2 */
	"Z (zombie)",		/*  4 */
	"T (stopped)",		/*  8 */
	"W (paging)"		/* 16 */
};

static inline const char * get_task_state(struct task_struct *tsk)
{
	unsigned int state = tsk->state & (TASK_RUNNING |
					   TASK_INTERRUPTIBLE |
					   TASK_UNINTERRUPTIBLE |
					   TASK_ZOMBIE |
					   TASK_STOPPED |
					   TASK_SWAPPING);
	const char **p = &task_state_array[0];

	while (state) {
		p++;
		state >>= 1;
	}
	return *p;
}

static inline char * task_state(struct task_struct *p, char *buffer)
{
	int g;

	buffer += sprintf(buffer,
		"State:\t%s\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n"
		"Groups:\t",
		get_task_state(p),
		p->pid, p->p_pptr->pid,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);

	for (g = 0; g < p->ngroups; g++)
		buffer += sprintf(buffer, "%d ", p->groups[g]);

	buffer += sprintf(buffer, "\n");
	return buffer;
}

static inline char * task_mem(struct task_struct *p, char *buffer)
{
	struct mm_struct * mm = p->mm;

	if (!mm)
		return buffer;
	if (mm != &init_mm) {
		struct vm_area_struct * vma;
		unsigned long data = 0, stack = 0;
		unsigned long exec = 0, lib = 0;

		down(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			unsigned long len = (vma->vm_end - vma->vm_start) >> 10;
			if (!vma->vm_file) {
				data += len;
				if (vma->vm_flags & VM_GROWSDOWN)
					stack += len;
				continue;
			}
			if (vma->vm_flags & VM_WRITE)
				continue;
			if (vma->vm_flags & VM_EXEC) {
				exec += len;
				if (vma->vm_flags & VM_EXECUTABLE)
					continue;
				lib += len;
			}
		}	
		up(&mm->mmap_sem);
		buffer += sprintf(buffer,
			"VmSize:\t%8lu kB\n"
			"VmLck:\t%8lu kB\n"
			"VmRSS:\t%8lu kB\n"
			"VmData:\t%8lu kB\n"
			"VmStk:\t%8lu kB\n"
			"VmExe:\t%8lu kB\n"
			"VmLib:\t%8lu kB\n",
			mm->total_vm << (PAGE_SHIFT-10),
			mm->locked_vm << (PAGE_SHIFT-10),
			mm->rss << (PAGE_SHIFT-10),
			data - stack, stack,
			exec - lib, lib);
	}
	return buffer;
}

static void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
				    sigset_t *catch)
{
	struct k_sigaction *k;
	int i;

	sigemptyset(ign);
	sigemptyset(catch);

	if (p->sig) {
		k = p->sig->action;
		for (i = 1; i <= _NSIG; ++i, ++k) {
			if (k->sa.sa_handler == SIG_IGN)
				sigaddset(ign, i);
			else if (k->sa.sa_handler != SIG_DFL)
				sigaddset(catch, i);
		}
	}
}

static inline char * task_sig(struct task_struct *p, char *buffer)
{
	sigset_t ign, catch;

	buffer += sprintf(buffer, "SigPnd:\t");
	buffer = render_sigset_t(&p->signal, buffer);
	*buffer++ = '\n';
	buffer += sprintf(buffer, "SigBlk:\t");
	buffer = render_sigset_t(&p->blocked, buffer);
	*buffer++ = '\n';

	collect_sigign_sigcatch(p, &ign, &catch);
	buffer += sprintf(buffer, "SigIgn:\t");
	buffer = render_sigset_t(&ign, buffer);
	*buffer++ = '\n';
	buffer += sprintf(buffer, "SigCgt:\t"); /* Linux 2.0 uses "SigCgt" */
	buffer = render_sigset_t(&catch, buffer);
	*buffer++ = '\n';

	return buffer;
}

extern inline char *task_cap(struct task_struct *p, char *buffer)
{
    return buffer + sprintf(buffer, "CapInh:\t%016x\n"
			    "CapPrm:\t%016x\n"
			    "CapEff:\t%016x\n",
			    cap_t(p->cap_inheritable),
			    cap_t(p->cap_permitted),
			    cap_t(p->cap_effective));
}

static struct task_struct *grab_task(int pid)
{
	struct task_struct *tsk = current;
	if (pid != tsk->pid) {
		read_lock(&tasklist_lock);
		tsk = find_task_by_pid(pid);
		if (tsk && tsk->mm && tsk->mm != &init_mm)
			mmget(tsk->mm);
		read_unlock(&tasklist_lock);
	}	
	return tsk;
}

static void release_task(struct task_struct *tsk)
{
	if (tsk != current && tsk->mm && tsk->mm != &init_mm)
		mmput(tsk->mm);
}

static int get_status(int pid, char * buffer)
{
	char * orig = buffer;
	struct task_struct *tsk;
	
	tsk = grab_task(pid);
	if (!tsk)
		return 0;
	buffer = task_name(tsk, buffer);
	buffer = task_state(tsk, buffer);
	buffer = task_mem(tsk, buffer);
	buffer = task_sig(tsk, buffer);
	buffer = task_cap(tsk, buffer);
	release_task(tsk);
	return buffer - orig;
}

static int get_stat(int pid, char * buffer)
{
	struct task_struct *tsk;
	unsigned long vsize, eip, esp, wchan;
	long priority, nice;
	int tty_pgrp;
	sigset_t sigign, sigcatch;
	char state;
	int res;

	tsk = grab_task(pid);
	if (!tsk)
		return 0;
	state = *get_task_state(tsk);
	vsize = eip = esp = 0;
	if (tsk->mm && tsk->mm != &init_mm) {
		struct vm_area_struct *vma;

		down(&tsk->mm->mmap_sem);
		for (vma = tsk->mm->mmap; vma; vma = vma->vm_next) {
			vsize += vma->vm_end - vma->vm_start;
		}
		up(&tsk->mm->mmap_sem);
		
		eip = KSTK_EIP(tsk);
		esp = KSTK_ESP(tsk);
	}

	wchan = get_wchan(tsk);

	collect_sigign_sigcatch(tsk, &sigign, &sigcatch);

	if (tsk->tty)
		tty_pgrp = tsk->tty->pgrp;
	else
		tty_pgrp = -1;

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = tsk->counter;
	priority = 20 - (priority * 10 + DEF_PRIORITY / 2) / DEF_PRIORITY;
	nice = tsk->priority;
	nice = 20 - (nice * 20 + DEF_PRIORITY / 2) / DEF_PRIORITY;

	res = sprintf(buffer,"%d (%s) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d\n",
		pid,
		tsk->comm,
		state,
		tsk->p_pptr->pid,
		tsk->pgrp,
		tsk->session,
	        tsk->tty ? kdev_t_to_nr(tsk->tty->device) : 0,
		tty_pgrp,
		tsk->flags,
		tsk->min_flt,
		tsk->cmin_flt,
		tsk->maj_flt,
		tsk->cmaj_flt,
		tsk->times.tms_utime,
		tsk->times.tms_stime,
		tsk->times.tms_cutime,
		tsk->times.tms_cstime,
		priority,
		nice,
		0UL /* removed */,
		tsk->it_real_value,
		tsk->start_time,
		vsize,
		tsk->mm ? tsk->mm->rss : 0, /* you might want to shift this left 3 */
		tsk->rlim ? tsk->rlim[RLIMIT_RSS].rlim_cur : 0,
		tsk->mm ? tsk->mm->start_code : 0,
		tsk->mm ? tsk->mm->end_code : 0,
		tsk->mm ? tsk->mm->start_stack : 0,
		esp,
		eip,
		/* The signal information here is obsolete.
		 * It must be decimal for Linux 2.0 compatibility.
		 * Use /proc/#/status for real-time signals.
		 */
		tsk->signal .sig[0] & 0x7fffffffUL,
		tsk->blocked.sig[0] & 0x7fffffffUL,
		sigign      .sig[0] & 0x7fffffffUL,
		sigcatch    .sig[0] & 0x7fffffffUL,
		wchan,
		tsk->nswap,
		tsk->cnswap,
		tsk->exit_signal);

	release_task(tsk);
	return res;
}
		
static inline void statm_pte_range(pmd_t * pmd, unsigned long address, unsigned long size,
	int * pages, int * shared, int * dirty, int * total)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("statm_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t page = *pte;

		address += PAGE_SIZE;
		pte++;
		if (pte_none(page))
			continue;
		++*total;
		if (!pte_present(page))
			continue;
		++*pages;
		if (pte_dirty(page))
			++*dirty;
		if (MAP_NR(pte_page(page)) >= max_mapnr)
			continue;
		if (atomic_read(&mem_map[MAP_NR(pte_page(page))].count) > 1)
			++*shared;
	} while (address < end);
}

static inline void statm_pmd_range(pgd_t * pgd, unsigned long address, unsigned long size,
	int * pages, int * shared, int * dirty, int * total)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		printk("statm_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		statm_pte_range(pmd, address, end - address, pages, shared, dirty, total);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void statm_pgd_range(pgd_t * pgd, unsigned long address, unsigned long end,
	int * pages, int * shared, int * dirty, int * total)
{
	while (address < end) {
		statm_pmd_range(pgd, address, end - address, pages, shared, dirty, total);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgd++;
	}
}

static int get_statm(int pid, char * buffer)
{
	int size=0, resident=0, share=0, trs=0, lrs=0, drs=0, dt=0;
	struct mm_struct *mm;

	mm = get_mm_and_lock(pid);
	if (mm) {
		struct vm_area_struct * vma = mm->mmap;

		while (vma) {
			pgd_t *pgd = pgd_offset(mm, vma->vm_start);
			int pages = 0, shared = 0, dirty = 0, total = 0;

			statm_pgd_range(pgd, vma->vm_start, vma->vm_end, &pages, &shared, &dirty, &total);
			resident += pages;
			share += shared;
			dt += dirty;
			size += total;
			if (vma->vm_flags & VM_EXECUTABLE)
				trs += pages;	/* text */
			else if (vma->vm_flags & VM_GROWSDOWN)
				drs += pages;	/* stack */
			else if (vma->vm_end > 0x60000000)
				lrs += pages;	/* library */
			else
				drs += pages;
			vma = vma->vm_next;
		}
		release_mm(mm);
	}
	return sprintf(buffer,"%d %d %d %d %d %d %d\n",
		       size, resident, share, trs, lrs, drs, dt);
}

/*
 * The way we support synthetic files > 4K
 * - without storing their contents in some buffer and
 * - without walking through the entire synthetic file until we reach the
 *   position of the requested data
 * is to cleverly encode the current position in the file's f_pos field.
 * There is no requirement that a read() call which returns `count' bytes
 * of data increases f_pos by exactly `count'.
 *
 * This idea is Linus' one. Bruno implemented it.
 */

/*
 * For the /proc/<pid>/maps file, we use fixed length records, each containing
 * a single line.
 */
#define MAPS_LINE_LENGTH	4096
#define MAPS_LINE_SHIFT		12
/*
 * f_pos = (number of the vma in the task->mm->mmap list) * MAPS_LINE_LENGTH
 *         + (index into the line)
 */
/* for systems with sizeof(void*) == 4: */
#define MAPS_LINE_FORMAT4	  "%08lx-%08lx %s %08lx %s %lu"
#define MAPS_LINE_MAX4	49 /* sum of 8  1  8  1 4 1 8 1 5 1 10 1 */

/* for systems with sizeof(void*) == 8: */
#define MAPS_LINE_FORMAT8	  "%016lx-%016lx %s %016lx %s %lu"
#define MAPS_LINE_MAX8	73 /* sum of 16  1  16  1 4 1 16 1 5 1 10 1 */

#define MAPS_LINE_MAX	MAPS_LINE_MAX8

/* FIXME: this does not do proper mm locking */
static ssize_t read_maps (int pid, struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	struct task_struct *p;
	struct vm_area_struct * map, * next;
	char * destptr = buf, * buffer;
	loff_t lineno;
	ssize_t column, i;
	int volatile_task;
	long retval;

	/*
	 * We might sleep getting the page, so get it first.
	 */
	retval = -ENOMEM;
	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		goto out;

	retval = -EINVAL;
	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);	/* FIXME!! This should be done after the last use */
	if (!p)
		goto freepage_out;

	if (!p->mm || p->mm == &init_mm || count == 0)
		goto getlen_out;

	/* Check whether the mmaps could change if we sleep */
	volatile_task = (p != current || atomic_read(&p->mm->count) > 1);

	/* decode f_pos */
	lineno = *ppos >> MAPS_LINE_SHIFT;
	column = *ppos & (MAPS_LINE_LENGTH-1);

	/* quickly go to line lineno */
	for (map = p->mm->mmap, i = 0; map && (i < lineno); map = map->vm_next, i++)
		continue;

	for ( ; map ; map = next ) {
		/* produce the next line */
		char *line;
		char str[5], *cp = str;
		int flags;
		kdev_t dev;
		unsigned long ino;
		int maxlen = (sizeof(void*) == 4) ?
			MAPS_LINE_MAX4 :  MAPS_LINE_MAX8;
		int len;

		/*
		 * Get the next vma now (but it won't be used if we sleep).
		 */
		next = map->vm_next;
		flags = map->vm_flags;

		*cp++ = flags & VM_READ ? 'r' : '-';
		*cp++ = flags & VM_WRITE ? 'w' : '-';
		*cp++ = flags & VM_EXEC ? 'x' : '-';
		*cp++ = flags & VM_MAYSHARE ? 's' : 'p';
		*cp++ = 0;

		dev = 0;
		ino = 0;
		if (map->vm_file != NULL) {
			dev = map->vm_file->f_dentry->d_inode->i_dev;
			ino = map->vm_file->f_dentry->d_inode->i_ino;
			line = d_path(map->vm_file->f_dentry, buffer, PAGE_SIZE);
			buffer[PAGE_SIZE-1] = '\n';
			line -= maxlen;
			if(line < buffer)
				line = buffer;
		} else
			line = buffer;

		len = sprintf(line,
			      sizeof(void*) == 4 ? MAPS_LINE_FORMAT4 : MAPS_LINE_FORMAT8,
			      map->vm_start, map->vm_end, str, map->vm_offset,
			      kdevname(dev), ino);

		if(map->vm_file) {
			for(i = len; i < maxlen; i++)
				line[i] = ' ';
			len = buffer + PAGE_SIZE - line;
		} else
			line[len++] = '\n';
		if (column >= len) {
			column = 0; /* continue with next line at column 0 */
			lineno++;
			continue; /* we haven't slept */
		}

		i = len-column;
		if (i > count)
			i = count;
		copy_to_user(destptr, line+column, i); /* may have slept */
		destptr += i;
		count   -= i;
		column  += i;
		if (column >= len) {
			column = 0; /* next time: next line at column 0 */
			lineno++;
		}

		/* done? */
		if (count == 0)
			break;

		/* By writing to user space, we might have slept.
		 * Stop the loop, to avoid a race condition.
		 */
		if (volatile_task)
			break;
	}

	/* encode f_pos */
	*ppos = (lineno << MAPS_LINE_SHIFT) + column;

getlen_out:
	retval = destptr - buf;

freepage_out:
	free_page((unsigned long)buffer);
out:
	return retval;
}

#ifdef __SMP__
static int get_pidcpu(int pid, char * buffer)
{
	struct task_struct * tsk;
	int i, len;

	tsk = grab_task(pid);
	if (!tsk)
		return 0;

	len = sprintf(buffer,
		"cpu  %lu %lu\n",
		tsk->times.tms_utime,
		tsk->times.tms_stime);
		
	for (i = 0 ; i < smp_num_cpus; i++)
		len += sprintf(buffer + len, "cpu%d %lu %lu\n",
			i,
			tsk->per_cpu_utime[cpu_logical_map(i)],
			tsk->per_cpu_stime[cpu_logical_map(i)]);

	release_task(tsk);
	return len;
}
#endif

#ifdef CONFIG_MODULES
extern int get_module_list(char *);
extern int get_ksyms_list(char *, char **, off_t, int);
#endif
extern int get_device_list(char *);
extern int get_partition_list(char *);
extern int get_filesystem_list(char *);
extern int get_filesystem_info( char * );
extern int get_irq_list(char *);
extern int get_dma_list(char *);
extern int get_cpuinfo(char *);
extern int get_pci_list(char *);
extern int get_md_status (char *);
extern int get_rtc_status (char *);
extern int get_locks_status (char *, char **, off_t, int);
extern int get_swaparea_info (char *);
extern int get_hardware_list(char *);
extern int get_stram_list(char *);

static long get_root_array(char * page, int type, char **start,
	off_t offset, unsigned long length)
{
	switch (type) {
		case PROC_LOADAVG:
			return get_loadavg(page);

		case PROC_UPTIME:
			return get_uptime(page);

		case PROC_MEMINFO:
			return get_meminfo(page);

#ifdef CONFIG_PCI_OLD_PROC
  	        case PROC_PCI:
			return get_pci_list(page);
#endif

#ifdef CONFIG_NUBUS
		case PROC_NUBUS:
			return get_nubus_list(page);
#endif			
			
		case PROC_CPUINFO:
			return get_cpuinfo(page);

		case PROC_VERSION:
			return get_version(page);

#ifdef CONFIG_DEBUG_MALLOC
		case PROC_MALLOC:
			return get_malloc(page);
#endif

#ifdef CONFIG_MODULES
		case PROC_MODULES:
			return get_module_list(page);

		case PROC_KSYMS:
			return get_ksyms_list(page, start, offset, length);
#endif

		case PROC_STAT:
			return get_kstat(page);

		case PROC_SLABINFO:
			return get_slabinfo(page);

		case PROC_DEVICES:
			return get_device_list(page);

		case PROC_PARTITIONS:
			return get_partition_list(page);

		case PROC_INTERRUPTS:
			return get_irq_list(page);

		case PROC_FILESYSTEMS:
			return get_filesystem_list(page);

		case PROC_DMA:
			return get_dma_list(page);

		case PROC_IOPORTS:
			return get_ioport_list(page);
#ifdef CONFIG_BLK_DEV_MD
	        case PROC_MD:
			return get_md_status(page);
#endif
		case PROC_CMDLINE:
			return get_cmdline(page);

                case PROC_MTAB:
                       return get_filesystem_info( page );

                case PROC_SWAP:
                       return get_swaparea_info(page);
#ifdef CONFIG_RTC
		case PROC_RTC:
			return get_rtc_status(page);
#endif
		case PROC_LOCKS:
			return get_locks_status(page, start, offset, length);
#ifdef CONFIG_PROC_HARDWARE
		case PROC_HARDWARE:
			return get_hardware_list(page);
#endif
#ifdef CONFIG_STRAM_PROC
		case PROC_STRAM:
			return get_stram_list(page);
#endif
	}
	return -EBADF;
}

static int process_unauthorized(int type, int pid)
{
	struct task_struct *p;
	uid_t euid=0;	/* Save the euid keep the lock short */
		
	read_lock(&tasklist_lock);
	
	/*
	 *	Grab the lock, find the task, save the uid and
	 *	check it has an mm still (ie its not dead)
	 */
	p = find_task_by_pid(pid);
	if(p)
	{
		euid=p->euid;
		if(!p->mm)	/* Scooby scooby doo where are you ? */
			p=NULL;
	}
		
	read_unlock(&tasklist_lock);

	if (!p)
		return 1;

	switch(type)
	{
		case PROC_PID_STATUS:
		case PROC_PID_STATM:
		case PROC_PID_STAT:
		case PROC_PID_MAPS:
		case PROC_PID_CMDLINE:
		case PROC_PID_CPU:
			return 0;	
	}
	if(capable(CAP_DAC_OVERRIDE) || current->fsuid == euid)
		return 0;
	return 1;
}


static int get_process_array(char * page, int pid, int type)
{
	switch (type) {
		case PROC_PID_STATUS:
			return get_status(pid, page);
		case PROC_PID_ENVIRON:
			return get_env(pid, page);
		case PROC_PID_CMDLINE:
			return get_arg(pid, page);
		case PROC_PID_STAT:
			return get_stat(pid, page);
		case PROC_PID_STATM:
			return get_statm(pid, page);
#ifdef __SMP__
		case PROC_PID_CPU:
			return get_pidcpu(pid, page);
#endif
	}
	return -EBADF;
}


static inline int fill_array(char * page, int pid, int type, char **start, off_t offset, int length)
{
	if (pid)
		return get_process_array(page, pid, type);
	return get_root_array(page, type, start, offset, length);
}

#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static ssize_t array_read(struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	unsigned long page;
	char *start;
	ssize_t length;
	ssize_t end;
	unsigned int type, pid;
	struct proc_dir_entry *dp;
	int err;

	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;
	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	type = inode->i_ino;
	pid = type >> 16;
	type &= 0x0000ffff;
	start = NULL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	
	if (pid && process_unauthorized(type, pid))
	{
		free_page(page);
		return -EIO;
	}
	
	if (dp->get_info)
		length = dp->get_info((char *)page, &start, *ppos,
				      count, 0);
	else
		length = fill_array((char *) page, pid, type,
				    &start, *ppos, count);
	if (length < 0) {
		free_page(page);
		return length;
	}
	if (start != NULL) {
		if (length > count)
			length = count;
		/* We have had block-adjusting processing! */
		err = copy_to_user(buf, start, length);
		*ppos += length;
		count = length;
	} else {
		/* Static 4kB (or whatever) block capacity */
		if (*ppos >= length) {
			free_page(page);
			return 0;
		}
		if (count + *ppos > length)
			count = length - *ppos;
		end = count + *ppos;
		err = copy_to_user(buf, (char *) page + *ppos, count);
		*ppos = end;
	}
	free_page(page);
	return err ? -EFAULT : count;
}

static struct file_operations proc_array_operations = {
	NULL,		/* array_lseek */
	array_read,
	NULL,		/* array_write */
	NULL,		/* array_readdir */
	NULL,		/* array_poll */
	NULL,		/* array_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_array_inode_operations = {
	&proc_array_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static ssize_t arraylong_read(struct file * file, char * buf,
			      size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	unsigned int pid = inode->i_ino >> 16;
	unsigned int type = inode->i_ino & 0x0000ffff;

	switch (type) {
		case PROC_PID_MAPS:
			return read_maps(pid, file, buf, count, ppos);
	}
	return -EINVAL;
}

static struct file_operations proc_arraylong_operations = {
	NULL,		/* array_lseek */
	arraylong_read,
	NULL,		/* array_write */
	NULL,		/* array_readdir */
	NULL,		/* array_poll */
	NULL,		/* array_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_arraylong_inode_operations = {
	&proc_arraylong_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
