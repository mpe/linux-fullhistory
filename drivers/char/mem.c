/*
 *  linux/drivers/char/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <linux/miscdevice.h>
#include <linux/tpqic02.h>
#include <linux/ftape.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/joystick.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#ifdef CONFIG_SOUND
void soundcard_init(void);
#endif
#ifdef CONFIG_ISDN
void isdn_init(void);
#endif
#ifdef CONFIG_PCWATCHDOG
void pcwatchdog_init(void);
#endif

static long do_write_mem(struct file * file,
	void *p, unsigned long realp, 					 
	const char * buf, unsigned long count)
{
	unsigned long written;

	written = 0;
#if defined(__sparc__) || defined(__mc68000__)
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (realp < PAGE_SIZE) {
		unsigned long sz = PAGE_SIZE-realp;
		if (sz > count) sz = count; 
		/* Hmm. Do something? */
		buf+=sz;
		p+=sz;
		count-=sz;
		written+=sz;
	}
#endif
	if (copy_from_user(p, buf, count) < 0) 
		return -EFAULT;
	written += count;
	file->f_pos += written;
	return count;
}


/*
 * This funcion reads the *physical* memory. The f_pos points directly to the 
 * memory location. 
 */
static long read_mem(struct inode * inode, struct file * file,
	char * buf, unsigned long count)
{
	unsigned long p = file->f_pos;
	unsigned long end_mem;
	unsigned long read;
	
	end_mem = __pa(high_memory);
	if (p >= end_mem)
		return 0;
	if (count > end_mem - p)
		count = end_mem - p;
	read = 0;
#if defined(__sparc__) || defined(__mc68000__)
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (p < PAGE_SIZE) {
		unsigned long sz = PAGE_SIZE-p;
		if (sz > count) 
			sz = count; 
		if (sz > 0) {
			if (clear_user(buf, sz))
				return -EFAULT;
			buf += sz; 
			p += sz; 
			count -= sz; 
			read += sz; 
		}
	}
#endif
	if (copy_to_user(buf, __va(p), count) < 0)
		return -EFAULT;
	read += count;
	file->f_pos += read;
	return read;
}

static long write_mem(struct inode * inode, struct file * file,
	const char * buf, unsigned long count)
{
	unsigned long p = file->f_pos;
	unsigned long end_mem;

	end_mem = __pa(high_memory);
	if (p >= end_mem)
		return 0;
	if (count > end_mem - p)
		count = end_mem - p;
	return do_write_mem(file,__va(p),p,buf,count);
}

