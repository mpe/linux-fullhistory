/* $Id: parport_procfs.c,v 1.1.2.2 1997/04/18 15:00:52 phil Exp $
 * Parallel port /proc interface code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tmw20@cam.ac.uk>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/tasks.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <linux/proc_fs.h>

#include <linux/parport.h>

#undef PARPORT_INCLUDE_BENCH

struct proc_dir_entry *base=NULL;

extern void parport_null_intr_func(int irq, void *dev_id, struct pt_regs *regs);

static int irq_write_proc(struct file *file, const char *buffer,
					  unsigned long count, void *data)
{
	int newirq;
	struct parport *pp = (struct parport *)data;
	
	if (count > 4 )  /* more than 4 digits for a irq 0x?? 0?? ??  */
		return(-EOVERFLOW);

	if (buffer[0] < 32 || !strncmp(buffer, "none", 4)) {
		newirq = PARPORT_IRQ_NONE;
	} else {
		if (buffer[0] == '0') {
			if( buffer[1] == 'x' )
				newirq = simple_strtoul(&buffer[2],0,16);
			else
				newirq = simple_strtoul(&buffer[1],0,8);
		} else {
			newirq = simple_strtoul(buffer,0,10);
		}
	}

	if (pp->irq != PARPORT_IRQ_NONE && !(pp->flags & PARPORT_FLAG_COMA)) 
		free_irq(pp->irq, pp);

	pp->irq = newirq;

	if (pp->irq != PARPORT_IRQ_NONE && !(pp->flags & PARPORT_FLAG_COMA)) { 
		struct pardevice *pd = pp->cad;

		if (pd == NULL) {
			pd = pp->devices;
			if (pd != NULL) 
				request_irq(pp->irq, pd->irq_func ? 
					    pd->irq_func :
					    parport_null_intr_func,
					    SA_INTERRUPT, pd->name, pd->port);
		} else {
			request_irq(pp->irq, pd->irq_func ? pd->irq_func :
				    parport_null_intr_func,
				    SA_INTERRUPT, pp->name, pd->port);
		}
	}

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
			len += sprintf(page+len, "+");
		else
			len += sprintf(page+len, " ");

		len += sprintf(page+len, "%s",pd1->name);

		if (pd1 == pp->lurker)
			len += sprintf(page+len, " LURK");
		
		len += sprintf(page+len,"\n");
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
	
	len += sprintf(page+len, "base:\t0x%x\n",pp->base);
	if (pp->irq == PARPORT_IRQ_NONE)
		len += sprintf(page+len, "irq:\tnone\n");
	else
		len += sprintf(page+len, "irq:\t%d\n",pp->irq);
	len += sprintf(page+len, "dma:\t%d\n",pp->dma);


#if 0
	len += sprintf(page+len, "modes:\t");
	{
#define printmode(x) {if(pp->modes&PARPORT_MODE_##x){len+=sprintf(page+len,"%s%s",f?",":"",#x);f++;}}
		int f = 0;
		printmode(SPP);
		printmode(PS2);
		printmode(EPP);
		printmode(ECP);
		printmode(ECPEPP);
		printmode(ECPPS2);
#undef printmode
	}
	len += sprintf(page+len, "\n");

	len += sprintf(page+len, "mode:\t");
	if (pp->modes & PARPORT_MODE_ECR) {
		switch (r_ecr(pp) >> 5) {
		case 0:
			len += sprintf(page+len, "SPP");
			if( pp->modes & PARPORT_MODE_PS2 )
				len += sprintf(page+len, ",PS2");
			if( pp->modes & PARPORT_MODE_EPP )
				len += sprintf(page+len, ",EPP");
			break;
		case 1:
			len += sprintf(page+len, "ECPPS2");
			break;
		case 2:
			len += sprintf(page+len, "DATAFIFO");
			break;
		case 3:
			len += sprintf(page+len, "ECP");
			break;
		case 4:
			len += sprintf(page+len, "ECPEPP");
			break;
		case 5:
			len += sprintf(page+len, "Reserved?");
			break;
		case 6:
			len += sprintf(page+len, "TEST");
			break;
		case 7:
			len += sprintf(page+len, "Configuration");
			break;
		}
	} else {
		len += sprintf(page+len, "SPP");
		if (pp->modes & PARPORT_MODE_PS2)
			len += sprintf(page+len, ",PS2");
		if (pp->modes & PARPORT_MODE_EPP)
			len += sprintf(page+len, ",EPP");
	}
	len += sprintf(page+len, "\n");
