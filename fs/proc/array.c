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
 * <haible@ma2s2.mathematik.uni-karlsruhe.de>
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

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

#ifdef CONFIG_DEBUG_MALLOC
int get_malloc(char * buffer);
#endif


static int read_core(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long p = file->f_pos, memsize;
	int read;
	int count1;
	char * pnt;
	struct user dump;
#ifdef __i386__
#	define FIRST_MAPPED	PAGE_SIZE	/* we don't have page 0 mapped on x86.. */
#else
#	define FIRST_MAPPED	0
#endif

	memset(&dump, 0, sizeof(struct user));
	dump.magic = CMAGIC;
	dump.u_dsize = MAP_NR(high_memory);
#ifdef __alpha__
	dump.start_data = PAGE_OFFSET;
#endif

	if (count < 0)
		return -EINVAL;
	memsize = MAP_NR(high_memory + PAGE_SIZE) << PAGE_SHIFT;
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
		memcpy_tofs(buf,(void *) pnt, count1);
		buf += count1;
		p += count1;
		count -= count1;
		read += count1;
	}

	while (count > 0 && p < PAGE_SIZE + FIRST_MAPPED) {
		put_user(0,buf);
		buf++;
		p++;
		count--;
		read++;
	}
	memcpy_tofs(buf, (void *) (PAGE_OFFSET + p - PAGE_SIZE), count);
	read += count;
	file->f_pos += read;
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
static int read_profile(struct inode *inode, struct file *file, char *buf, int count)
{
	unsigned long p = file->f_pos;
	int read;
	char * pnt;
	unsigned int sample_step = 1 << prof_shift;

	if (count < 0)
		return -EINVAL;
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
	memcpy_tofs(buf,(void *)pnt,count);
	read += count;
	file->f_pos += read;
	return read;
}

/* Writing to /proc/profile resets the counters */
static int write_profile(struct inode * inode, struct file * file, const char * buf, int count)
{
    int i=prof_len;

    while (i--)
	    prof_buffer[i]=0UL;
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

	for (i = 0 ; i < NR_IRQS ; i++)
		sum += kstat.interrupts[i];
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
		jiffies - (kstat.cpu_user + kstat.cpu_nice + kstat.cpu_system),
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
		len += sprintf(buffer + len, " %u", kstat.interrupts[i]);
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
	idle = task[0]->utime + task[0]->stime;

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

static struct task_struct ** get_task(pid_t pid)
{
	struct task_struct ** p;

	p = task;
	while (++p < task+NR_TASKS) {
		if (*p && (*p)->pid == pid)
			return p;
	}
	return NULL;
}

static unsigned long get_phys_addr(struct task_struct * p, unsigned long ptr)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;

	if (!p || !p->mm || ptr >= TASK_SIZE)
		return 0;
	page_dir = pgd_offset(p->mm,ptr);
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

static int get_array(struct task_struct ** p, unsigned long start, unsigned long end, char * buffer)
{
	unsigned long addr;
	int size = 0, result = 0;
	char c;

	if (start >= end)
		return result;
	for (;;) {
		addr = get_phys_addr(*p, start);
		if (!addr)
			goto ready;
		do {
			c = *(char *) addr;
			if (!c)
				result = size;
			if (size < PAGE_SIZE)
				buffer[size++] = c;
			else
				goto ready;
			addr++;
			start++;
			if (!c && start >= end)
				goto ready;
		} while (addr & ~PAGE_MASK);
	}
ready:
	/* remove the trailing blanks, used to fill out argv,envp space */
	while (result>0 && buffer[result-1]==' ')
		result--;
	return result;
}

static int get_env(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);

	if (!p || !*p || !(*p)->mm)
		return 0;
	return get_array(p, (*p)->mm->env_start, (*p)->mm->env_end, buffer);
}

static int get_arg(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);

	if (!p || !*p || !(*p)->mm)
		return 0;
	return get_array(p, (*p)->mm->arg_start, (*p)->mm->arg_end, buffer);
}

