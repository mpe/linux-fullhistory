/*
 *	linux/kernel/ioport.c
 *
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

static unsigned long ioport_registrar[IO_BITMAP_SIZE] = {0, /* ... */};
#define IOPORTNAMES_NUM 32
#define IOPORTNAMES_LEN 26
struct { int from;
	 int num;	
	 int flags;
	 char name[IOPORTNAMES_LEN];
	} ioportnames[IOPORTNAMES_NUM];

#define _IODEBUG

#ifdef IODEBUG
static char * ios(unsigned long l)
{
	static char str[33] = { '\0' };
	int i;
	unsigned long mask;

	for (i = 0, mask = 0x80000000; i < 32; ++i, mask >>= 1)
		str[i] = (l & mask) ? '1' : '0';
	return str;
}

static void dump_io_bitmap(void)
{
	int i, j;
	int numl = sizeof(current->tss.io_bitmap) >> 2;

	for (i = j = 0; j < numl; ++i)
	{
		printk("%4d [%3x]: ", 64*i, 64*i);
		printk("%s ", ios(current->tss.io_bitmap[j++]));
		if (j < numl)
			printk("%s", ios(current->tss.io_bitmap[j++]));
		printk("\n");
	}
}
#endif

/* Set EXTENT bits starting at BASE in BITMAP to value TURN_ON. */
asmlinkage void set_bitmap(unsigned long *bitmap, short base, short extent, int new_value)
{
	int mask;
	unsigned long *bitmap_base = bitmap + (base >> 5);
	unsigned short low_index = base & 0x1f;
	int length = low_index + extent;

	if (low_index != 0) {
		mask = (~0 << low_index);
		if (length < 32)
				mask &= ~(~0 << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
		length -= 32;
	}

	mask = (new_value ? ~0 : 0);
	while (length >= 32) {
		*bitmap_base++ = mask;
		length -= 32;
	}

	if (length > 0) {
		mask = ~(~0 << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
	}
}

/* Check for set bits in BITMAP starting at BASE, going to EXTENT. */
asmlinkage int check_bitmap(unsigned long *bitmap, short base, short extent)
{
	int mask;
	unsigned long *bitmap_base = bitmap + (base >> 5);
	unsigned short low_index = base & 0x1f;
	int length = low_index + extent;

	if (low_index != 0) {
		mask = (~0 << low_index);
		if (length < 32)
				mask &= ~(~0 << length);
		if (*bitmap_base++ & mask)
			return 1;
		length -= 32;
	}
	while (length >= 32) {
		if (*bitmap_base++ != 0)
			return 1;
		length -= 32;
	}

	if (length > 0) {
		mask = ~(~0 << length);
		if (*bitmap_base++ & mask)
			return 1;
	}
	return 0;
}

/*
 * This generates the report for /proc/ioports
 */
int get_ioport_list(char *buf)
{       int i=0,len=0;
	while(i<IOPORTNAMES_LEN && len<4000)
	{	if(ioportnames[i].flags)
			len+=sprintf(buf+len,"%04x-%04x : %s\n",
				ioportnames[i].from,
				ioportnames[i].from+ioportnames[i].num-1,
				ioportnames[i].name);
		i++;
	}
        if(len>=4000) 
		len+=sprintf(buf+len,"4k-Limit reached!\n");
        return len;
}

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	if (from + num <= from)
		return -EINVAL;
	if (from + num > IO_BITMAP_SIZE*32)
		return -EINVAL;
	if (!suser())
		return -EPERM;

#ifdef IODEBUG
	printk("io: from=%d num=%d %s\n", from, num, (turn_on ? "on" : "off"));
#endif
	set_bitmap((unsigned long *)current->tss.io_bitmap, from, num, !turn_on);
	return 0;
}

unsigned int *stack;

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 *
 * Here we just change the eflags value on the stack: we allow
 * only the super-user to do it. This depends on the stack-layout
 * on system-call entry - see also fork() and the signal handling
 * code.
 */
asmlinkage int sys_iopl(long ebx,long ecx,long edx,
	     long esi, long edi, long ebp, long eax, long ds,
	     long es, long fs, long gs, long orig_eax,
	     long eip,long cs,long eflags,long esp,long ss)
{
	unsigned int level = ebx;

	if (level > 3)
		return -EINVAL;
	if (!suser())
		return -EPERM;
	*(&eflags) = (eflags & 0xffffcfff) | (level << 12);
	return 0;
}

/*
 * This is the 'old' snarfing worker function
 */
void do_snarf_region(unsigned int from, unsigned int num)
{
        if (from > IO_BITMAP_SIZE*32)
                return;
        if (from + num > IO_BITMAP_SIZE*32)
                num = IO_BITMAP_SIZE*32 - from;
        set_bitmap(ioport_registrar, from, num, 1);
        return;
}

/*
 * Call this from the device driver to register the ioport region.
 */
void register_iomem(unsigned int from, unsigned int num, char *name)
{	
	int i=0;
	while(ioportnames[i].flags && i<IOPORTNAMES_NUM)
	 i++;
	if(i==IOPORTNAMES_NUM)
		printk("warning:ioportname-table is full");
	else
	{	
	 	strncpy(ioportnames[i].name,name,IOPORTNAMES_LEN);
		ioportnames[i].name[IOPORTNAMES_LEN-1]=(char)0;
		ioportnames[i].from=from;
		ioportnames[i].num=num;
		ioportnames[i].flags=1;
	}
	do_snarf_region(from,num);
}

/*
 * This is for compatibilty with older drivers.
 * It can be removed when all driver call the new function.
 */
void snarf_region(unsigned int from, unsigned int num)
{	register_iomem(from,num,"No name given.");
}

/*
 * The worker for releasing
 */
void do_release_region(unsigned int from, unsigned int num)
{
        if (from > IO_BITMAP_SIZE*32)
                return;
        if (from + num > IO_BITMAP_SIZE*32)
                num = IO_BITMAP_SIZE*32 - from;
        set_bitmap(ioport_registrar, from, num, 0);
        return;
}

/* 
 * Call this when the device driver is unloaded
 */
void release_region(unsigned int from, unsigned int num)
{	int i=0;
	while(i<IOPORTNAMES_NUM)
	{	if(ioportnames[i].from==from && ioportnames[i].num==num)
			ioportnames[i].flags=0;	
		i++;
	}
	do_release_region(from,num);
}

/*
 * Call this to check the ioport region before probing
 */
int check_region(unsigned int from, unsigned int num)
{
	if (from > IO_BITMAP_SIZE*32)
		return 0;
	if (from + num > IO_BITMAP_SIZE*32)
		num = IO_BITMAP_SIZE*32 - from;
	return check_bitmap(ioport_registrar, from, num);
}

/* Called from init/main.c to reserve IO ports. */
void reserve_setup(char *str, int *ints)
{
	int i;

	for (i = 1; i < ints[0]; i += 2)
		register_iomem(ints[i], ints[i+1],"reserved");
}
