/* Parallel port /proc interface code.
 * 
 * Authors: David Campbell <campbell@torque.net>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *          Philip Blundell <philb@gnu.org>
 *          Andrea Arcangeli
 *          Riccardo Facchetti <fizban@tin.it>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell
 *
 * Cleaned up include files - Russell King <linux@arm.uk.linux.org>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/parport.h>
#include <linux/ctype.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *base = NULL;

static int irq_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	int retval = -EINVAL;
	int newirq = PARPORT_IRQ_NONE;
	struct parport *pp = (struct parport *)data;
	int oldirq = pp->irq;

/*
 * We can have these valid cases:
 * 	"none" (count == 4 || count == 5)
 * 	decimal number (count == 2 || count == 3)
 * 	octal number (count == 3 || count == 4)
 * 	hex number (count == 4 || count == 5)
 * all other cases are -EINVAL
 *
 * Note: newirq is alredy set up to NONE.
 *
 * -RF
 */
	if (count > 5  || count < 1)
		goto out;

	if (isdigit(buffer[0]))
		newirq = simple_strtoul(buffer, NULL, 0);
	else if (strncmp(buffer, "none", 4) != 0) {
		if (buffer[0] < 32)
			/* Things like '\n' are harmless */
			retval = count;

		goto out;
	}

	retval = count;

	if (oldirq == newirq)
		goto out;

	if (pp->flags & PARPORT_FLAG_COMA)
		goto out_ok;

	retval = -EBUSY;

	/*
	 * Here we don' t need the irq version of spinlocks because
	 * the parport_lowlevel irq handler must not change the cad,
	 * and so has no one reason to write_lock() the cad_lock spinlock.
	 *						-arca
	 */
	read_lock(&pp->cad_lock);

	if (pp->cad)
	{
		read_unlock(&pp->cad_lock);
		return retval;
	}

	if (newirq != PARPORT_IRQ_NONE) { 
		retval = request_irq(newirq, pp->ops->interrupt,
				     0, pp->name, pp);
		if (retval)
		{
			read_unlock(&pp->cad_lock);
			return retval;
		}
	}

	if (oldirq != PARPORT_IRQ_NONE)
		free_irq(oldirq, pp);

	retval = count;

	read_unlock(&pp->cad_lock);

out_ok:
	pp->irq = newirq;

out:
	return retval;
}

static int irq_read_proc(char *page, char **start, off_t off,
					 int count, int *eof, void *data)
{
	struct parport *pp = (struct parport *)data;
	int len;
	
	if (pp->irq == PARPORT_IRQ_NONE) {
		len = sprintf(page, "none\n");
	} else {
#ifdef __sparc__
		len = sprintf(page, "%s\n", __irq_itoa(pp->irq));
#else
		len = sprintf(page, "%d\n", pp->irq);
#endif
	}
	
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

	if (pp->irq == PARPORT_IRQ_NONE) {
		len += sprintf(page+len, "irq:\tnone\n");
	} else {
#ifdef __sparc__
		len += sprintf(page+len, "irq:\t%s\n",__irq_itoa(pp->irq));
#else
		len += sprintf(page+len, "irq:\t%d\n",pp->irq);
#endif
	}

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

static int autoprobe_read_proc (char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	struct parport *pp = (struct parport *) data;
	int len = 0;
	const char *str;

	page[0] = '\0';

	if ((str = pp->probe_info.class_name) != NULL)
		len += sprintf (page+len, "CLASS:%s;\n", str);

	if ((str = pp->probe_info.model) != NULL)
		len += sprintf (page+len, "MODEL:%s;\n", str);

	if ((str = pp->probe_info.mfr) != NULL)
		len += sprintf (page+len, "MANUFACTURER:%s;\n", str);

	if ((str = pp->probe_info.description) != NULL)
		len += sprintf (page+len, "DESCRIPTION:%s;\n", str);

	if ((str = pp->probe_info.cmdset) != NULL)
		len += sprintf (page+len, "COMMAND SET:%s;\n", str);

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

static void destroy_proc_tree(struct parport *pp) {
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
}

static struct proc_dir_entry *new_proc_entry(const char *name, mode_t mode,
					     struct proc_dir_entry *parent,
					     unsigned short ino,
					     struct parport *p)
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
	{
		if (p && p->ops)
			ent->fill_inode = p->ops->fill_inode;
		ent->nlink = 2;
	} else
		ent->nlink = 1;

	proc_register(parent, ent);
	
	return ent;
}

/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 * We use it here to manage the module use counts.
 *
 * Note: only the top-level directory needs to do this; if
 * a lower level is referenced, the parent will be as well.
 */
static void parport_modcount(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		inc_parport_count();
	else
		dec_parport_count();
#endif
}

int parport_proc_init(void)
{
	base = new_proc_entry("parport", S_IFDIR, &proc_root,PROC_PARPORT,
			      NULL);
	if (base == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/parport.\n");
		return 0;
	}
	base->fill_inode = &parport_modcount;

	return 1;
}

void parport_proc_cleanup(void)
{
	if (base) {
		proc_unregister(&proc_root,base->low_ino);
		kfree(base);
		base = NULL;
	}
}

int parport_proc_register(struct parport *pp)
{
	memset(&pp->pdir, 0, sizeof(struct parport_dir));

	if (base == NULL) {
		printk(KERN_ERR "parport_proc not initialised yet.\n");
		return 1;
	}
	
	strncpy(pp->pdir.name, pp->name + strlen("parport"), 
		sizeof(pp->pdir.name));

	pp->pdir.entry = new_proc_entry(pp->pdir.name, S_IFDIR, base, 0, pp);
	if (pp->pdir.entry == NULL)
		goto out_fail;

	pp->pdir.irq = new_proc_entry("irq", S_IFREG | S_IRUGO | S_IWUSR, 
				      pp->pdir.entry, 0, pp);
	if (pp->pdir.irq == NULL)
		goto out_fail;

	pp->pdir.irq->read_proc = irq_read_proc;
	pp->pdir.irq->write_proc = irq_write_proc;
	pp->pdir.irq->data = pp;
	
	pp->pdir.devices = new_proc_entry("devices", 0, pp->pdir.entry, 0, pp);
	if (pp->pdir.devices == NULL)
		goto out_fail;

	pp->pdir.devices->read_proc = devices_read_proc;
	pp->pdir.devices->data = pp;
	
	pp->pdir.hardware = new_proc_entry("hardware", 0, pp->pdir.entry, 0,
					   pp);
	if (pp->pdir.hardware == NULL)
		goto out_fail;

	pp->pdir.hardware->read_proc = hardware_read_proc;
	pp->pdir.hardware->data = pp;

	pp->pdir.probe = new_proc_entry("autoprobe", 0, pp->pdir.entry, 0, pp);
	if (pp->pdir.probe == NULL)
		goto out_fail;

	pp->pdir.probe->read_proc = autoprobe_read_proc;
	pp->pdir.probe->data = pp;

	return 0;

out_fail:

	printk(KERN_ERR "%s: failure registering /proc/ entry.\n", pp->name);
	destroy_proc_tree(pp);
	return 1;
}

int parport_proc_unregister(struct parport *pp)
{
	destroy_proc_tree(pp);
	return 0;
}

#else

int parport_proc_register(struct parport *p) 
{
	return 0;
}

int parport_proc_unregister(struct parport *p)
{
	return 0;
}

int parport_proc_init(void)
{
	return 0;
}

void parport_proc_cleanup(void)
{
}

#endif
