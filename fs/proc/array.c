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
 * Danny ter Haar    :	Some minor additions for cpuinfo
 *			<danny@ow.nl>
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
#include <linux/delay.h>
#include <linux/mm.h>

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
	unsigned long p = file->f_pos;
	int read;
	int count1;
	char * pnt;
	struct user dump;

	memset(&dump, 0, sizeof(struct user));
	dump.magic = CMAGIC;
	dump.u_dsize = high_memory >> 12;

	if (count < 0)
		return -EINVAL;
	if (p >= high_memory + PAGE_SIZE)
		return 0;
	if (count > high_memory + PAGE_SIZE - p)
		count = high_memory + PAGE_SIZE - p;
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

	while (p < 2*PAGE_SIZE && count > 0) {
		put_fs_byte(0,buf);
		buf++;
		p++;
		count--;
		read++;
	}
	memcpy_tofs(buf,(void *) (p - PAGE_SIZE),count);
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


#ifdef CONFIG_PROFILE

extern unsigned long prof_len;
extern unsigned long * prof_buffer;
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
	unsigned long sample_step = 1 << CONFIG_PROFILE_SHIFT;

	if (count < 0)
	    return -EINVAL;
	if (p >= (prof_len+1)*sizeof(unsigned long))
	    return 0;
	if (count > (prof_len+1)*sizeof(unsigned long) - p)
	    count = (prof_len+1)*sizeof(unsigned long) - p;
    read = 0;

    while (p < sizeof(unsigned long) && count > 0) {
        put_fs_byte(*((char *)(&sample_step)+p),buf);
		buf++; p++; count--; read++;
    }
    pnt = (char *)prof_buffer + p - sizeof(unsigned long);
	memcpy_tofs(buf,(void *)pnt,count);
	read += count;
	file->f_pos += read;
	return read;
}

/* Writing to /proc/profile resets the counters */
static int write_profile(struct inode * inode, struct file * file, char * buf, int count)
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

#endif /* CONFIG_PROFILE */


static int get_loadavg(char * buffer)
{
	int a, b, c;

	a = avenrun[0] + (FIXED_1/200);
	b = avenrun[1] + (FIXED_1/200);
	c = avenrun[2] + (FIXED_1/200);
	return sprintf(buffer,"%d.%02d %d.%02d %d.%02d %d/%d\n",
		LOAD_INT(a), LOAD_FRAC(a),
		LOAD_INT(b), LOAD_FRAC(b),
		LOAD_INT(c), LOAD_FRAC(c),
		nr_running, nr_tasks);
}

static int get_kstat(char * buffer)
{
	int i, len;
	unsigned sum = 0;

	for (i = 0 ; i < 16 ; i++)
		sum += kstat.interrupts[i];
	len = sprintf(buffer,
		"cpu  %u %u %u %lu\n"
		"disk %u %u %u %u\n"
		"page %u %u\n"
		"swap %u %u\n"
		"intr %u",
		kstat.cpu_user,
		kstat.cpu_nice,
		kstat.cpu_system,
		jiffies - (kstat.cpu_user + kstat.cpu_nice + kstat.cpu_system),
		kstat.dk_drive[0],
		kstat.dk_drive[1],
		kstat.dk_drive[2],
		kstat.dk_drive[3],
		kstat.pgpgin,
		kstat.pgpgout,
		kstat.pswpin,
		kstat.pswpout,
		sum);
	for (i = 0 ; i < 16 ; i++)
		len += sprintf(buffer + len, " %u", kstat.interrupts[i]);
	len += sprintf(buffer + len,
		"\nctxt %u\n"
		"btime %lu\n",
		kstat.context_swtch,
		xtime.tv_sec - jiffies / HZ);
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

	si_meminfo(&i);
	si_swapinfo(&i);
	return sprintf(buffer, "        total:   used:    free:   shared:  buffers:\n"
		"Mem:  %8lu %8lu %8lu %8lu %8lu\n"
		"Swap: %8lu %8lu %8lu\n",
		i.totalram, i.totalram-i.freeram, i.freeram, i.sharedram, i.bufferram,
		i.totalswap, i.totalswap-i.freeswap, i.freeswap);
}

