/*
 * drivers/sbus/char/jsflash.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds	(drivers/char/mem.c)
 *  Copyright (C) 1997  Eddie C. Dost		(drivers/sbus/char/flash.c)
 *  Copyright (C) 1999  Pete Zaitcev
 *
 * This driver is used to program OS into a Flash SIMM on
 * Krups and Espresso platforms.
 *
 * It is anticipated that programming an OS Flash will be a routine
 * procedure. In the same time it is exeedingly dangerous because
 * a user can program its OBP flash with OS image and effectively
 * kill the machine.
 *
 * This driver uses an interface different from Eddie's flash.c
 * as a silly safeguard.
 *
 * XXX The flash.c manipulates page caching characteristics in a certain
 * dubious way; also it assumes that remap_page_range() can remap
 * PCI bus locations, which may be false. ioremap() must be used
 * instead. We should discuss this.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/string.h>
#if 0	/* P3 from mem.c */
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/raw.h>
#include <linux/capability.h>
#endif

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#if 0	/* P3 from mem.c */
#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/ebus.h>
#endif
#include <asm/pcic.h>
#include <asm/oplib.h>

#include <asm/jsflash.h>		/* ioctl arguments. <linux/> ?? */
#define JSFIDSZ		(sizeof(struct jsflash_ident_arg))
#define JSFPRGSZ	(sizeof(struct jsflash_program_arg))

/*
 * Our device numbers have no business in system headers.
 * The only thing a user knows is the device name /dev/jsflash.
 */
#define JSF_MINOR	178

/*
 * Access functions.
 * We could ioremap(), but it's easier this way.
 */
static unsigned int jsf_inl(unsigned long addr)
{
	unsigned long retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
				"=r" (retval) :
				"r" (addr), "i" (ASI_M_BYPASS));
        return retval;
}

static void jsf_outl(unsigned long addr, __u32 data)
{

	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
				"r" (data), "r" (addr), "i" (ASI_M_BYPASS) :
				"memory");
}

/*
 * soft carrier
 */
struct jsflash {
	unsigned long base;
	unsigned long size;
	unsigned long busy;		/* In use? */
	struct jsflash_ident_arg id;
};

/*
 * We do not map normal memory or obio as a safety precaution.
 * But offsets are real, for ease of userland programming.
 */
#define JSF_BASE_TOP	0x30000000
#define JSF_BASE_ALL	0x20000000

#define JSF_BASE_JK	0x20400000

/*
 * Let's pretend we may have several of these...
 */
static struct jsflash jsf0;

/*
 * Wait for AMD to finish its embedded algorithm.
 * We use the Toggle bit DQ6 (0x40) because it does not
 * depend on the data value as /DATA bit DQ7 does.
 *
 * XXX Do we need any timeout here?
 */
static void jsf_wait(unsigned long p) {
	unsigned int x1, x2;

	for (;;) {
		x1 = jsf_inl(p);
		x2 = jsf_inl(p);
		if ((x1 & 0x40404040) == (x2 & 0x40404040)) return;
	}
}

/*
 * Programming will only work if Flash is clean,
 * we leave it to the programmer application.
 *
 * AMD must be programmed one byte at a time;
 * thus, Simple Tech SIMM must be written 4 bytes at a time.
 *
 * Write waits for the chip to become ready after the write
 * was finished. This is done so that application would read
 * consistent data after the write is done.
 */
static void jsf_write4(unsigned long fa, u32 data) {

	jsf_outl(fa, 0xAAAAAAAA);		/* Unlock 1 Write 1 */
	jsf_outl(fa, 0x55555555);		/* Unlock 1 Write 2 */
	jsf_outl(fa, 0xA0A0A0A0);		/* Byte Program */
	jsf_outl(fa, data);

	jsf_wait(fa);
}

/*
 * The memory devices use the full 32/64 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
static loff_t jsf_lseek(struct file * file, loff_t offset, int orig)
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

/*
 * P3: OS SIMM Cannot be read in other size but a 32bits word.
 */
static ssize_t jsf_read(struct file * file, char * buf, 
    size_t togo, loff_t *ppos)
{
	unsigned long p = *ppos;
	char *tmp = buf;

	union byte4 {
		char s[4];
		unsigned int n;
	} b;

	if (verify_area(VERIFY_WRITE, buf, togo))
		return -EFAULT; 

	if (p < JSF_BASE_ALL || p >= JSF_BASE_TOP) {
		return 0;
	}

	if ((p + togo) < p	/* wrap */
	   || (p + togo) >= JSF_BASE_TOP) {
		togo = JSF_BASE_TOP - p;
	}

	if (p < JSF_BASE_ALL && togo != 0) {
#if 0 /* __bzero XXX */
		size_t x = JSF_BASE_ALL - p;
		if (x > togo) x = togo;
		clear_user(tmp, x);
		tmp += x;
		p += x;
		togo -= x;
#else
		/*
		 * Implementation of clear_user() calls __bzero
		 * without regard to modversions,
		 * so we cannot build a module.
		 */
		return 0;
#endif
	}

	while (togo >= 4) {
		togo -= 4;
		b.n = jsf_inl(p);
		copy_to_user(tmp, b.s, 4);
		tmp += 4;
		p += 4;
	}

	/*
	 * XXX Small togo may remain if 1 byte is ordered.
	 * It would be nice if we did a word size read and unpacked it.
	 */

	*ppos = p;
	return tmp-buf;
}