#endif	
#if 0
	/* Now no detection, please fix with an external function */
	len += sprintf(page+len, "chipset:\tunknown\n");
#endif
#ifdef PARPORT_INCLUDE_BENCHMARK
	if (pp->speed)
		len += sprintf(page+len, "bench:\t%d Bytes/s\n",pp->speed);
	else
		len += sprintf(page+len, "bench:\tunknown\n");
#endif	

	*start = 0;
	*eof   = 1;
	return len;
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


int parport_proc_init()
{
	base = new_proc_entry("parport", S_IFDIR, &proc_root,PROC_PARPORT);

	if (base)
		return 1;
	else {
		printk(KERN_ERR "parport: Error creating proc entry /proc/parport\n");
		return 0;
	}
}

int parport_proc_cleanup()
{
	if (base)
		proc_unregister(&proc_root,base->low_ino);

	base = NULL;
	
	return 0;
}

int parport_proc_register(struct parport *pp)
{
	struct proc_dir_entry *ent;
	static int conta=0;
	char *name;

	memset(&pp->pdir,0,sizeof(struct parport_dir));

	if (!base) {
		printk(KERN_ERR "parport: Error entry /proc/parport, not generated?\n");
		return 1;
	}
	
	name = pp->pdir.name;
	sprintf(name,"%d",conta++);

	ent = new_proc_entry(name, S_IFDIR, base,0);
	if (!ent) {
		printk(KERN_ERR "parport: Error registering proc_entry /proc/%s\n",name);
		return 1;
	}
	pp->pdir.entry = ent;

	ent = new_proc_entry("irq", S_IFREG | S_IRUGO | S_IWUSR, pp->pdir.entry,0);
	if (!ent) {
		printk(KERN_ERR "parport: Error registering proc_entry /proc/%s/irq\n",name);
		return 1;
	}
	ent->read_proc = irq_read_proc;
	ent->write_proc= irq_write_proc;
	ent->data      = pp;
	pp->pdir.irq   = ent;
	
	ent = new_proc_entry("devices", 0, pp->pdir.entry,0);
	if (!ent) {
		printk(KERN_ERR "parport: Error registering proc_entry /proc/%s/devices\n",name);
		return 1;
	}
	ent->read_proc   = devices_read_proc;
	ent->data        = pp;
	pp->pdir.devices = ent;
	
	ent = new_proc_entry("hardware", 0, pp->pdir.entry,0);
	if (!ent) {
		printk(KERN_ERR "parport: Error registering proc_entry /proc/%s/hardware\n",name);
		return 1;
	}
	ent->read_proc    = hardware_read_proc;
	ent->data         = pp;
	pp->pdir.hardware = ent;
	return 0;
}

int parport_proc_unregister(struct parport *pp)
{
	if (pp->pdir.entry) {
		if (pp->pdir.irq) {
			proc_unregister(pp->pdir.entry, pp->pdir.irq->low_ino);
			kfree(pp->pdir.irq);
		}
		
		if (pp->pdir.devices) {
			proc_unregister(pp->pdir.entry,
					pp->pdir.devices->low_ino);
			kfree(pp->pdir.devices);
		}
		
		if (pp->pdir.hardware) {
			proc_unregister(pp->pdir.entry,
					pp->pdir.hardware->low_ino);
			kfree(pp->pdir.hardware);
		}
		
		proc_unregister(base, pp->pdir.entry->low_ino);
		kfree(pp->pdir.entry);
	}
	
	return 0;
}
