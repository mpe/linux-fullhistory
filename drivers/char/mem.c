/*
 *  linux/drivers/char/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/tpqic02.h>
#include <linux/ftape.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/joystick.h>
#include <linux/i2c.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#ifdef CONFIG_SOUND
void soundcore_init(void);
#ifdef CONFIG_SOUND_OSS
void soundcard_init(void);
#endif
#ifdef CONFIG_DMASOUND
void dmasound_init(void);
#endif
#endif
#ifdef CONFIG_SPARCAUDIO
extern int sparcaudio_init(void);
#endif
#ifdef CONFIG_ISDN
int isdn_init(void);
#endif
#ifdef CONFIG_VIDEO_DEV
extern int videodev_init(void);
#endif
#ifdef CONFIG_FB
extern void fbmem_init(void);
#endif
#ifdef CONFIG_PROM_CONSOLE
extern void prom_con_init(void);
#endif
#ifdef CONFIG_MDA_CONSOLE
extern void mda_console_init(void);
#endif
#if defined(CONFIG_PPC) || defined(CONFIG_MAC)
extern void adbdev_init(void);
#endif

static ssize_t do_write_mem(struct file * file, void *p, unsigned long realp,
			    const char * buf, size_t count, loff_t *ppos)
{
	ssize_t written;

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
	if (copy_from_user(p, buf, count)) 
		return -EFAULT;
	written += count;
	*ppos += written;
	return written;
}


/*
 * This funcion reads the *physical* memory. The f_pos points directly to the 
 * memory location. 
 */
static ssize_t read_mem(struct file * file, char * buf,
			size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned long end_mem;
	ssize_t read;
	
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
	if (copy_to_user(buf, __va(p), count))
		return -EFAULT;
	read += count;
	*ppos += read;
	return read;
}

static ssize_t write_mem(struct file * file, const char * buf, 
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned long end_mem;

	end_mem = __pa(high_memory);
	if (p >= end_mem)
		return 0;
	if (count > end_mem - p)
		count = end_mem - p;
	return do_write_mem(file, __va(p), p, buf, count, ppos);
}

/*
 * This should probably be per-architecture in <asm/pgtable.h>
 */
static inline unsigned long pgprot_noncached(unsigned long prot)
{
#if defined(__i386__)
	if (boot_cpu_data.x86 > 3)
		prot |= _PAGE_PCD;
#elif defined(__powerpc__)
	prot |= _PAGE_NO_CACHE | _PAGE_GUARDED;
#elif defined(__mc68000__)
	if (CPU_IS_020_OR_030)
		prot |= _PAGE_NOCACHE030;
	/* Use no-cache mode, serialized */
	if (CPU_IS_040_OR_060)
		prot = (prot & _CACHEMASK040) | _PAGE_NOCACHE_S;
#elif defined(__mips__)
	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;
#endif

	return prot;
}

static int mmap_mem(struct file * file, struct vm_area_struct * vma)
{
	unsigned long offset = vma->vm_offset;

	if (offset & ~PAGE_MASK)
		return -ENXIO;

	/*
	 * Accessing memory above the top the kernel knows about or
	 * through a file pointer that was marked O_SYNC will be
	 * done non-cached.
	 *
	 * Set VM_IO, as this is likely a non-cached access to an
	 * I/O area, and we don't want to include that in a core
	 * file.
	 */
	if (offset >= __pa(high_memory) || (file->f_flags & O_SYNC)) {
		pgprot_val(vma->vm_page_prot) = pgprot_noncached(pgprot_val(vma->vm_page_prot));
		vma->vm_flags |= VM_IO;
	}
	if (remap_page_range(vma->vm_start, offset, vma->vm_end-vma->vm_start,
			     vma->vm_page_prot))
		return -EAGAIN;
	vma->vm_file = file;
	file->f_count++;
	return 0;
}

/*
 * This function reads the *virtual* memory as seen by the kernel.
 */
static ssize_t read_kmem(struct file *file, char *buf, 
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read = 0;
	ssize_t virtr;
		
	if (p < (unsigned long) high_memory) { 
		read = count;
		if (count > (unsigned long) high_memory - p)
			read = (unsigned long) high_memory - p;

#if defined(__sparc__) || defined(__mc68000__)
		/* we don't have page 0 mapped on sparc and m68k.. */
		if (p < PAGE_SIZE && read > 0) {
			size_t tmp = PAGE_SIZE - p;
			if (tmp > read) tmp = read;
			clear_user(buf, tmp);
			buf += tmp;
			p += tmp;
			read -= tmp;
			count -= tmp;
		}
#endif
		copy_to_user(buf, (char *)p, read);
		p += read;
		buf += read;
		count -= read;
	}

	virtr = vread(buf, (char *)p, count);
	if (virtr < 0)
		return virtr;
	*ppos += p + virtr;
	return virtr + read;
}

/*
 * This function writes to the *virtual* memory as seen by the kernel.
 */
static ssize_t write_kmem(struct file * file, const char * buf, 
			  size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;

	if (p >= (unsigned long) high_memory)
		return 0;
	if (count > (unsigned long) high_memory - p)
		count = (unsigned long) high_memory - p;
	return do_write_mem(file, (void*)p, p, buf, count, ppos);
}

static ssize_t read_port(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	unsigned long i = *ppos;
	char *tmp = buf;

	if (verify_area(VERIFY_WRITE,buf,count))
		return -EFAULT; 
	while (count-- > 0 && i < 65536) {
		if (__put_user(inb(i),tmp) < 0) 
			return -EFAULT;  
		i++;
		tmp++;
	}
	*ppos = i;
	return tmp-buf;
}

