/* $Id: parport.h,v 1.1.2.5 1997/03/29 21:08:31 phil Exp $ */

#ifndef _PARPORT_H_
#define _PARPORT_H_

#include <asm/system.h>
#include <asm/ptrace.h>

/* Maximum of 8 ports per machine */
#define PARPORT_MAX  8 

/* Type classes for Plug-and-Play probe */

typedef enum {
	PARPORT_CLASS_LEGACY = 0,       /* Non-IEEE1284 device */
	PARPORT_CLASS_PRINTER,
	PARPORT_CLASS_MODEM,
	PARPORT_CLASS_NET,
	PARPORT_CLASS_HDC,              /* Hard disk controller */
	PARPORT_CLASS_PCMCIA,
	PARPORT_CLASS_MEDIA,            /* Multimedia device */
	PARPORT_CLASS_FDC,              /* Floppy disk controller */
	PARPORT_CLASS_PORTS,
	PARPORT_CLASS_SCANNER,
	PARPORT_CLASS_DIGCAM,
	PARPORT_CLASS_OTHER,            /* Anything else */
	PARPORT_CLASS_UNSPEC            /* No CLS field in ID */
} parport_device_class;

struct parport_device_info {
	parport_device_class class;
	char *mfr;
	char *model;
	char *cmdset;
	char *description;
};

/* Definitions for parallel port sharing */

/* Forward declare some stuff so we can use mutually circular structures */
struct ppd;
struct parport;

/* Each device can have two callback functions:
 *  1) a preemption function, called by the resource manager to request
 *     that the driver relinquish control of the port.  The driver should
 *     return zero if it agrees to release the port, and nonzero if it 
 *     refuses.  Do not call parport_release() - the kernel will do this
 *     implicitly.
 *
 *  2) a wake-up function, called by the resource manager to tell drivers
 *     that the port is available to be claimed.  If a driver wants to use
 *     the port, it should call parport_claim() here.  The return value from
 *     this function is ignored.
 */
typedef int (*callback_func) (void *);

/* This is an ordinary kernel IRQ handler routine.
 * The dev_id field (void *) will point the the port structure
 * associated with the interrupt request (to allow IRQ sharing)
 * Please make code IRQ sharing as this function may be called
 * when it isn't meant for you...
 */
typedef void (*irq_handler_func) (int, void *, struct pt_regs *);

/* A parallel port device */
struct ppd {
	char *name;
	struct parport *port;	/* The port this is associated with */
	callback_func preempt;	/* preemption function */
	callback_func wakeup;	/* kick function */
	void *private;
	irq_handler_func irq_func;
	int flags;
	unsigned char ctr;	/* SPP CTR register */
	unsigned char ecr;	/* ECP ECR register */
	struct ppd *next;
	struct ppd *prev;
};

/* A parallel port */
struct parport {
	unsigned int base;	/* base address */
	unsigned int size;	/* IO extent */
	char *name;
	int irq;		/* interrupt (or -1 for none) */
	int dma;
	unsigned int modes;
	struct ppd *devices;
	struct ppd *cad;	/* port owner */
	struct ppd *lurker;
	unsigned int ctr;	/* SPP CTR register */
	unsigned int ecr;	/* ECP ECR register */
	struct parport *next;
        unsigned int flags; 
	struct proc_dir_entry *proc_dir;
	struct parport_device_info probe_info; 
};

/* parport_register_port registers a new parallel port at the given address (if
 * one does not already exist) and returns a pointer to it.  This entails
 * claiming the I/O region, IRQ and DMA.
 * NULL is returned if initialisation fails. 
 */
struct parport *parport_register_port(unsigned long base, int irq, int dma);

/* parport_in_use returns nonzero if there are devices attached to a port. */
#define parport_in_use(x)  ((x)->devices != NULL)

/* parport_destroy blows away a parallel port.  This fails if any devices are
 * registered.
 */
void parport_destroy(struct parport *);

/* parport_enumerate returns a pointer to the linked list of all the ports
 * in this machine.
 */
struct parport *parport_enumerate(void);

/* parport_register_device declares that a device is connected to a port, and 
 * tells the kernel all it needs to know.  
 * pf is the preemption function (may be NULL for a transient driver)
 * kf is the wake-up function (may be NULL for a transient driver)
 * irq_func is the interrupt handler (may be NULL for no interrupts)
 * Only one lurking driver can be used on a given port. 
 * handle is a user pointer that gets handed to callback functions. 
 */
struct ppd *parport_register_device(struct parport *port, const char *name,
				    callback_func pf, callback_func kf,
				    irq_handler_func irq_func, int flags,
				    void *handle);

/* parport_deregister causes the kernel to forget about a device */
void parport_unregister_device(struct ppd *dev);

/* parport_claim tries to gain ownership of the port for a particular driver.
 * This may fail (return non-zero) if another driver is busy.  If this
 * driver has registered an interrupt handler, it will be enabled. 
 */
int parport_claim(struct ppd *dev);

/* parport_release reverses a previous parport_claim.  This can never fail, 
 * though the effects are undefined (except that they are bad) if you didn't
 * previously own the port.  Once you have released the port you should make
 * sure that neither your code nor the hardware on the port tries to initiate
 * any communication without first re-claiming the port.
 * If you mess with the port state (enabling ECP for example) you should
 * clean up before releasing the port. 
 */
void parport_release(struct ppd *dev);

/* The "modes" entry in parport is a bit field representing the following
 * modes.
 * Note that LP_ECPEPP is for the SMC EPP+ECP mode which is NOT
 * 100% compatible with EPP.
 */
#define PARPORT_MODE_SPP	        0x0001
#define PARPORT_MODE_PS2		0x0002
#define PARPORT_MODE_EPP		0x0004
#define PARPORT_MODE_ECP		0x0008
#define PARPORT_MODE_ECPEPP		0x0010
#define PARPORT_MODE_ECR		0x0020  /* ECR Register Exists */
#define PARPORT_MODE_ECPPS2		0x0040

/* Flags used to identify what a device does
 */
#define PARPORT_DEV_TRAN	        0x0000
#define PARPORT_DEV_LURK	        0x0001

#define PARPORT_FLAG_COMA		1

extern int parport_ieee1284_nibble_mode_ok(struct parport *, unsigned char);
extern int parport_wait_peripheral(struct parport *, unsigned char, unsigned
				   char);

/* Prototypes from parport_procfs */
extern int parport_proc_register(struct parport *pp);
extern void parport_proc_unregister(struct parport *pp);

/* Prototypes from parport_ksyms.c */
extern void dec_parport_count(void);
extern void inc_parport_count(void);

extern int parport_probe(struct parport *port, char *buffer, int len);
extern void parport_probe_one(struct parport *port);

/* Primitive port access functions */
extern inline void parport_w_ctrl(struct parport *port, int val) 
{
	outb(val, port->base+2);
}

extern inline int parport_r_ctrl(struct parport *port)
{
	return inb(port->base+2);
}

extern inline void parport_w_data(struct parport *port, int val)
{
	outb(val, port->base);
}

extern inline int parport_r_data(struct parport *port)
{
	return inb(port->base);
}

extern inline int parport_r_status(struct parport *port)
{
	return inb(port->base+1);
}

#endif /* _PARPORT_H_ */