static unsigned long get_wchan(struct task_struct *p)
{
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
#if defined(__i386__)
	{
		unsigned long ebp, eip;
		unsigned long stack_page;
		int count = 0;

		stack_page = p->kernel_stack_page;
		if (!stack_page)
			return 0;
		ebp = p->tss.ebp;
		do {
			if (ebp < stack_page || ebp >= 4092+stack_page)
				return 0;
			eip = *(unsigned long *) (ebp+4);
			if ((void *)eip != sleep_on &&
			    (void *)eip != interruptible_sleep_on)
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
	    if (pc >= (unsigned long) interruptible_sleep_on && pc < (unsigned long) add_timer) {
		schedule_frame = ((unsigned long *)p->tss.ksp)[6];
		return ((unsigned long *)schedule_frame)[12];
	    }
	    return pc;
	}
#endif
	return 0;
}

#if defined(__i386__)
# define KSTK_EIP(tsk)	(((unsigned long *)tsk->kernel_stack_page)[1019])
# define KSTK_ESP(tsk)	(((unsigned long *)tsk->kernel_stack_page)[1022])
#elif defined(__alpha__)
  /*
   * See arch/alpha/kernel/ptrace.c for details.
   */
# define PT_REG(reg)		(PAGE_SIZE - sizeof(struct pt_regs)	\
				 + (long)&((struct pt_regs *)0)->reg)
# define KSTK_EIP(tsk)	(*(unsigned long *)(tsk->kernel_stack_page + PT_REG(pc)))
# define KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->tss.usp)
#elif defined(__sparc__)
# define PT_REG(reg)            (PAGE_SIZE - sizeof(struct pt_regs)     \
                                 + (long)&((struct pt_regs *)0)->reg)
# define KSTK_EIP(tsk)  (*(unsigned long *)(tsk->kernel_stack_page + PT_REG(pc)))
# define KSTK_ESP(tsk)  (*(unsigned long *)(tsk->kernel_stack_page + PT_REG(u_regs[UREG_FP])))
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

static inline char * task_state(struct task_struct *p, char *buffer)
{
#define NR_STATES (sizeof(states)/sizeof(const char *))
	unsigned int n = p->state;
	static const char * states[] = {
		"R (running)",
		"S (sleeping)",
		"D (disk sleep)",
		"Z (zombie)",
		"T (stopped)",
		"W (paging)",
		". Huh?"
	};

	if (n >= NR_STATES)
		n = NR_STATES-1;

	buffer += sprintf(buffer,
		"State:\t%s\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n",
		states[n],
		p->pid, p->p_pptr->pid,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);
	return buffer;
}

