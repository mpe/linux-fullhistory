#include <linux/sched.h>
#include <linux/interrupt.h>

unsigned int spinlocks[32];

static void show_stack(unsigned int *stack)
{
	int i;

	for (i = 0; i < 40; i++) {
		extern int get_options, __start_fixup;
		unsigned int p = stack[i];
		if (p >= (unsigned int) &get_options && p < (unsigned int)&__start_fixup)
			printk("[<%08x>] ", p);
	}
}

void __putlock_negative(
	unsigned int ecx,
	unsigned int edx,
	unsigned int eax,
	unsigned int from_where)
{
	static int count = 0;

	spinlocks[current->processor] = 0;

	if (count < 5) {
		count++;
		printk("negative putlock from %x\n", from_where);
		show_stack(&ecx);
	}
}

void __check_locks(unsigned int type)
{
	static int warned = 0;
	
	if (warned < 5) {
		unsigned int from_where = (&type)[-1];
		unsigned int this_cpu = current->processor;
		int bad_irq = 0;

		if (type) {
			unsigned long flags;
			__save_flags(flags);
			if (!(flags & 0x200) || (global_irq_holder == this_cpu))
				bad_irq = 1;
		}

		if (spinlocks[this_cpu] ||
		    local_irq_count[this_cpu] ||
		    local_bh_count[this_cpu] ||
		    bad_irq) {
			warned++;
			printk("scheduling with spinlocks=%d at %x\n", spinlocks[this_cpu], from_where);
			show_stack(&type);
		}
	}
}


