/* $Id: parport.h,v 1.2.6.3.2.2 1997/04/18 15:03:53 phil Exp $ */

#ifndef _PARPORT_H_
#define _PARPORT_H_

#include <asm/system.h>
#include <asm/ptrace.h>
#include <linux/proc_fs.h>

/* Maximum of 8 ports per machine */
#define PARPORT_MAX  8 

/* Magic numbers */
#define PARPORT_IRQ_NONE  -2
#define PARPORT_DMA_NONE  -2
#define PARPORT_IRQ_AUTO  -1
#define PARPORT_DMA_AUTO  -1
#define PARPORT_DISABLE   -2

/* Define this later. */
struct parport;

struct pc_parport_state {
	unsigned int ctr;
	unsigned int ecr;
};

struct parport_state {
	union {
		struct pc_parport_state pc;
		/* ARC has no state. */
		void *misc; 
	} u;
};

/* Generic operations vector through the dispatch table. */
#define parport_write_data(p,x)            (p)->ops->write_data(p,x)
#define parport_read_data(p)               (p)->ops->read_data(p)
#define parport_write_control(p,x)         (p)->ops->write_control(p,x)
#define parport_read_control(p)            (p)->ops->read_control(p)
#define parport_frob_control(p,m,v)        (p)->ops->frob_control(p,m,v)
#define parport_write_econtrol(p,x)        (p)->ops->write_econtrol(p,x)
#define parport_read_econtrol(p)           (p)->ops->read_econtrol(p)
#define parport_frob_econtrol(p,m,v)       (p)->ops->frob_econtrol(p,m,v)
#define parport_write_status(p,v)          (p)->ops->write_status(p,v)
#define parport_read_status(p)             (p)->ops->read_status(p)
#define parport_write_fifo(p,v)            (p)->ops->write_fifo(p,v)
#define parport_read_fifo(p)               (p)->ops->read_fifo(p)
#define parport_change_mode(p,m)           (p)->ops->change_mode(p,m)
#define parport_release_resources(p)       (p)->ops->release_resources(p)
#define parport_claim_resources(p)         (p)->ops->claim_resources(p)

struct parport_operations {
	void (*write_data)(struct parport *, unsigned int);
	unsigned int (*read_data)(struct parport *);
	void (*write_control)(struct parport *, unsigned int);
	unsigned int (*read_control)(struct parport *);
	unsigned int (*frob_control)(struct parport *, unsigned int mask, unsigned int val);
	void (*write_econtrol)(struct parport *, unsigned int);
	unsigned int (*read_econtrol)(struct parport *);
	unsigned int (*frob_econtrol)(struct parport *, unsigned int mask, unsigned int val);
	void (*write_status)(struct parport *, unsigned int);
	unsigned int (*read_status)(struct parport *);
	void (*write_fifo)(struct parport *, unsigned int);
	unsigned int (*read_fifo)(struct parport *);

	void (*change_mode)(struct parport *, int);

	void (*release_resources)(struct parport *);
	int (*claim_resources)(struct parport *);

	unsigned int (*epp_write_block)(struct parport *, void *, unsigned int);
	unsigned int (*epp_read_block)(struct parport *, void *, unsigned int);

	unsigned int (*ecp_write_block)(struct parport *, void *, unsigned int, void (*fn)(struct parport *, void *, unsigned int), void *);
	unsigned int (*ecp_read_block)(struct parport *, void *, unsigned int, void (*fn)(struct parport *, void *, unsigned int), void *);

	void (*save_state)(struct parport *, struct parport_state *);
	void (*restore_state)(struct parport *, struct parport_state *);

	void (*enable_irq)(struct parport *);
	void (*disable_irq)(struct parport *);
	int (*examine_irq)(struct parport *);

	void (*inc_use_count)(void);
	void (*dec_use_count)(void);
};

#define PARPORT_CONTROL_STROBE    0x1
#define PARPORT_CONTROL_AUTOFD    0x2
#define PARPORT_CONTROL_INIT      0x4
#define PARPORT_CONTROL_SELECT    0x8
#define PARPORT_CONTROL_INTEN     0x10
#define PARPORT_CONTROL_DIRECTION 0x20

#define PARPORT_STATUS_ERROR      0x8
#define PARPORT_STATUS_SELECT     0x10
#define PARPORT_STATUS_PAPEROUT   0x20
#define PARPORT_STATUS_ACK        0x40
#define PARPORT_STATUS_BUSY       0x80

/* Type classes for Plug-and-Play probe.  */
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

/* Each device can have two callback functions:
 *  1) a preemption function, called by the resource manager to request
 *     that the driver relinquish control of the port.  The driver should
 *     return zero if it agrees to release the port, and nonzero if it 
 *     refuses.  Do not call parport_release() - the kernel will do this
 *     implicitly.
 *
 *  2) a wake-up function, called by the resource manager to tell drivers
 *     that the port is available to be claimed.  If a driver wants to use
 *     the port, it should call parport_claim() here.
 */