static int get_version(char * buffer)
{
	extern char *linux_banner;

	strcpy(buffer, linux_banner);
	return strlen(buffer);
}

static int get_cpuinfo(char * buffer)
{
#ifdef __i386__
	char *model[2][9]={{"DX","SX","DX/2","4","SX/2","6",
				"DX/2-WB","DX/4"},
			{"Pentium 60/66","Pentium 90/100","3",
				"4","5","6","7","8"}};
	char mask[2];
	mask[0] = x86_mask+'@';
	mask[1] = '\0';
	return sprintf(buffer,"cpu\t\t: %c86\n"
			      "model\t\t: %s\n"
			      "mask\t\t: %s\n"
			      "vid\t\t: %s\n"
			      "fdiv_bug\t: %s\n"
			      "math\t\t: %s\n"
			      "hlt\t\t: %s\n"
			      "wp\t\t: %s\n"
			      "Integrated NPU\t: %s\n"
			      "Enhanced VM86\t: %s\n"
			      "IO Breakpoints\t: %s\n"
			      "4MB Pages\t: %s\n"
			      "TS Counters\t: %s\n"
			      "Pentium MSR\t: %s\n"
			      "Mach. Ch. Exep.\t: %s\n"
			      "CMPXCHGB8B\t: %s\n"
		              "BogoMips\t: %lu.%02lu\n",
			      x86+'0', 
			      x86_model ? model[x86-4][x86_model-1] : "Unknown",
			      x86_mask ? mask : "Unknown",
			      x86_vendor_id,
			      fdiv_bug ? "yes" : "no",
			      hard_math ? "yes" : "no",
			      hlt_works_ok ? "yes" : "no",
			      wp_works_ok ? "yes" : "no",
			      x86_capability & 1 ? "yes" : "no",
			      x86_capability & 2 ? "yes" : "no",
			      x86_capability & 4 ? "yes" : "no",
			      x86_capability & 8 ? "yes" : "no",
			      x86_capability & 16 ? "yes" : "no",
			      x86_capability & 32 ? "yes" : "no",
			      x86_capability & 128 ? "yes" : "no",
			      x86_capability & 256 ? "yes" : "no",
		              loops_per_sec/500000, (loops_per_sec/5000) % 100
			      );
#else
	return 0;
#endif
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

	if (!p || ptr >= TASK_SIZE)
		return 0;
	page_dir = pgd_offset(p,ptr);
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

	if (!p || !*p)
		return 0;
	return get_array(p, (*p)->mm->env_start, (*p)->mm->env_end, buffer);
}

static int get_arg(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);

	if (!p || !*p)
		return 0;
	return get_array(p, (*p)->mm->arg_start, (*p)->mm->arg_end, buffer);
}

static unsigned long get_wchan(struct task_struct *p)
{
#ifdef __i386__
	unsigned long ebp, eip;
	unsigned long stack_page;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
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
#endif
	return 0;
}

#define	KSTK_EIP(stack)	(((unsigned long *)stack)[1019])
#define	KSTK_ESP(stack)	(((unsigned long *)stack)[1022])

