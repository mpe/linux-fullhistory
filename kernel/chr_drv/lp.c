/*
 $Header: /usr/src/linux/kernel/chr_drv/lp.c,v 1.9 1992/01/06 16:11:19
  james_r_wiegand Exp james_r_wiegand $
*/

#define __LP_C__
#include <linux/lp.h>

#include <checkpoint.h>

int lp_reset(int minor)
{
	int testvalue;

	/* reset value */
	outb(0, LP_B(minor)+2);
	for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
		;
	outb(LP_PSELECP | LP_PINITP, LP_B(minor)+2);
	return LP_S(minor);
}

void lp_init(void)
{
	int offset = 0;
	unsigned int testvalue = 0;
	int count = 0;

	/* take on all known port values */
	for (offset = 0; offset < LP_NO; offset++) {
		/* write to port & read back to check */
		outb( LP_DUMMY, LP_B(offset));
		for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
			;
		testvalue = inb(LP_B(offset));
		if (testvalue != 255) {
			LP_F(offset) |= LP_EXIST;
			lp_reset(offset);
			printk("lp_init: lp%d exists (%d)\n", offset, testvalue);
			count++;
		}
	}
	if (count == 0)
		printk("lp_init: no lp devices found\n");
}

int lp_char(char lpchar, int minor)
{
	int retval = 0;
	unsigned long count  = 0; 

	outb(lpchar, LP_B(minor));
	do {
		retval = LP_S(minor);
		schedule(); 
		count ++;
	} while(!(retval & LP_PBUSY) && count < LP_TIMEOUT);
	if (count == LP_TIMEOUT) {
		printk("lp%d timeout\n\r", minor);
		return 0;
	}
  /* control port pr_table[0]+2 take strobe high */
	outb(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_B( minor ) + 2 ));
  /* take strobe low */
	outb(( LP_PSELECP | LP_PINITP ), ( LP_B( minor ) + 2 ));
  /* get something meaningful for return value */
	return LP_S(minor);
}

int lp_write(unsigned minor, char *buf, int count)
{
	int  retval;
	int  loop;
	int  tcount;
	char c, *temp = buf;

	if (minor > LP_NO - 1)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;

/* if we aren't the "owner task", check if the old owner has died... */
	if (LP_T(minor) != current->pid && (LP_F(minor) & LP_BUSY)) {
		for(tcount = 0; tcount < NR_TASKS; tcount++) { 
			if (!task[tcount])
				continue;
			if (task[tcount]->state == TASK_ZOMBIE)
				continue;
			if (task[tcount]->pid == LP_T(minor)) {
			        tcount = -1;
				break;
			}
		}
		if (tcount == -1)
			return -EBUSY;
	}

	LP_T(minor) = current->pid;
	LP_F(minor) |= LP_BUSY;
	LP_R(minor) = count;
	temp = buf;

	for (loop = 0 ; loop < count ; loop++, temp++) {
		c = get_fs_byte(temp);
		retval = lp_char(c, minor);
		LP_R(minor)--;
		if (retval & LP_POUTPA) {
			LP_F(minor) |= LP_NOPA;
			return loop?loop:-ENOSPC;
		} else
			LP_F(minor) &= ~LP_NOPA;

		if (!(retval & LP_PSELECD)) {
			LP_F(minor) &= ~LP_SELEC;
			return loop?loop:-EFAULT;
		} else
			LP_F(minor) &= ~LP_SELEC;

    /* not offline or out of paper. on fire? */
		if (!(retval & LP_PERRORP)) {
			LP_F(minor) |= LP_ERR;
			return loop?loop:-EIO;
		} else
			LP_F(minor) &= ~LP_SELEC;
	}
	return count;
}