/* A parallel port device */
struct pardevice {
	char *name;
	struct parport *port;
	int (*preempt)(void *);
	void (*wakeup)(void *);
	void *private;
	void (*irq_func)(int, void *, struct pt_regs *);
	int flags;
	struct pardevice *next;
	struct pardevice *prev;
	struct parport_state *state;     /* saved status over preemption */
};

struct parport_dir {
	struct proc_dir_entry *entry;    /* Directory /proc/parport/X     */
	struct proc_dir_entry *irq;      /* IRQ entry /proc/parport/X/irq */
	struct proc_dir_entry *devices;  /* /proc/parport/X/devices       */
	struct proc_dir_entry *hardware; /* /proc/parport/X/hardware      */
	char name[4];                    /* /proc/parport/"XXXX" */
};

/* A parallel port */
struct parport {
	unsigned int base;	/* base address */
	unsigned int size;	/* IO extent */
	char *name;
	int irq;		/* interrupt (or -1 for none) */
	int dma;
	unsigned int modes;

	struct pardevice *devices;
	struct pardevice *cad;	/* port owner */
	struct pardevice *lurker;
	
	struct parport *next;
	unsigned int flags; 

	struct parport_dir pdir;
	struct parport_device_info probe_info; 

	struct parport_operations *ops;
	void *private_data;     /* for lowlevel driver */
};

/* parport_register_port registers a new parallel port at the given address (if
 * one does not already exist) and returns a pointer to it.  This entails
 * claiming the I/O region, IRQ and DMA.
 * NULL is returned if initialisation fails. 
 */
struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops);

/* Unregister a port. */
void parport_unregister_port(struct parport *port);

/* parport_in_use returns nonzero if there are devices attached to a port. */
#define parport_in_use(x)  ((x)->devices != NULL)

/* Put a parallel port to sleep; release its hardware resources.  Only possible
 * if no devices are registered.  */
void parport_quiesce(struct parport *);

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
struct pardevice *parport_register_device(struct parport *port, 
			  const char *name,
			  int (*pf)(void *), void (*kf)(void *),
			  void (*irq_func)(int, void *, struct pt_regs *), 
			  int flags, void *handle);

/* parport_unregister unlinks a device from the chain. */
void parport_unregister_device(struct pardevice *dev);

/* parport_claim tries to gain ownership of the port for a particular driver.
 * This may fail (return non-zero) if another driver is busy.  If this
 * driver has registered an interrupt handler, it will be enabled. 
 */
int parport_claim(struct pardevice *dev);

/* parport_release reverses a previous parport_claim.  This can never fail, 
 * though the effects are undefined (except that they are bad) if you didn't
 * previously own the port.  Once you have released the port you should make
 * sure that neither your code nor the hardware on the port tries to initiate
 * any communication without first re-claiming the port.
 * If you mess with the port state (enabling ECP for example) you should
 * clean up before releasing the port. 
 */
void parport_release(struct pardevice *dev);

/* The "modes" entry in parport is a bit field representing the following
 * modes.
 * Note that LP_ECPEPP is for the SMC EPP+ECP mode which is NOT
 * 100% compatible with EPP.
 */
#define PARPORT_MODE_PCSPP	        0x0001
#define PARPORT_MODE_PCPS2		0x0002
#define PARPORT_MODE_PCEPP		0x0004
#define PARPORT_MODE_PCECP		0x0008
#define PARPORT_MODE_PCECPEPP		0x0010
#define PARPORT_MODE_PCECR		0x0020  /* ECR Register Exists */
#define PARPORT_MODE_PCECPPS2		0x0040

/* Flags used to identify what a device does. */
#define PARPORT_DEV_TRAN	        0x0000  /* We're transient. */
#define PARPORT_DEV_LURK	        0x0001  /* We lurk. */

#define PARPORT_FLAG_COMA		1

extern int parport_ieee1284_nibble_mode_ok(struct parport *, unsigned char);
extern int parport_wait_peripheral(struct parport *, unsigned char, unsigned
				   char);

/* Prototypes from parport_procfs */
extern int parport_proc_init(void);
extern int parport_proc_cleanup(void);
extern int parport_proc_register(struct parport *pp);
extern int parport_proc_unregister(struct parport *pp);

/* Prototypes from parport_ksyms.c */
extern void dec_parport_count(void);
extern void inc_parport_count(void);

extern int parport_probe(struct parport *port, char *buffer, int len);
extern void parport_probe_one(struct parport *port);

#endif /* _PARPORT_H_ */
