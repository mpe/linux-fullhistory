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

#define IOTABLE_SIZE 32
#define STR(x) #x

typedef struct resource_entry_t {
	u_long from, num;
	const char *name;
	struct resource_entry_t *next;
} resource_entry_t;

static resource_entry_t iolist = { 0, 0, "", NULL };

static resource_entry_t iotable[IOTABLE_SIZE];

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

/*
 * This generates the report for /proc/ioports
 */
int get_ioport_list(char *buf)
{
	resource_entry_t *p;
	int len = 0;

	for (p = iolist.next; (p) && (len < 4000); p = p->next)
		len += sprintf(buf+len, "%04lx-%04lx : %s\n",
			   p->from, p->from+p->num-1, p->name);
	if (p)
		len += sprintf(buf+len, "4K limit reached!\n");
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
 * The workhorse function: find where to put a new entry
 */
static resource_entry_t *find_gap(resource_entry_t *root,
				  u_long from, u_long num)
{
	unsigned long flags;
	resource_entry_t *p;
	
	if (from > from+num-1)
		return NULL;
	save_flags(flags);
	cli();
	for (p = root; ; p = p->next) {
		if ((p != root) && (p->from+p->num-1 >= from)) {
			p = NULL;
			break;
		}
		if ((p->next == NULL) || (p->next->from > from+num-1))
			break;
	}
	restore_flags(flags);
	return p;
}

/*
 * Call this from the device driver to register the ioport region.
 */
void request_region(unsigned int from, unsigned int num, const char *name)
{
	resource_entry_t *p;
	int i;

	for (i = 0; i < IOTABLE_SIZE; i++)
		if (iotable[i].num == 0)
			break;
	if (i == IOTABLE_SIZE)
		printk("warning: ioport table is full\n");
	else {
		p = find_gap(&iolist, from, num);
		if (p == NULL)
			return;
		iotable[i].name = name;
		iotable[i].from = from;
		iotable[i].num = num;
		iotable[i].next = p->next;
		p->next = &iotable[i];
		return;
	}
}

/*
 * This is for compatibilty with older drivers.
 * It can be removed when all driver call the new function.
 */
void snarf_region(unsigned int from, unsigned int num)
{
	request_region(from,num,"No name given.");
}

/* 
 * Call this when the device driver is unloaded
 */
void release_region(unsigned int from, unsigned int num)
{
	resource_entry_t *p, *q;

	for (p = &iolist; ; p = q) {
		q = p->next;
		if (q == NULL)
			break;
		if ((q->from == from) && (q->num == num)) {
			q->num = 0;
			p->next = q->next;
			return;
		}
	}
}

/*
 * Call this to check the ioport region before probing
 */
int check_region(unsigned int from, unsigned int num)
{
	return (find_gap(&iolist, from, num) == NULL) ? -EBUSY : 0;
}

/* Called from init/main.c to reserve IO ports. */
void reserve_setup(char *str, int *ints)
{
	int i;

	for (i = 1; i < ints[0]; i += 2)
		request_region(ints[i], ints[i+1], "reserved");
}
