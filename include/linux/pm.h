/*
 *  pm.h - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/list.h>
#include <asm/atomic.h>

/*
 * Power management requests... these are passed to pm_send_all() and friends.
 *
 * these functions are old and deprecated, see below.
 */
typedef int __bitwise pm_request_t;

#define PM_SUSPEND	((__force pm_request_t) 1)	/* enter D1-D3 */
#define PM_RESUME	((__force pm_request_t) 2)	/* enter D0 */


/*
 * Device types... these are passed to pm_register
 */
typedef int __bitwise pm_dev_t;

#define PM_UNKNOWN_DEV	((__force pm_dev_t) 0)	/* generic */
#define PM_SYS_DEV	((__force pm_dev_t) 1)	/* system device (fan, KB controller, ...) */
#define PM_PCI_DEV	((__force pm_dev_t) 2)	/* PCI device */
#define PM_USB_DEV	((__force pm_dev_t) 3)	/* USB device */
#define PM_SCSI_DEV	((__force pm_dev_t) 4)	/* SCSI device */
#define PM_ISA_DEV	((__force pm_dev_t) 5)	/* ISA device */
#define	PM_MTD_DEV	((__force pm_dev_t) 6)	/* Memory Technology Device */

/*
 * System device hardware ID (PnP) values
 */
enum
{
	PM_SYS_UNKNOWN = 0x00000000, /* generic */
	PM_SYS_KBC =	 0x41d00303, /* keyboard controller */
	PM_SYS_COM =	 0x41d00500, /* serial port */
	PM_SYS_IRDA =	 0x41d00510, /* IRDA controller */
	PM_SYS_FDC =	 0x41d00700, /* floppy controller */
	PM_SYS_VGA =	 0x41d00900, /* VGA controller */
	PM_SYS_PCMCIA =	 0x41d00e00, /* PCMCIA controller */
};

/*
 * Device identifier
 */
#define PM_PCI_ID(dev) ((dev)->bus->number << 16 | (dev)->devfn)

/*
 * Request handler callback
 */
struct pm_dev;

typedef int (*pm_callback)(struct pm_dev *dev, pm_request_t rqst, void *data);

/*
 * Dynamic device information
 */
struct pm_dev
{
	pm_dev_t	 type;
	unsigned long	 id;
	pm_callback	 callback;
	void		*data;

	unsigned long	 flags;
	unsigned long	 state;
	unsigned long	 prev_state;

	struct list_head entry;
};

#ifdef CONFIG_PM

extern int pm_active;

#define PM_IS_ACTIVE() (pm_active != 0)

/*
 * Register a device with power management
 */
struct pm_dev __deprecated *pm_register(pm_dev_t type, unsigned long id, pm_callback callback);

/*
 * Unregister a device with power management
 */
void __deprecated pm_unregister(struct pm_dev *dev);

/*
 * Unregister all devices with matching callback
 */
void __deprecated pm_unregister_all(pm_callback callback);

/*
 * Send a request to a single device
 */
int __deprecated pm_send(struct pm_dev *dev, pm_request_t rqst, void *data);

/*
 * Send a request to all devices
 */
int __deprecated pm_send_all(pm_request_t rqst, void *data);

#else /* CONFIG_PM */

#define PM_IS_ACTIVE() 0

static inline struct pm_dev *pm_register(pm_dev_t type,
					 unsigned long id,
					 pm_callback callback)
{
	return NULL;
}

static inline void pm_unregister(struct pm_dev *dev) {}

static inline void pm_unregister_all(pm_callback callback) {}

static inline int pm_send(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	return 0;
}

static inline int pm_send_all(pm_request_t rqst, void *data)
{
	return 0;
}

#endif /* CONFIG_PM */

/* Functions above this comment are list-based old-style power
 * managment. Please avoid using them.  */

/*
 * Callbacks for platform drivers to implement.
 */
extern void (*pm_idle)(void);
extern void (*pm_power_off)(void);

typedef int __bitwise suspend_state_t;

#define PM_SUSPEND_ON		((__force suspend_state_t) 0)
#define PM_SUSPEND_STANDBY	((__force suspend_state_t) 1)
#define PM_SUSPEND_MEM		((__force suspend_state_t) 3)
#define PM_SUSPEND_DISK		((__force suspend_state_t) 4)
#define PM_SUSPEND_MAX		((__force suspend_state_t) 5)

typedef int __bitwise suspend_disk_method_t;

#define	PM_DISK_FIRMWARE	((__force suspend_disk_method_t) 1)
#define	PM_DISK_PLATFORM	((__force suspend_disk_method_t) 2)
#define	PM_DISK_SHUTDOWN	((__force suspend_disk_method_t) 3)
#define	PM_DISK_REBOOT		((__force suspend_disk_method_t) 4)
#define	PM_DISK_MAX		((__force suspend_disk_method_t) 5)

struct pm_ops {
	suspend_disk_method_t pm_disk_mode;
	int (*prepare)(suspend_state_t state);
	int (*enter)(suspend_state_t state);
	int (*finish)(suspend_state_t state);
};

extern void pm_set_ops(struct pm_ops *);

extern int pm_suspend(suspend_state_t state);


/*
 * Device power management
 */

struct device;

typedef u32 __bitwise pm_message_t;

/*
 * There are 4 important states driver can be in:
 * ON     -- driver is working
 * FREEZE -- stop operations and apply whatever policy is applicable to a suspended driver
 *           of that class, freeze queues for block like IDE does, drop packets for
 *           ethernet, etc... stop DMA engine too etc... so a consistent image can be
 *           saved; but do not power any hardware down.
 * SUSPEND - like FREEZE, but hardware is doing as much powersaving as possible. Roughly
 *           pci D3.
 *
 * Unfortunately, current drivers only recognize numeric values 0 (ON) and 3 (SUSPEND).
 * We'll need to fix the drivers. So yes, putting 3 to all diferent defines is intentional,
 * and will go away as soon as drivers are fixed. Also note that typedef is neccessary,
 * we'll probably want to switch to
 *   typedef struct pm_message_t { int event; int flags; } pm_message_t
 * or something similar soon.
 */

#define PMSG_FREEZE	((__force pm_message_t) 3)
#define PMSG_SUSPEND	((__force pm_message_t) 3)
#define PMSG_ON		((__force pm_message_t) 0)

struct dev_pm_info {
	pm_message_t		power_state;
#ifdef	CONFIG_PM
	pm_message_t		prev_state;
	void			* saved_state;
	atomic_t		pm_users;
	struct device		* pm_parent;
	struct list_head	entry;
#endif
};

extern void device_pm_set_parent(struct device * dev, struct device * parent);

extern int device_suspend(pm_message_t state);
extern int device_power_down(pm_message_t state);
extern void device_power_up(void);
extern void device_resume(void);


#endif /* __KERNEL__ */

#endif /* _LINUX_PM_H */