static ssize_t write_port(struct file * file, const char * buf,
			  size_t count, loff_t *ppos)
{
	unsigned long i = *ppos;
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
	*ppos = i;
	return tmp-buf;
}

static ssize_t read_null(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t write_null(struct file * file, const char * buf,
			  size_t count, loff_t *ppos)
{
	return count;
}

/*
 * For fun, we are using the MMU for this.
 */
static inline size_t read_zero_pagealigned(char * buf, size_t size)
{
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long addr=(unsigned long)buf;

	mm = current->mm;
	/* Oops, this was forgotten before. -ben */
	down(&mm->mmap_sem);

	/* For private mappings, just map in zero pages. */
	for (vma = find_vma(mm, addr); vma; vma = vma->vm_next) {
		unsigned long count;

		if (vma->vm_start > addr || (vma->vm_flags & VM_WRITE) == 0)
			goto out_up;
		if (vma->vm_flags & VM_SHARED)
			break;
		count = vma->vm_end - addr;
		if (count > size)
			count = size;

		flush_cache_range(mm, addr, addr + count);
		zap_page_range(mm, addr, count);
        	zeromap_page_range(addr, count, PAGE_COPY);
        	flush_tlb_range(mm, addr, addr + count);

		size -= count;
		buf += count;
		addr += count;
		if (size == 0)
			goto out_up;
	}

	up(&mm->mmap_sem);
	
	/* The shared case is hard. Let's do the conventional zeroing. */ 
	do {
		unsigned long unwritten = clear_user(buf, PAGE_SIZE);
		if (unwritten)
			return size + unwritten - PAGE_SIZE;
		if (current->need_resched)
			schedule();
		buf += PAGE_SIZE;
		size -= PAGE_SIZE;
	} while (size);

	return size;
out_up:
	up(&mm->mmap_sem);
	return size;
}

static ssize_t read_zero(struct file * file, char * buf, 
			 size_t count, loff_t *ppos)
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

static int mmap_zero(struct file * file, struct vm_area_struct * vma)
{
	if (vma->vm_flags & VM_SHARED)
		return -EINVAL;
	if (zeromap_page_range(vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static ssize_t write_full(struct file * file, const char * buf,
			  size_t count, loff_t *ppos)
{
	return -ENOSPC;
}

/*
 * Special lseek() function for /dev/null and /dev/zero.  Most notably, you
 * can fopen() both devices with "a" now.  This was previously impossible.
 * -- SRB.
 */

static loff_t null_lseek(struct file * file, loff_t offset, int orig)
{
	return file->f_pos = 0;
}

/*
 * The memory devices use the full 32/64 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
static loff_t memory_lseek(struct file * file, loff_t offset, int orig)
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
}

#define mmap_kmem	mmap_mem
#define zero_lseek	null_lseek
#define full_lseek      null_lseek
#define write_zero	write_null
#define read_full       read_zero

static struct file_operations mem_fops = {
	memory_lseek,
	read_mem,
	write_mem,
	NULL,		/* mem_readdir */
	NULL,		/* mem_poll */
	NULL,		/* mem_ioctl */
	mmap_mem,
	NULL,		/* no special open code */
	NULL,		/* flush */
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
	NULL,		/* flush */
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
	NULL,		/* flush */
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
	NULL,		/* flush */
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
	NULL,		/* flush */
	NULL		/* no special release code */
};

static struct file_operations full_fops = {
	full_lseek,
	read_full,
	write_full,
	NULL,		/* full_readdir */
	NULL,		/* full_poll */
	NULL,		/* full_ioctl */	
	NULL,		/* full_mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
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
#if !defined(CONFIG_PPC) && !defined(__mc68000__)
		case 4:
			filp->f_op = &port_fops;
			break;
#endif
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
	NULL,		/* flush */
	NULL,		/* release */
	NULL		/* fsync */
};

__initfunc(int chr_dev_init(void))
{
	if (register_chrdev(MEM_MAJOR,"mem",&memory_fops))
		printk("unable to get major %d for memory devs\n", MEM_MAJOR);
	rand_initialize();
#if defined (CONFIG_FB)
	fbmem_init();
#endif
#if defined (CONFIG_PROM_CONSOLE)
	prom_con_init();
#endif
#if defined (CONFIG_MDA_CONSOLE)
	mda_console_init();
#endif
	tty_init();
#ifdef CONFIG_PRINTER
	lp_init();
#endif
#ifdef CONFIG_M68K_PRINTER
	lp_m68k_init();
#endif
	misc_init();
#ifdef CONFIG_SOUND
	soundcore_init();
#ifdef CONFIG_SOUND_OSS	
	soundcard_init();
#endif	
#ifdef CONFIG_DMASOUND
	dmasound_init();
#endif	
#endif
#ifdef CONFIG_SPARCAUDIO
	sparcaudio_init();
#endif
#ifdef CONFIG_JOYSTICK
	/*
	 *	Some joysticks only appear when the sound card they are
	 *	connected to is configured. Keep the sound/joystick ordering.
	 */
	js_init();
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
#ifdef CONFIG_VIDEO_BT848
	i2c_init();
#endif
#if defined(CONFIG_PPC) || defined(CONFIG_MAC)
	adbdev_init();
#endif
#ifdef CONFIG_VIDEO_DEV
	videodev_init();
#endif
	return 0;
}