static int get_stat(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);
	unsigned long sigignore=0, sigcatch=0, bit=1, wchan;
	unsigned long vsize, eip, esp;
	int i,tty_pgrp;
	char state;

	if (!p || !*p)
		return 0;
	if ((*p)->state < 0 || (*p)->state > 5)
		state = '.';
	else
		state = "RSDZTD"[(*p)->state];
	eip = esp = 0;
	vsize = (*p)->kernel_stack_page;
	if (vsize) {
		eip = KSTK_EIP(vsize);
		esp = KSTK_ESP(vsize);
		vsize = (*p)->mm->brk - (*p)->mm->start_code + PAGE_SIZE-1;
		if (esp)
			vsize += TASK_SIZE - esp;
	}
	wchan = get_wchan(*p);
	for(i=0; i<32; ++i) {
		switch((unsigned long) (*p)->sigaction[i].sa_handler) {
		case 1: sigignore |= bit; break;
		case 0: break;
		default: sigcatch |= bit;
		} bit <<= 1;
	}
	if ((*p)->tty)
		tty_pgrp = (*p)->tty->pgrp;
	else
		tty_pgrp = -1;
	return sprintf(buffer,"%d (%s) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu \
%lu %lu %lu %lu\n",
		pid,
		(*p)->comm,
		state,
		(*p)->p_pptr->pid,
		(*p)->pgrp,
		(*p)->session,
	        (*p)->tty ? (*p)->tty->device : 0,
		tty_pgrp,
		(*p)->flags,
		(*p)->mm->min_flt,
		(*p)->mm->cmin_flt,
		(*p)->mm->maj_flt,
		(*p)->mm->cmaj_flt,
		(*p)->utime,
		(*p)->stime,
		(*p)->cutime,
		(*p)->cstime,
		(*p)->counter,  /* this is the kernel priority ---
				   subtract 30 in your user-level program. */
		(*p)->priority, /* this is the nice value ---
				   subtract 15 in your user-level program. */
		(*p)->timeout,
		(*p)->it_real_value,
		(*p)->start_time,
		vsize,
		(*p)->mm->rss, /* you might want to shift this left 3 */
		(*p)->rlim[RLIMIT_RSS].rlim_cur,
		(*p)->mm->start_code,
		(*p)->mm->end_code,
		(*p)->mm->start_stack,
		esp,
		eip,
		(*p)->signal,
		(*p)->blocked,
		sigignore,
		sigcatch,
		wchan);
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
		if (mem_map[MAP_NR(pte_page(page))] > 1)
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
	struct task_struct ** p = get_task(pid);
	int size=0, resident=0, share=0, trs=0, lrs=0, drs=0, dt=0;

	if (!p || !*p)
		return 0;
	if ((*p)->state != TASK_ZOMBIE) {
		struct vm_area_struct * vma = (*p)->mm->mmap;

		while (vma) {
			pgd_t *pgd = pgd_offset(*p, vma->vm_start);
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
#define MAPS_LINE_FORMAT	  "%08lx-%08lx %s %08lx %02x:%02x %lu\n"
#define MAPS_LINE_MAX	49 /* sum of 8  1  8  1 4 1 8  1  2 1  2 1 10 1 */

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

	if (count == 0)
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
		dev_t dev;
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
			      MAJOR(dev),MINOR(dev), ino);

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

extern int get_module_list(char *);
extern int get_device_list(char *);
extern int get_filesystem_list(char *);
extern int get_ksyms_list(char *);
extern int get_irq_list(char *);
extern int get_dma_list(char *);
extern int get_cpuinfo(char *);
extern int get_pci_list(char*);

static int get_root_array(char * page, int type)
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

		case PROC_MODULES:
			return get_module_list(page);

		case PROC_STAT:
			return get_kstat(page);

		case PROC_DEVICES:
			return get_device_list(page);

		case PROC_INTERRUPTS:
			return get_irq_list(page);

		case PROC_FILESYSTEMS:
			return get_filesystem_list(page);

		case PROC_KSYMS:
			return get_ksyms_list(page);

		case PROC_DMA:
			return get_dma_list(page);

		case PROC_IOPORTS:
			return get_ioport_list(page);
	}
	return -EBADF;
}

static int get_process_array(char * page, int pid, int type)
{
	switch (type) {
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


static inline int fill_array(char * page, int pid, int type)
{
	if (pid)
		return get_process_array(page, pid, type);
	return get_root_array(page, type);
}

static int array_read(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long page;
	int length;
	int end;
	unsigned int type, pid;

	if (count < 0)
		return -EINVAL;
	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	type = inode->i_ino;
	pid = type >> 16;
	type &= 0x0000ffff;
	length = fill_array((char *) page, pid, type);
	if (length < 0) {
		free_page(page);
		return length;
	}
	if (file->f_pos >= length) {
		free_page(page);
		return 0;
	}
	if (count + file->f_pos > length)
		count = length - file->f_pos;
	end = count + file->f_pos;
	memcpy_tofs(buf, (char *) page + file->f_pos, count);
	free_page(page);
	file->f_pos = end;
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
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