static int mmap_mem(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	unsigned long offset = vma->vm_offset;

	if (offset & ~PAGE_MASK)
		return -ENXIO;
#if defined(__i386__)
	/*
	 * hmm.. This disables high-memory caching, as the XFree86 team
	 * wondered about that at one time.
	 * The surround logic should disable caching for the high device
	 * addresses anyway, but right now this seems still needed.
	 */
	if (x86 > 3 && offset >= __pa(high_memory))
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
#ifdef __powerpc__
	if (offset >= __pa(high_memory))
		pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE | _PAGE_GUARDED;
#endif
	if (remap_page_range(vma->vm_start, offset, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	vma->vm_dentry = dget(file->f_dentry);
	return 0;
}

/*
 * This function reads the *virtual* memory as seen by the kernel.
 */
static long read_kmem(struct inode *inode, struct file *file,
	char *buf, unsigned long count)
{
	unsigned long p = file->f_pos;
	unsigned long read = 0;
	long virtr;
		
	if (p < (unsigned long) high_memory) { 
		unsigned long tmp;
		
		if (count > (unsigned long) high_memory - p)
			tmp = (unsigned long) high_memory - p;
		else
			tmp = count;
		read = tmp;
#if defined(__sparc__) || defined(__mc68000__)
		/* we don't have page 0 mapped on sparc and m68k.. */
		while (p < PAGE_SIZE && tmp > 0) {
			put_user(0,buf);
			buf++;
			p++;
			tmp--;
		}
#endif
		copy_to_user(buf, (char *) p, tmp);
		buf += tmp;
	}

	virtr = vread(buf, (char *) (unsigned long) file->f_pos, count - read);
	if (virtr < 0)
		return virtr;
	file->f_pos += virtr + read;
	return virtr + read;
}

/*
 * This function writes to the *virtual* memory as seen by the kernel.
 */
static long write_kmem(struct inode * inode, struct file * file,
	const char * buf, unsigned long count)
{
	unsigned long p = file->f_pos;

	if (p >= (unsigned long) high_memory)
		return 0;
	if (count > (unsigned long) high_memory - p)
		count = (unsigned long) high_memory - p;
	return do_write_mem(file,(void*)p,p,buf,count);
}

static long read_port(struct inode * inode, struct file * file,
	char * buf, unsigned long count)
{
	unsigned int i = file->f_pos;
	char * tmp = buf;

	if (verify_area(VERIFY_WRITE,buf,count))
		return -EFAULT; 
	while (count-- > 0 && i < 65536) {
		if (__put_user(inb(i),tmp) < 0) 
			return -EFAULT;  
		i++;
		tmp++;
	}
	file->f_pos = i;
	return tmp-buf;
}

static long write_port(struct inode * inode, struct file * file,
	const char * buf, unsigned long count)
{
	unsigned int i = file->f_pos;
	const char * tmp = buf;

	if (verify_area(VERIFY_READ,buf,count))
		return -EFAULT;
	while (count-- > 0 && i < 65536) {
		char c;
		if (__get_user(c, tmp)) 
			return -EFAULT; 
		outb(c,i);
		i++;
		tmp++;
	}
	file->f_pos = i;
	return tmp-buf;
}

static long read_null(struct inode * node, struct file * file,
	char * buf, unsigned long count)
{
	return 0;
}

static long write_null(struct inode * inode, struct file * file,
	const char * buf, unsigned long count)
{
	return count;
}

/*
 * For fun, we are using the MMU for this.
 */
static inline unsigned long read_zero_pagealigned(char * buf, unsigned long size)
{
	struct vm_area_struct * vma;
	unsigned long addr=(unsigned long)buf;

	/* For private mappings, just map in zero pages. */
	for (vma = find_vma(current->mm, addr); vma; vma = vma->vm_next) {
		unsigned long count;

		if (vma->vm_start > addr || (vma->vm_flags & VM_WRITE) == 0)
			return size;
		if (vma->vm_flags & VM_SHARED)
			break;
		count = vma->vm_end - addr;
		if (count > size)
			count = size;

		flush_cache_range(current->mm, addr, addr + count);
		zap_page_range(current->mm, addr, count);
        	zeromap_page_range(addr, count, PAGE_COPY);
        	flush_tlb_range(current->mm, addr, addr + count);

		size -= count;
		buf += count;
		addr += count;
		if (size == 0)
			return 0;
	}
	
	/* The shared case is hard. Lets do the conventional zeroing. */ 
	do {
		unsigned long unwritten = clear_user(buf, PAGE_SIZE);
		if (unwritten)
			return size + unwritten - PAGE_SIZE;
		if (need_resched)
			schedule();
		buf += PAGE_SIZE;
		size -= PAGE_SIZE;
	} while (size);

	return size;
}

static long read_zero(struct inode * node, struct file * file,
	char * buf, unsigned long count)
{
	unsigned long left, unwritten, written = 0;

	if (!count)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	left = count;

	/* do we want to be clever? Arbitrary cut-off */
	if (count >= PAGE_SIZE*4) {
		unsigned long partial;

		/* How much left of the page? */
		partial = (PAGE_SIZE-1) & -(unsigned long) buf;
		unwritten = clear_user(buf, partial);
		written = partial - unwritten;
		if (unwritten)
			goto out;
		left -= partial;
		buf += partial;
		unwritten = read_zero_pagealigned(buf, left & PAGE_MASK);
		written += (left & PAGE_MASK) - unwritten;
		if (unwritten)
			goto out;
		buf += left & PAGE_MASK;
		left &= ~PAGE_MASK;
	}
	unwritten = clear_user(buf, left);
	written += left - unwritten;
out:
	return written ? written : -EFAULT;
}

static int mmap_zero(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	if (vma->vm_flags & VM_SHARED)
		return -EINVAL;
	if (zeromap_page_range(vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static long read_full(struct inode * node, struct file * file,
	char * buf, unsigned long count)
{
	file->f_pos += count;
	return count;
}

static long write_full(struct inode * inode, struct file * file,
	const char * buf, unsigned long count)
{
	return -ENOSPC;
}

/*
 * Special lseek() function for /dev/null and /dev/zero.  Most notably, you can fopen()
 * both devices with "a" now.  This was previously impossible.  SRB.
 */

static long long null_lseek(struct inode * inode, struct file * file,
	long long offset, int orig)
{
	return file->f_pos=0;
}
/*
 * The memory devices use the full 32/64 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
static long long memory_lseek(struct inode * inode, struct file * file,
	long long offset, int orig)
{
	switch (orig) {
		case 0:
			file->f_pos = offset;
			return file->f_pos;
		case 1:
			file->f_pos += offset;
			return file->f_pos;
		default:
			return -EINVAL;
	}
	if (file->f_pos < 0)
		return 0;
	return file->f_pos;
}

#define mmap_kmem	mmap_mem
#define zero_lseek	null_lseek
#define write_zero	write_null

static struct file_operations mem_fops = {
	memory_lseek,
	read_mem,
	write_mem,
	NULL,		/* mem_readdir */
	NULL,		/* mem_poll */
	NULL,		/* mem_ioctl */
	mmap_mem,
	NULL,		/* no special open code */
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct file_operations kmem_fops = {
	memory_lseek,
	read_kmem,
	write_kmem,
	NULL,		/* kmem_readdir */
	NULL,		/* kmem_poll */
	NULL,		/* kmem_ioctl */
	mmap_kmem,
	NULL,		/* no special open code */
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct file_operations null_fops = {
	null_lseek,
	read_null,
	write_null,
	NULL,		/* null_readdir */
	NULL,		/* null_poll */
	NULL,		/* null_ioctl */
	NULL,		/* null_mmap */
	NULL,		/* no special open code */
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct file_operations port_fops = {
	memory_lseek,
	read_port,
	write_port,
	NULL,		/* port_readdir */
	NULL,		/* port_poll */
	NULL,		/* port_ioctl */
	NULL,		/* port_mmap */
	NULL,		/* no special open code */
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct file_operations zero_fops = {
	zero_lseek,
	read_zero,
	write_zero,
	NULL,		/* zero_readdir */
	NULL,		/* zero_poll */
	NULL,		/* zero_ioctl */
	mmap_zero,
	NULL,		/* no special open code */
	NULL		/* no special release code */
};

static struct file_operations full_fops = {
	memory_lseek,
	read_full,
	write_full,
	NULL,		/* full_readdir */
	NULL,		/* full_poll */
	NULL,		/* full_ioctl */	
	NULL,		/* full_mmap */
	NULL,		/* no special open code */
	NULL		/* no special release code */
};

static int memory_open(struct inode * inode, struct file * filp)
{
	switch (MINOR(inode->i_rdev)) {
		case 1:
			filp->f_op = &mem_fops;
			break;
		case 2:
			filp->f_op = &kmem_fops;
			break;
		case 3:
			filp->f_op = &null_fops;
			break;
		case 4:
			filp->f_op = &port_fops;
			break;
		case 5:
			filp->f_op = &zero_fops;
			break;
		case 7:
			filp->f_op = &full_fops;
			break;
		case 8:
			filp->f_op = &random_fops;
			break;
		case 9:
			filp->f_op = &urandom_fops;
			break;
		default:
			return -ENXIO;
	}
	if (filp->f_op && filp->f_op->open)
		return filp->f_op->open(inode,filp);
	return 0;
}

static struct file_operations memory_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	memory_open,	/* just a selector for the real open */
	NULL,		/* release */
	NULL		/* fsync */
};

__initfunc(int chr_dev_init(void))
{
	if (register_chrdev(MEM_MAJOR,"mem",&memory_fops))
		printk("unable to get major %d for memory devs\n", MEM_MAJOR);
	rand_initialize();
	tty_init();
#ifdef CONFIG_PRINTER
	lp_init();
#endif
#if defined (CONFIG_BUSMOUSE) || defined(CONFIG_UMISC) || \
    defined (CONFIG_PSMOUSE) || defined (CONFIG_MS_BUSMOUSE) || \
    defined (CONFIG_ATIXL_BUSMOUSE) || defined(CONFIG_SOFT_WATCHDOG) || \
    defined (CONFIG_AMIGAMOUSE) || defined (CONFIG_ATARIMOUSE) || \
    defined (CONFIG_PCWATCHDOG) || \
    defined (CONFIG_APM) || defined (CONFIG_RTC) || \
    defined (CONFIG_SUN_MOUSE) || defined (CONFIG_NVRAM)
	misc_init();
#endif
#ifdef CONFIG_SOUND
	soundcard_init();
#endif
#ifdef CONFIG_JOYSTICK
	/*
	 *	Some joysticks only appear when the soundcard they are
	 *	connected too is confgured. Keep the sound/joystick ordering.
	 */
	joystick_init();
#endif	
#if CONFIG_QIC02_TAPE
	qic02_tape_init();
#endif
#if CONFIG_ISDN
	isdn_init();
#endif
#ifdef CONFIG_FTAPE
	ftape_init();
#endif
	return 0;
}
