/* Parallel port /proc interface code.
 * 
 * Authors: David Campbell <campbell@torque.net>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *          Philip Blundell <philb@gnu.org>
 *          Andrea Arcangeli <arcangeli@mbox.queen.it>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell
 */

#include <linux/stddef.h>
#include <linux/tasks.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/parport.h>

struct proc_dir_entry *base = NULL;

extern void parport_null_intr_func(int irq, void *dev_id, struct pt_regs *regs);

static int irq_write_proc(struct file *file, const char *buffer,
					  unsigned long count, void *data)
{
	unsigned int newirq, oldirq;
	struct parport *pp = (struct parport *)data;
	
	if (count > 5 )  /* more than 4 digits + \n for a irq 0x?? 0?? ??  */
		return -EOVERFLOW;

	if (buffer[0] < 32 || !strncmp(buffer, "none", 4)) {
		newirq = PARPORT_IRQ_NONE;
	} else {
		if (buffer[0] == '0') {
			if (buffer[1] == 'x')
				newirq = simple_strtoul(&buffer[2], 0, 16);
			else
				newirq = simple_strtoul(&buffer[1], 0, 8);
		} else {
			newirq = simple_strtoul(buffer, 0, 10);
		}
	}

	if (newirq >= NR_IRQS)
		return -EOVERFLOW;

	if (pp->irq != PARPORT_IRQ_NONE && !(pp->flags & PARPORT_FLAG_COMA)) {
		if (pp->cad != NULL && pp->cad->irq_func != NULL)
			free_irq(pp->irq, pp->cad->private);
		else
			free_irq(pp->irq, NULL);
	}

	oldirq = pp->irq;
	pp->irq = newirq;

	if (pp->irq != PARPORT_IRQ_NONE && !(pp->flags & PARPORT_FLAG_COMA)) { 
		struct pardevice *cad = pp->cad;

		if (cad == NULL)
			request_irq(pp->irq, parport_null_intr_func,
				    SA_INTERRUPT, pp->name, NULL);
		else
			request_irq(pp->irq, cad->irq_func ? cad->irq_func :
				    parport_null_intr_func, SA_INTERRUPT,
				    cad->name, cad->private);
	}

	if (oldirq != PARPORT_IRQ_NONE && newirq == PARPORT_IRQ_NONE &&
	    pp->cad != NULL && pp->cad->irq_func != NULL)
		pp->cad->irq_func(pp->irq, pp->cad->private, NULL);

	return count;
}

static int irq_read_proc(char *page, char **start, off_t off,
					 int count, int *eof, void *data)
{
	struct parport *pp = (struct parport *)data;
	int len;
	
	if (pp->irq == PARPORT_IRQ_NONE)
		len = sprintf(page, "none\n");
	else
		len = sprintf(page, "%d\n", pp->irq);
	
	*start = 0;
	*eof   = 1;
	return len;
}

static int devices_read_proc(char *page, char **start, off_t off,
					 int count, int *eof, void *data)
{
	struct parport *pp = (struct parport *)data;
	struct pardevice *pd1;
	int len=0;

	for (pd1 = pp->devices; pd1 ; pd1 = pd1->next) {
		if (pd1 == pp->cad)
			page[len++] = '+';
		else
			page[len++] = ' ';

		len += sprintf(page+len, "%s", pd1->name);

		page[len++] = '\n';
	}
		
	*start = 0;
	*eof   = 1;
	return len;
}

static int hardware_read_proc(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	struct parport *pp = (struct parport *)data;
	int len=0;
	
	len += sprintf(page+len, "base:\t0x%lx\n",pp->base);

	if (pp->irq == PARPORT_IRQ_NONE)
		len += sprintf(page+len, "irq:\tnone\n");
	else
		len += sprintf(page+len, "irq:\t%d\n",pp->irq);

	if (pp->dma == PARPORT_DMA_NONE)
		len += sprintf(page+len, "dma:\tnone\n");
	else
		len += sprintf(page+len, "dma:\t%d\n",pp->dma);

	len += sprintf(page+len, "modes:\t");
	{
#define printmode(x) {if(pp->modes&PARPORT_MODE_PC##x){len+=sprintf(page+len,"%s%s",f?",":"",#x);f++;}}
		int f = 0;
		printmode(SPP);
		printmode(PS2);
		printmode(EPP);
		printmode(ECP);
		printmode(ECPEPP);
		printmode(ECPPS2);
#undef printmode
	}
	page[len++] = '\n';

	*start = 0;
	*eof   = 1;
	return len;
}