static inline char * task_mem(struct task_struct *p, char *buffer)
{
	struct mm_struct * mm = p->mm;

	if (mm && mm != &init_mm) {
		struct vm_area_struct * vma = mm->mmap;
		unsigned long data = 0, stack = 0;
		unsigned long exec = 0, lib = 0;

		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			unsigned long len = (vma->vm_end - vma->vm_start) >> 10;
			if (!vma->vm_inode) {
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

static inline char * task_sig(struct task_struct *p, char *buffer)
{
	buffer += sprintf(buffer,
		"SigPnd:\t%08lx\n"
		"SigBlk:\t%08lx\n",
		p->signal, p->blocked);
	if (p->sig) {
		struct sigaction * action = p->sig->action;
		unsigned long sig_ign = 0, sig_caught = 0;
		unsigned long bit = 1;
		int i;

		for (i = 0; i < 32; i++) {
			switch((unsigned long) action->sa_handler) {
				case 0:
					break;
				case 1:
					sig_ign |= bit;
					break;
				default:
					sig_caught |= bit;
			}
			bit <<= 1;
			action++;
		}

		buffer += sprintf(buffer,
			"SigIgn:\t%08lx\n"
			"SigCgt:\t%08lx\n",
			sig_ign, sig_caught);
	}
	return buffer;
}

static int get_status(int pid, char * buffer)
{
	char * orig = buffer;
	struct task_struct ** p = get_task(pid), *tsk;

	if (!p || (tsk = *p) == NULL)
		return 0;
	buffer = task_name(tsk, buffer);
	buffer = task_state(tsk, buffer);
	buffer = task_mem(tsk, buffer);
	buffer = task_sig(tsk, buffer);
	return buffer - orig;
}

static int get_stat(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid), *tsk;
	unsigned long sigignore=0, sigcatch=0, wchan;
	unsigned long vsize, eip, esp;
	long priority, nice;
	int i,tty_pgrp;
	char state;

	if (!p || (tsk = *p) == NULL)
		return 0;
	if (tsk->state < 0 || tsk->state > 5)
		state = '.';
	else
		state = "RSDZTW"[tsk->state];
	vsize = eip = esp = 0;
	if (tsk->mm && tsk->mm != &init_mm) {
		struct vm_area_struct *vma = tsk->mm->mmap;
		while (vma) {
			vsize += vma->vm_end - vma->vm_start;
			vma = vma->vm_next;
		}
		if (tsk->kernel_stack_page) {
			eip = KSTK_EIP(tsk);
			esp = KSTK_ESP(tsk);
		}
	}
	wchan = get_wchan(tsk);
	if (tsk->sig) {
		unsigned long bit = 1;
		for(i=0; i<32; ++i) {
			switch((unsigned long) tsk->sig->action[i].sa_handler) {
				case 0:
					break;
				case 1:
					sigignore |= bit;
					break;
				default:
					sigcatch |= bit;
			}
			bit <<= 1;
		}
	}
	if (tsk->tty)
		tty_pgrp = tsk->tty->pgrp;
	else
		tty_pgrp = -1;

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" unix priority/nice value  */
	priority = tsk->counter;
	priority = 20 - (priority * 10 + DEF_PRIORITY / 2) / DEF_PRIORITY;
	nice = tsk->priority;
	nice = 20 - (nice * 20 + DEF_PRIORITY / 2) / DEF_PRIORITY;

	return sprintf(buffer,"%d (%s) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu\n",
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
		tsk->utime,
		tsk->stime,
		tsk->cutime,
		tsk->cstime,
		priority,
		nice,
		tsk->timeout,
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
		tsk->signal,
		tsk->blocked,
		sigignore,
		sigcatch,
		wchan,
		tsk->nswap,
		tsk->cnswap);
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
		if (pte_page(page) >= high_memory)
			continue;
		if (mem_map[MAP_NR(pte_page(page))].count > 1)
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
	struct task_struct ** p = get_task(pid), *tsk;
	int size=0, resident=0, share=0, trs=0, lrs=0, drs=0, dt=0;

	if (!p || (tsk = *p) == NULL)
		return 0;
	if (tsk->mm && tsk->mm != &init_mm) {
		struct vm_area_struct * vma = tsk->mm->mmap;

		while (vma) {
			pgd_t *pgd = pgd_offset(tsk->mm, vma->vm_start);
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
#define MAPS_LINE_LENGTH	1024
#define MAPS_LINE_SHIFT		10
/*
 * f_pos = (number of the vma in the task->mm->mmap list) * MAPS_LINE_LENGTH
 *         + (index into the line)
 */
#ifdef __alpha__
#define MAPS_LINE_FORMAT	  "%016lx-%016lx %s %016lx %s %lu\n"
#define MAPS_LINE_MAX	73 /* sum of 16  1  16  1 4 1 16 1 5 1 10 1 */
#else
#define MAPS_LINE_FORMAT	  "%08lx-%08lx %s %08lx %s %lu\n"
#define MAPS_LINE_MAX	49 /* sum of 8  1  8  1 4 1 8 1 5 1 10 1 */
#endif

static int read_maps (int pid, struct file * file, char * buf, int count)
{
	struct task_struct ** p = get_task(pid);
	char * destptr;
	loff_t lineno;
	int column;
	struct vm_area_struct * map;
	int i;

	if (!p || !*p)
		return -EINVAL;

	if (!(*p)->mm || (*p)->mm == &init_mm || count == 0)
		return 0;

	/* decode f_pos */
	lineno = file->f_pos >> MAPS_LINE_SHIFT;
	column = file->f_pos & (MAPS_LINE_LENGTH-1);

	/* quickly go to line lineno */
	for (map = (*p)->mm->mmap, i = 0; map && (i < lineno); map = map->vm_next, i++)
		continue;

	destptr = buf;

	for ( ; map ; ) {
		/* produce the next line */
		char line[MAPS_LINE_MAX+1];
		char str[5], *cp = str;
		int flags;
		kdev_t dev;
		unsigned long ino;
		int len;

		flags = map->vm_flags;

		*cp++ = flags & VM_READ ? 'r' : '-';
		*cp++ = flags & VM_WRITE ? 'w' : '-';
		*cp++ = flags & VM_EXEC ? 'x' : '-';
		*cp++ = flags & VM_MAYSHARE ? 's' : 'p';
		*cp++ = 0;

		if (map->vm_inode != NULL) {
			dev = map->vm_inode->i_dev;
			ino = map->vm_inode->i_ino;
		} else {
			dev = 0;
			ino = 0;
		}

		len = sprintf(line, MAPS_LINE_FORMAT,
			      map->vm_start, map->vm_end, str, map->vm_offset,
			      kdevname(dev), ino);

		if (column >= len) {
			column = 0; /* continue with next line at column 0 */
			lineno++;
			map = map->vm_next;
			continue;
		}

		i = len-column;
		if (i > count)
			i = count;
		memcpy_tofs(destptr, line+column, i);
		destptr += i; count -= i;
		column += i;
		if (column >= len) {
			column = 0; /* next time: next line at column 0 */
			lineno++;
			map = map->vm_next;
		}

		/* done? */
		if (count == 0)
			break;

		/* By writing to user space, we might have slept.
		 * Stop the loop, to avoid a race condition.
		 */
		if (*p != current)
			break;
	}

	/* encode f_pos */
	file->f_pos = (lineno << MAPS_LINE_SHIFT) + column;

	return destptr-buf;
}

#ifdef CONFIG_MODULES
extern int get_module_list(char *);
extern int get_ksyms_list(char *, char **, off_t, int);
#endif
extern int get_device_list(char *);
extern int get_filesystem_list(char *);
extern int get_filesystem_info( char * );
extern int get_irq_list(char *);
extern int get_dma_list(char *);
extern int get_cpuinfo(char *);
extern int get_pci_list(char*);
extern int get_md_status (char *);
extern int get_rtc_status (char *);
extern int get_locks_status (char *);
#ifdef __SMP_PROF__
extern int get_smp_prof_list(char *);
#endif

static int get_root_array(char * page, int type, char **start, off_t offset, int length)
{
	switch (type) {
		case PROC_LOADAVG:
			return get_loadavg(page);

		case PROC_UPTIME:
			return get_uptime(page);

		case PROC_MEMINFO:
			return get_meminfo(page);

#ifdef CONFIG_PCI
  	        case PROC_PCI:
			return get_pci_list(page);
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

		case PROC_DEVICES:
			return get_device_list(page);

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
#ifdef __SMP_PROF__
		case PROC_SMP_PROF:
			return get_smp_prof_list(page);
#endif
		case PROC_CMDLINE:
			return get_cmdline(page);

                case PROC_MTAB:
                       return get_filesystem_info( page );
#ifdef CONFIG_RTC
		case PROC_RTC:
			return get_rtc_status(page);
#endif
		case PROC_LOCKS:
			return get_locks_status(page);
	}
	return -EBADF;
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

static int array_read(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long page;
	char *start;
	int length;
	int end;
	unsigned int type, pid;
	struct proc_dir_entry *dp;

	if (count < 0)
		return -EINVAL;
	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;
	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	type = inode->i_ino;
	pid = type >> 16;
	type &= 0x0000ffff;
	start = NULL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (dp->get_info)
		length = dp->get_info((char *)page, &start, file->f_pos,
				      count, 0);
	else
		length = fill_array((char *) page, pid, type,
				    &start, file->f_pos, count);
	if (length < 0) {
		free_page(page);
		return length;
	}
	if (start != NULL) {
		/* We have had block-adjusting processing! */
		memcpy_tofs(buf, start, length);
		file->f_pos += length;
		count = length;
	} else {
		/* Static 4kB (or whatever) block capacity */
		if (file->f_pos >= length) {
			free_page(page);
			return 0;
		}
		if (count + file->f_pos > length)
			count = length - file->f_pos;
		end = count + file->f_pos;
		memcpy_tofs(buf, (char *) page + file->f_pos, count);
		file->f_pos = end;
	}
	free_page(page);
	return count;
}

static struct file_operations proc_array_operations = {
	NULL,		/* array_lseek */
	array_read,
	NULL,		/* array_write */
	NULL,		/* array_readdir */
	NULL,		/* array_select */
	NULL,		/* array_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
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

static int arraylong_read (struct inode * inode, struct file * file, char * buf, int count)
{
	unsigned int pid = inode->i_ino >> 16;
	unsigned int type = inode->i_ino & 0x0000ffff;

	if (count < 0)
		return -EINVAL;

	switch (type) {
		case PROC_PID_MAPS:
			return read_maps(pid, file, buf, count);
	}
	return -EINVAL;
}

static struct file_operations proc_arraylong_operations = {
	NULL,		/* array_lseek */
	arraylong_read,
	NULL,		/* array_write */
	NULL,		/* array_readdir */
	NULL,		/* array_select */
	NULL,		/* array_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
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