static ssize_t jsf_write(struct file * file, const char * buf,
    size_t count, loff_t *ppos)
{
	return -ENOSPC;
}

/*
 */
static int jsf_ioctl_erase(unsigned long arg)
{
	unsigned long p;

	/* p = jsf0.base;	hits wrong bank */
	p = 0x20400000;

	jsf_outl(p, 0xAAAAAAAA);		/* Unlock 1 Write 1 */
	jsf_outl(p, 0x55555555);		/* Unlock 1 Write 2 */
	jsf_outl(p, 0x80808080);		/* Erase setup */
	jsf_outl(p, 0xAAAAAAAA);		/* Unlock 2 Write 1 */
	jsf_outl(p, 0x55555555);		/* Unlock 2 Write 2 */
	jsf_outl(p, 0x10101010);		/* Chip erase */

#if 0
	/*
	 * This code is ok, except that counter based timeout
	 * has no place in this world. Let's just drop timeouts...
	 */
	{
		int i;
		__u32 x;
		for (i = 0; i < 1000000; i++) {
			x = jsf_inl(p);
			if ((x & 0x80808080) == 0x80808080) break;
		}
		if ((x & 0x80808080) != 0x80808080) {
			printk("jsf0: erase timeout with 0x%08x\n", x);
		} else {
			printk("jsf0: erase done with 0x%08x\n", x);
		}
	}
#else
	jsf_wait(p);
#endif

	return 0;
}

/*
 * Program a block of flash.
 * Very simple because we can do it byte by byte anyway.
 */
static int jsf_ioctl_program(unsigned long arg)
{
	struct jsflash_program_arg abuf;
	char *uptr;
	unsigned long p;
	unsigned int togo;
	union {
		unsigned int n;
		char s[4];
	} b;

	if (verify_area(VERIFY_READ, (void *)arg, JSFPRGSZ))
		return -EFAULT; 
	copy_from_user(&abuf, (char *)arg, JSFPRGSZ);
	p = abuf.off;
	togo = abuf.size;
	if ((togo & 3) || (p & 3)) return -EINVAL;

	uptr = (char *) abuf.data;
	if (verify_area(VERIFY_READ, uptr, togo))
		return -EFAULT;
	while (togo != 0) {
		--togo;
		copy_from_user(&b.s[0], uptr, 4);
		jsf_write4(p, b.n);
		p += 4;
		uptr += 4;
	}

	return 0;
}

static int jsf_ioctl(struct inode *inode, struct file *f, unsigned int cmd,
    unsigned long arg)
{
	int error = -ENOTTY;

	switch (cmd) {
	case JSFLASH_IDENT:
		if (verify_area(VERIFY_WRITE, (void *)arg, JSFIDSZ))
			return -EFAULT; 
		copy_to_user(arg, &jsf0.id, JSFIDSZ);
		error = 0;
		break;
	case JSFLASH_ERASE:
		error = jsf_ioctl_erase(arg);
		break;
	case JSFLASH_PROGRAM:
		error = jsf_ioctl_program(arg);
		break;
	}

	return error;
}

static int jsf_mmap(struct file * file, struct vm_area_struct * vma)
{
	return -ENXIO;
}

static int jsf_open(struct inode * inode, struct file * filp)
{

	if (jsf0.base == 0) return -ENXIO;
	if (test_and_set_bit(0, (void *)&jsf0.busy) != 0)
		return -EBUSY;

	MOD_INC_USE_COUNT;
	return 0;	/* XXX What security? */
}

static int jsf_release(struct inode *inode, struct file *file)
{

	MOD_DEC_USE_COUNT;

	jsf0.busy = 0;
	return 0;
}

static struct file_operations jsf_fops = {
	llseek:		jsf_lseek,
	read:		jsf_read,
	write:		jsf_write,
	ioctl:		jsf_ioctl,
	mmap:		jsf_mmap,
	open:		jsf_open,
	release:	jsf_release,
};

static struct miscdevice jsf_dev = { JSF_MINOR, "jsflash", &jsf_fops };

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
int __init jsflash_init(void)
#endif
{
	int rc;
	char banner[128];

	/* FIXME: Really autodetect things */
	prom_getproperty(prom_root_node, "banner-name", banner, 128);
	if (strcmp (banner, "JavaStation-NC") && strcmp (banner, "JavaStation-E"))
		return -ENXIO;

	/* extern enum sparc_cpu sparc_cpu_model; */ /* in <asm/system.h> */
	if (sparc_cpu_model == sun4m && jsf0.base == 0) {
		/* XXX Autodetect */
		/*
		 * We do not want to use PROM properties;
		 * They are faked by PROLL anyways.
		 */
		jsf0.base = JSF_BASE_JK;
		jsf0.size = 0x00800000;		/* 8M */

		jsf0.id.off = JSF_BASE_ALL;
		jsf0.id.size = 0x01000000;	/* 16M - all segments */
		strcpy(jsf0.id.name, "Krups_all");

		printk("Espresso Flash @0x%lx\n", jsf0.base);
	}

	if ((rc = misc_register(&jsf_dev)) != 0) {
		printk(KERN_ERR "jsf: unable to get misc minor %d\n",
		    JSF_MINOR);
		jsf0.base = 0;
		return rc;
	}
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{

	/* for (all probed units) {  } */
	if (jsf0.busy)
		printk("jsf0: cleaning busy unit\n");
	jsf0.base = 0;
	jsf0.busy = 0;

	misc_deregister(&jsf_dev);
}
#endif