static inline void destroy_proc_entry(struct proc_dir_entry *root, 
				      struct proc_dir_entry **d)
{
	proc_unregister(root, (*d)->low_ino);
	kfree(*d);
	*d = NULL;
}

static struct proc_dir_entry *new_proc_entry(const char *name, mode_t mode,
					     struct proc_dir_entry *parent,
					     unsigned short ino)
{
	struct proc_dir_entry *ent;

	ent = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
	if (!ent)
		return NULL;

	memset(ent, 0, sizeof(struct proc_dir_entry));
	
	if (mode == S_IFDIR)
		mode |= S_IRUGO | S_IXUGO;
	else if (mode == 0)
		mode = S_IFREG | S_IRUGO;

	ent->low_ino = ino;
	ent->name = name;
	ent->namelen = strlen(name);
	ent->mode = mode;

	if (S_ISDIR(mode)) 
		ent->nlink = 2;
	else
		ent->nlink = 1;

	proc_register(parent, ent);
	
	return ent;
}


int parport_proc_init(void)
{
	base = new_proc_entry("parport", S_IFDIR, &proc_root,PROC_PARPORT);

	if (base == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/parport.\n");
		return 0;
	}

	return 1;
}

void parport_proc_cleanup(void)
{
	if (base) proc_unregister(&proc_root,base->low_ino);
	base = NULL;
}

int parport_proc_register(struct parport *pp)
{
	static const char *proc_msg = KERN_ERR "%s: Trouble with /proc.\n";

	memset(&pp->pdir, 0, sizeof(struct parport_dir));

	if (base == NULL) {
		printk(KERN_ERR "parport_proc not initialised yet.\n");
		return 1;
	}
	
	strncpy(pp->pdir.name, pp->name + strlen("parport"), 
		sizeof(pp->pdir.name));

	pp->pdir.entry = new_proc_entry(pp->pdir.name, S_IFDIR, base, 0);
	if (pp->pdir.entry == NULL) {
		printk(proc_msg, pp->name);
		return 1;
	}

	pp->pdir.irq = new_proc_entry("irq", S_IFREG | S_IRUGO | S_IWUSR, 
				      pp->pdir.entry, 0);
	if (pp->pdir.irq == NULL) {
		printk(proc_msg, pp->name);
		destroy_proc_entry(base, &pp->pdir.entry);
		return 1;
	}
	pp->pdir.irq->read_proc = irq_read_proc;
	pp->pdir.irq->write_proc = irq_write_proc;
	pp->pdir.irq->data = pp;
	
	pp->pdir.devices = new_proc_entry("devices", 0, pp->pdir.entry, 0);
	if (pp->pdir.devices == NULL) {
		printk(proc_msg, pp->name);
		destroy_proc_entry(pp->pdir.entry, &pp->pdir.irq);
		destroy_proc_entry(base, &pp->pdir.entry);
		return 1;
	}
	pp->pdir.devices->read_proc = devices_read_proc;
	pp->pdir.devices->data = pp;
	
	pp->pdir.hardware = new_proc_entry("hardware", 0, pp->pdir.entry, 0);
	if (pp->pdir.hardware == NULL) {
		printk(proc_msg, pp->name);
		destroy_proc_entry(pp->pdir.entry, &pp->pdir.devices);
		destroy_proc_entry(pp->pdir.entry, &pp->pdir.irq);
		destroy_proc_entry(base, &pp->pdir.entry);
		return 1;
	}
	pp->pdir.hardware->read_proc = hardware_read_proc;
	pp->pdir.hardware->data = pp;

	return 0;
}

int parport_proc_unregister(struct parport *pp)
{
	if (pp->pdir.entry) {
		if (pp->pdir.irq) 
			destroy_proc_entry(pp->pdir.entry, &pp->pdir.irq);
		
		if (pp->pdir.devices) 
			destroy_proc_entry(pp->pdir.entry, &pp->pdir.devices);
		
		if (pp->pdir.hardware)
			destroy_proc_entry(pp->pdir.entry, &pp->pdir.hardware);

		if (pp->pdir.probe)
			destroy_proc_entry(pp->pdir.entry, &pp->pdir.probe);
		
		destroy_proc_entry(base, &pp->pdir.entry);
	}
	
	return 0;
}
