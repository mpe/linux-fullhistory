#ifndef __LINUX_PARPORT_PC_H
#define __LINUX_PARPORT_PC_H

#include <asm/io.h>

/* --- register definitions ------------------------------- */

#define ECONTROL 0x402
#define CONFIGB  0x401
#define CONFIGA  0x400
#define EPPREG   0x4
#define CONTROL  0x2
#define STATUS   0x1
#define DATA     0

extern __inline__ void parport_pc_write_epp(struct parport *p, unsigned char d)
{
	outb(d, p->base+EPPREG);
}

extern __inline__ unsigned char parport_pc_read_epp(struct parport *p)
{
	return inb(p->base+EPPREG);
}

extern __inline__ unsigned char parport_pc_read_configb(struct parport *p)
{
	return inb(p->base+CONFIGB);
}

extern __inline__ void parport_pc_write_data(struct parport *p, unsigned char d)
{
	outb(d, p->base+DATA);
}

extern __inline__ unsigned char parport_pc_read_data(struct parport *p)
{
	return inb(p->base+DATA);
}

extern __inline__ void parport_pc_write_control(struct parport *p, unsigned char d)
{
	outb(d, p->base+CONTROL);
}

extern __inline__ unsigned char parport_pc_read_control(struct parport *p)
{
	return inb(p->base+CONTROL);
}

extern __inline__ unsigned char parport_pc_frob_control(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base+CONTROL);
	outb(((old & ~mask) ^ val), p->base+CONTROL);
	return old;
}

extern __inline__ void parport_pc_write_status(struct parport *p, unsigned char d)
{
	outb(d, p->base+STATUS);
}

extern __inline__ unsigned char parport_pc_read_status(struct parport *p)
{
	return inb(p->base+STATUS);
}

extern __inline__ void parport_pc_write_econtrol(struct parport *p, unsigned char d)
{
	outb(d, p->base+ECONTROL);
}

extern __inline__ unsigned char parport_pc_read_econtrol(struct parport *p)
{
	return inb(p->base+ECONTROL);
}

extern __inline__ unsigned char parport_pc_frob_econtrol(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base+ECONTROL);
	outb(((old & ~mask) ^ val), p->base+ECONTROL);
	return old;
}

extern void parport_pc_change_mode(struct parport *p, int m);

extern void parport_pc_write_fifo(struct parport *p, unsigned char v);

extern unsigned char parport_pc_read_fifo(struct parport *p);

extern void parport_pc_disable_irq(struct parport *p);

extern void parport_pc_enable_irq(struct parport *p);

extern void parport_pc_release_resources(struct parport *p);

extern int parport_pc_claim_resources(struct parport *p);

extern void parport_pc_save_state(struct parport *p, struct parport_state *s);

extern void parport_pc_restore_state(struct parport *p, struct parport_state *s);

extern size_t parport_pc_epp_read_block(struct parport *p, void *buf, size_t length);

extern size_t parport_pc_epp_write_block(struct parport *p, void *buf, size_t length);

extern int parport_pc_ecp_read_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle);

extern int parport_pc_ecp_write_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle);

extern int parport_pc_examine_irq(struct parport *p);

extern void parport_pc_inc_use_count(void);

extern void parport_pc_dec_use_count(void);

#endif
