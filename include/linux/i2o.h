/*
 * I2O kernel space accessible structures/APIs
 *
 * (c) Copyright 1999, 2000 Red Hat Software
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *************************************************************************
 *
 * This header file defined the I2O APIs/structures for use by
 * the I2O kernel modules.
 *
 */

#ifndef _I2O_H
#define _I2O_H

#ifdef __KERNEL__		/* This file to be included by kernel only */

#include <linux/i2o-dev.h>

/* How many different OSM's are we allowing */
#define I2O_MAX_DRIVERS		4

#include <asm/io.h>
#include <asm/semaphore.h>	/* Needed for MUTEX init macros */
#include <linux/pci.h>
#include <linux/dma-mapping.h>

/* message queue empty */
#define I2O_QUEUE_EMPTY		0xffffffff

/*
 *	Message structures
 */
struct i2o_message {
	union {
		struct {
			u8 version_offset;
			u8 flags;
			u16 size;
			u32 target_tid:12;
			u32 init_tid:12;
			u32 function:8;
			u32 icntxt;	/* initiator context */
			u32 tcntxt;	/* transaction context */
		} s;
		u32 head[4];
	} u;
	/* List follows */
	u32 body[0];
};

/*
 *	Each I2O device entity has one of these. There is one per device.
 */
struct i2o_device {
	i2o_lct_entry lct_data;	/* Device LCT information */

	struct i2o_controller *iop;	/* Controlling IOP */
	struct list_head list;	/* node in IOP devices list */

	struct device device;

	struct semaphore lock;	/* device lock */

	struct class_device classdev;	/* i2o device class */
};

/*
 *	Event structure provided to the event handling function
 */
struct i2o_event {
	struct work_struct work;
	struct i2o_device *i2o_dev;	/* I2O device pointer from which the
					   event reply was initiated */
	u16 size;		/* Size of data in 32-bit words */
	u32 tcntxt;		/* Transaction context used at
				   registration */
	u32 event_indicator;	/* Event indicator from reply */
	u32 data[0];		/* Event data from reply */
};

/*
 *	I2O classes which could be handled by the OSM
 */
struct i2o_class_id {
	u16 class_id:12;
};

/*
 *	I2O driver structure for OSMs
 */
struct i2o_driver {
	char *name;		/* OSM name */
	int context;		/* Low 8 bits of the transaction info */
	struct i2o_class_id *classes;	/* I2O classes that this OSM handles */

	/* Message reply handler */
	int (*reply) (struct i2o_controller *, u32, struct i2o_message *);

	/* Event handler */
	void (*event) (struct i2o_event *);

	struct workqueue_struct *event_queue;	/* Event queue */

	struct device_driver driver;

	/* notification of changes */
	void (*notify_controller_add) (struct i2o_controller *);
	void (*notify_controller_remove) (struct i2o_controller *);
	void (*notify_device_add) (struct i2o_device *);
	void (*notify_device_remove) (struct i2o_device *);

	struct semaphore lock;
};

/*
 *	Contains all information which are necessary for DMA operations
 */
struct i2o_dma {
	void *virt;
	dma_addr_t phys;
	u32 len;
};

/*
 *	Context queue entry, used for 32-bit context on 64-bit systems
 */
struct i2o_context_list_element {
	struct list_head list;
	u32 context;
	void *ptr;
	unsigned long timestamp;
};

/*
 * Each I2O controller has one of these objects
 */
struct i2o_controller {
	char name[16];
	int unit;
	int type;

	struct pci_dev *pdev;	/* PCI device */

	unsigned int short_req:1;	/* use small block sizes */
	unsigned int no_quiesce:1;	/* dont quiesce before reset */
	unsigned int raptor:1;		/* split bar */
	unsigned int promise:1;		/* Promise controller */

#ifdef CONFIG_MTRR
	int mtrr_reg0;
	int mtrr_reg1;
#endif

	struct list_head devices;	/* list of I2O devices */

	struct notifier_block *event_notifer;	/* Events */
	atomic_t users;
	struct list_head list;	/* Controller list */
	void __iomem *post_port;	/* Inbout port address */
	void __iomem *reply_port;	/* Outbound port address */
	void __iomem *irq_mask;		/* Interrupt register address */

	/* Dynamic LCT related data */

	struct i2o_dma status;	/* status of IOP */

	struct i2o_dma hrt;	/* HW Resource Table */
	i2o_lct *lct;		/* Logical Config Table */
	struct i2o_dma dlct;	/* Temp LCT */
	struct semaphore lct_lock;	/* Lock for LCT updates */
	struct i2o_dma status_block;	/* IOP status block */

	struct i2o_dma base;	/* controller messaging unit */
	struct i2o_dma in_queue;	/* inbound message queue Host->IOP */
	struct i2o_dma out_queue;	/* outbound message queue IOP->Host */

	unsigned int battery:1;		/* Has a battery backup */
	unsigned int io_alloc:1;	/* An I/O resource was allocated */
	unsigned int mem_alloc:1;	/* A memory resource was allocated */

	struct resource io_resource;	/* I/O resource allocated to the IOP */
	struct resource mem_resource;	/* Mem resource allocated to the IOP */

	struct proc_dir_entry *proc_entry;	/* /proc dir */

	struct list_head bus_list;	/* list of busses on IOP */
	struct device device;
	struct i2o_device *exec;	/* Executive */
#if BITS_PER_LONG == 64
	spinlock_t context_list_lock;	/* lock for context_list */
	atomic_t context_list_counter;	/* needed for unique contexts */
	struct list_head context_list;	/* list of context id's
					   and pointers */
#endif
	spinlock_t lock;	/* lock for controller
				   configuration */

	void *driver_data[I2O_MAX_DRIVERS];	/* storage for drivers */
};

/*
 * I2O System table entry
 *
 * The system table contains information about all the IOPs in the
 * system.  It is sent to all IOPs so that they can create peer2peer
 * connections between them.
 */
struct i2o_sys_tbl_entry {
	u16 org_id;
	u16 reserved1;
	u32 iop_id:12;
	u32 reserved2:20;
	u16 seg_num:12;
	u16 i2o_version:4;
	u8 iop_state;
	u8 msg_type;
	u16 frame_size;
	u16 reserved3;
	u32 last_changed;
	u32 iop_capabilities;
	u32 inbound_low;
	u32 inbound_high;
};

struct i2o_sys_tbl {
	u8 num_entries;
	u8 version;
	u16 reserved1;
	u32 change_ind;
	u32 reserved2;
	u32 reserved3;
	struct i2o_sys_tbl_entry iops[0];
};

extern struct list_head i2o_controllers;

/* Message functions */
static inline u32 i2o_msg_get(struct i2o_controller *, struct i2o_message __iomem **);
extern u32 i2o_msg_get_wait(struct i2o_controller *, struct i2o_message __iomem **,
			    int);
static inline void i2o_msg_post(struct i2o_controller *, u32);
static inline int i2o_msg_post_wait(struct i2o_controller *, u32,
				    unsigned long);
extern int i2o_msg_post_wait_mem(struct i2o_controller *, u32, unsigned long,
				 struct i2o_dma *);
extern void i2o_msg_nop(struct i2o_controller *, u32);
static inline void i2o_flush_reply(struct i2o_controller *, u32);

/* DMA handling functions */
static inline int i2o_dma_alloc(struct device *, struct i2o_dma *, size_t,
				unsigned int);
static inline void i2o_dma_free(struct device *, struct i2o_dma *);
int i2o_dma_realloc(struct device *, struct i2o_dma *, size_t, unsigned int);

static inline int i2o_dma_map(struct device *, struct i2o_dma *);
static inline void i2o_dma_unmap(struct device *, struct i2o_dma *);

/* IOP functions */
extern int i2o_status_get(struct i2o_controller *);

extern int i2o_event_register(struct i2o_device *, struct i2o_driver *, int,
			      u32);
extern struct i2o_device *i2o_iop_find_device(struct i2o_controller *, u16);
extern struct i2o_controller *i2o_find_iop(int);

/* Functions needed for handling 64-bit pointers in 32-bit context */
#if BITS_PER_LONG == 64
extern u32 i2o_cntxt_list_add(struct i2o_controller *, void *);
extern void *i2o_cntxt_list_get(struct i2o_controller *, u32);
extern u32 i2o_cntxt_list_remove(struct i2o_controller *, void *);
extern u32 i2o_cntxt_list_get_ptr(struct i2o_controller *, void *);

static inline u32 i2o_ptr_low(void *ptr)
{
	return (u32) (u64) ptr;
};

static inline u32 i2o_ptr_high(void *ptr)
{
	return (u32) ((u64) ptr >> 32);
};
#else
static inline u32 i2o_cntxt_list_add(struct i2o_controller *c, void *ptr)
{
	return (u32) ptr;
};

static inline void *i2o_cntxt_list_get(struct i2o_controller *c, u32 context)
{
	return (void *)context;
};

static inline u32 i2o_cntxt_list_remove(struct i2o_controller *c, void *ptr)
{
	return (u32) ptr;
};

static inline u32 i2o_cntxt_list_get_ptr(struct i2o_controller *c, void *ptr)
{
	return (u32) ptr;
};

static inline u32 i2o_ptr_low(void *ptr)
{
	return (u32) ptr;
};

static inline u32 i2o_ptr_high(void *ptr)
{
	return 0;
};
#endif

/* I2O driver (OSM) functions */
extern int i2o_driver_register(struct i2o_driver *);
extern void i2o_driver_unregister(struct i2o_driver *);

/**
 *	i2o_driver_notify_controller_add - Send notification of added controller
 *					   to a single I2O driver
 *
 *	Send notification of added controller to a single registered driver.
 */
static inline void i2o_driver_notify_controller_add(struct i2o_driver *drv,
						    struct i2o_controller *c)
{
	if (drv->notify_controller_add)
		drv->notify_controller_add(c);
};

/**
 *	i2o_driver_notify_controller_remove - Send notification of removed
 *					      controller to a single I2O driver
 *
 *	Send notification of removed controller to a single registered driver.
 */
static inline void i2o_driver_notify_controller_remove(struct i2o_driver *drv,
						       struct i2o_controller *c)
{
	if (drv->notify_controller_remove)
		drv->notify_controller_remove(c);
};

/**
 *	i2o_driver_notify_device_add - Send notification of added device to a
 *				       single I2O driver
 *
 *	Send notification of added device to a single registered driver.
 */
static inline void i2o_driver_notify_device_add(struct i2o_driver *drv,
						struct i2o_device *i2o_dev)
{
	if (drv->notify_device_add)
		drv->notify_device_add(i2o_dev);
};

/**
 *	i2o_driver_notify_device_remove - Send notification of removed device
 *					  to a single I2O driver
 *
 *	Send notification of removed device to a single registered driver.
 */
static inline void i2o_driver_notify_device_remove(struct i2o_driver *drv,
						   struct i2o_device *i2o_dev)
{
	if (drv->notify_device_remove)
		drv->notify_device_remove(i2o_dev);
};

extern void i2o_driver_notify_controller_add_all(struct i2o_controller *);
extern void i2o_driver_notify_controller_remove_all(struct i2o_controller *);
extern void i2o_driver_notify_device_add_all(struct i2o_device *);
extern void i2o_driver_notify_device_remove_all(struct i2o_device *);

/* I2O device functions */
extern int i2o_device_claim(struct i2o_device *);
extern int i2o_device_claim_release(struct i2o_device *);

/* Exec OSM functions */
extern int i2o_exec_lct_get(struct i2o_controller *);

/* device to i2o_device and driver to i2o_driver convertion functions */
#define to_i2o_driver(drv) container_of(drv,struct i2o_driver, driver)
#define to_i2o_device(dev) container_of(dev, struct i2o_device, device)

/*
 *	Messenger inlines
 */
static inline u32 I2O_POST_READ32(struct i2o_controller *c)
{
	rmb();
	return readl(c->post_port);
};

static inline void I2O_POST_WRITE32(struct i2o_controller *c, u32 val)
{
	wmb();
	writel(val, c->post_port);
};

static inline u32 I2O_REPLY_READ32(struct i2o_controller *c)
{
	rmb();
	return readl(c->reply_port);
};

static inline void I2O_REPLY_WRITE32(struct i2o_controller *c, u32 val)
{
	wmb();
	writel(val, c->reply_port);
};

static inline u32 I2O_IRQ_READ32(struct i2o_controller *c)
{
	rmb();
	return readl(c->irq_mask);
};

static inline void I2O_IRQ_WRITE32(struct i2o_controller *c, u32 val)
{
	wmb();
	writel(val, c->irq_mask);
	wmb();
};

/**
 *	i2o_msg_get - obtain an I2O message from the IOP
 *	@c: I2O controller
 *	@msg: pointer to a I2O message pointer
 *
 *	This function tries to get a message slot. If no message slot is
 *	available do not wait until one is availabe (see also i2o_msg_get_wait).
 *
 *	On a success the message is returned and the pointer to the message is
 *	set in msg. The returned message is the physical page frame offset
 *	address from the read port (see the i2o spec). If no message is
 *	available returns I2O_QUEUE_EMPTY and msg is leaved untouched.
 */
static inline u32 i2o_msg_get(struct i2o_controller *c,
			      struct i2o_message __iomem **msg)
{
	u32 m;

	if ((m = I2O_POST_READ32(c)) != I2O_QUEUE_EMPTY)
		*msg = c->in_queue.virt + m;

	return m;
};

/**
 *	i2o_msg_post - Post I2O message to I2O controller
 *	@c: I2O controller to which the message should be send
 *	@m: the message identifier
 *
 *	Post the message to the I2O controller.
 */
static inline void i2o_msg_post(struct i2o_controller *c, u32 m)
{
	I2O_POST_WRITE32(c, m);
};

/**
 * 	i2o_msg_post_wait - Post and wait a message and wait until return
 *	@c: controller
 *	@m: message to post
 *	@timeout: time in seconds to wait
 *
 * 	This API allows an OSM to post a message and then be told whether or
 *	not the system received a successful reply. If the message times out
 *	then the value '-ETIMEDOUT' is returned.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static inline int i2o_msg_post_wait(struct i2o_controller *c, u32 m,
				    unsigned long timeout)
{
	return i2o_msg_post_wait_mem(c, m, timeout, NULL);
};

/**
 *	i2o_flush_reply - Flush reply from I2O controller
 *	@c: I2O controller
 *	@m: the message identifier
 *
 *	The I2O controller must be informed that the reply message is not needed
 *	anymore. If you forget to flush the reply, the message frame can't be
 *	used by the controller anymore and is therefore lost.
 *
 *	FIXME: is there a timeout after which the controller reuse the message?
 */
static inline void i2o_flush_reply(struct i2o_controller *c, u32 m)
{
	I2O_REPLY_WRITE32(c, m);
};

/**
 *	i2o_out_to_virt - Turn an I2O message to a virtual address
 *	@c: controller
 *	@m: message engine value
 *
 *	Turn a receive message from an I2O controller bus address into
 *	a Linux virtual address. The shared page frame is a linear block
 *	so we simply have to shift the offset. This function does not
 *	work for sender side messages as they are ioremap objects
 *	provided by the I2O controller.
 */
static inline struct i2o_message *i2o_msg_out_to_virt(struct i2o_controller *c,
						      u32 m)
{
	BUG_ON(m < c->out_queue.phys
	       || m >= c->out_queue.phys + c->out_queue.len);

	return c->out_queue.virt + (m - c->out_queue.phys);
};

/**
 *	i2o_msg_in_to_virt - Turn an I2O message to a virtual address
 *	@c: controller
 *	@m: message engine value
 *
 *	Turn a send message from an I2O controller bus address into
 *	a Linux virtual address. The shared page frame is a linear block
 *	so we simply have to shift the offset. This function does not
 *	work for receive side messages as they are kmalloc objects
 *	in a different pool.
 */
static inline struct i2o_message __iomem *i2o_msg_in_to_virt(struct i2o_controller *c,
						     u32 m)
{
	return c->in_queue.virt + m;
};

/**
 *	i2o_dma_alloc - Allocate DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which should get the DMA buffer
 *	@len: length of the new DMA memory
 *	@gfp_mask: GFP mask
 *
 *	Allocate a coherent DMA memory and write the pointers into addr.
 *
 *	Returns 0 on success or -ENOMEM on failure.
 */
static inline int i2o_dma_alloc(struct device *dev, struct i2o_dma *addr,
				size_t len, unsigned int gfp_mask)
{
	addr->virt = dma_alloc_coherent(dev, len, &addr->phys, gfp_mask);
	if (!addr->virt)
		return -ENOMEM;

	memset(addr->virt, 0, len);
	addr->len = len;

	return 0;
};

/**
 *	i2o_dma_free - Free DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which contains the DMA buffer
 *
 *	Free a coherent DMA memory and set virtual address of addr to NULL.
 */
static inline void i2o_dma_free(struct device *dev, struct i2o_dma *addr)
{
	if (addr->virt) {
		if (addr->phys)
			dma_free_coherent(dev, addr->len, addr->virt,
					  addr->phys);
		else
			kfree(addr->virt);
		addr->virt = NULL;
	}
};

/**
 *	i2o_dma_map - Map the memory to DMA
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which should be mapped
 *
 *	Map the memory in addr->virt to coherent DMA memory and write the
 *	physical address into addr->phys.
 *
 *	Returns 0 on success or -ENOMEM on failure.
 */
static inline int i2o_dma_map(struct device *dev, struct i2o_dma *addr)
{
	if (!addr->virt)
		return -EFAULT;

	if (!addr->phys)
		addr->phys = dma_map_single(dev, addr->virt, addr->len,
					    DMA_BIDIRECTIONAL);
	if (!addr->phys)
		return -ENOMEM;

	return 0;
};

/**
 *	i2o_dma_unmap - Unmap the DMA memory
 *	@dev: struct device pointer to the PCI device of the I2O controller
 *	@addr: i2o_dma struct which should be unmapped
 *
 *	Unmap the memory in addr->virt from DMA memory.
 */
static inline void i2o_dma_unmap(struct device *dev, struct i2o_dma *addr)
{
	if (!addr->virt)
		return;

	if (addr->phys) {
		dma_unmap_single(dev, addr->phys, addr->len, DMA_BIDIRECTIONAL);
		addr->phys = 0;
	}
};

/*
 *	Endian handling wrapped into the macro - keeps the core code
 *	cleaner.
 */

#define i2o_raw_writel(val, mem)	__raw_writel(cpu_to_le32(val), mem)

extern int i2o_parm_field_get(struct i2o_device *, int, int, void *, int);
extern int i2o_parm_table_get(struct i2o_device *, int, int, int, void *, int,
			      void *, int);
/* FIXME: remove
extern int i2o_query_table(int, struct i2o_controller *, int, int, int,
			   void *, int, void *, int);
extern int i2o_clear_table(struct i2o_controller *, int, int);
extern int i2o_row_add_table(struct i2o_controller *, int, int, int,
			     void *, int);
extern int i2o_issue_params(int, struct i2o_controller *, int, void *, int,
			    void *, int);
*/

/* debugging functions */
extern void i2o_report_status(const char *, const char *, struct i2o_message *);
extern void i2o_dump_message(struct i2o_message *);
extern void i2o_dump_hrt(struct i2o_controller *c);
extern void i2o_debug_state(struct i2o_controller *c);

/*
 *	Cache strategies
 */

/*	The NULL strategy leaves everything up to the controller. This tends to be a
 *	pessimal but functional choice.
 */
#define CACHE_NULL		0
/*	Prefetch data when reading. We continually attempt to load the next 32 sectors
 *	into the controller cache.
 */
#define CACHE_PREFETCH		1
/*	Prefetch data when reading. We sometimes attempt to load the next 32 sectors
 *	into the controller cache. When an I/O is less <= 8K we assume its probably
 *	not sequential and don't prefetch (default)
 */
#define CACHE_SMARTFETCH	2
/*	Data is written to the cache and then out on to the disk. The I/O must be
 *	physically on the medium before the write is acknowledged (default without
 *	NVRAM)
 */
#define CACHE_WRITETHROUGH	17
/*	Data is written to the cache and then out on to the disk. The controller
 *	is permitted to write back the cache any way it wants. (default if battery
 *	backed NVRAM is present). It can be useful to set this for swap regardless of
 *	battery state.
 */
#define CACHE_WRITEBACK		18
/*	Optimise for under powered controllers, especially on RAID1 and RAID0. We
 *	write large I/O's directly to disk bypassing the cache to avoid the extra
 *	memory copy hits. Small writes are writeback cached
 */
#define CACHE_SMARTBACK		19
/*	Optimise for under powered controllers, especially on RAID1 and RAID0. We
 *	write large I/O's directly to disk bypassing the cache to avoid the extra
 *	memory copy hits. Small writes are writethrough cached. Suitable for devices
 *	lacking battery backup
 */
#define CACHE_SMARTTHROUGH	20

/*
 *	Ioctl structures
 */

#define 	BLKI2OGRSTRAT	_IOR('2', 1, int)
#define 	BLKI2OGWSTRAT	_IOR('2', 2, int)
#define 	BLKI2OSRSTRAT	_IOW('2', 3, int)
#define 	BLKI2OSWSTRAT	_IOW('2', 4, int)

/*
 *	I2O Function codes
 */

/*
 *	Executive Class
 */
#define	I2O_CMD_ADAPTER_ASSIGN		0xB3
#define	I2O_CMD_ADAPTER_READ		0xB2
#define	I2O_CMD_ADAPTER_RELEASE		0xB5
#define	I2O_CMD_BIOS_INFO_SET		0xA5
#define	I2O_CMD_BOOT_DEVICE_SET		0xA7
#define	I2O_CMD_CONFIG_VALIDATE		0xBB
#define	I2O_CMD_CONN_SETUP		0xCA
#define	I2O_CMD_DDM_DESTROY		0xB1
#define	I2O_CMD_DDM_ENABLE		0xD5
#define	I2O_CMD_DDM_QUIESCE		0xC7
#define	I2O_CMD_DDM_RESET		0xD9
#define	I2O_CMD_DDM_SUSPEND		0xAF
#define	I2O_CMD_DEVICE_ASSIGN		0xB7
#define	I2O_CMD_DEVICE_RELEASE		0xB9
#define	I2O_CMD_HRT_GET			0xA8
#define	I2O_CMD_ADAPTER_CLEAR		0xBE
#define	I2O_CMD_ADAPTER_CONNECT		0xC9
#define	I2O_CMD_ADAPTER_RESET		0xBD
#define	I2O_CMD_LCT_NOTIFY		0xA2
#define	I2O_CMD_OUTBOUND_INIT		0xA1
#define	I2O_CMD_PATH_ENABLE		0xD3
#define	I2O_CMD_PATH_QUIESCE		0xC5
#define	I2O_CMD_PATH_RESET		0xD7
#define	I2O_CMD_STATIC_MF_CREATE	0xDD
#define	I2O_CMD_STATIC_MF_RELEASE	0xDF
#define	I2O_CMD_STATUS_GET		0xA0
#define	I2O_CMD_SW_DOWNLOAD		0xA9
#define	I2O_CMD_SW_UPLOAD		0xAB
#define	I2O_CMD_SW_REMOVE		0xAD
#define	I2O_CMD_SYS_ENABLE		0xD1
#define	I2O_CMD_SYS_MODIFY		0xC1
#define	I2O_CMD_SYS_QUIESCE		0xC3
#define	I2O_CMD_SYS_TAB_SET		0xA3

/*
 * Utility Class
 */
#define I2O_CMD_UTIL_NOP		0x00
#define I2O_CMD_UTIL_ABORT		0x01
#define I2O_CMD_UTIL_CLAIM		0x09
#define I2O_CMD_UTIL_RELEASE		0x0B
#define I2O_CMD_UTIL_PARAMS_GET		0x06
#define I2O_CMD_UTIL_PARAMS_SET		0x05
#define I2O_CMD_UTIL_EVT_REGISTER	0x13
#define I2O_CMD_UTIL_EVT_ACK		0x14
#define I2O_CMD_UTIL_CONFIG_DIALOG	0x10
#define I2O_CMD_UTIL_DEVICE_RESERVE	0x0D
#define I2O_CMD_UTIL_DEVICE_RELEASE	0x0F
#define I2O_CMD_UTIL_LOCK		0x17
#define I2O_CMD_UTIL_LOCK_RELEASE	0x19
#define I2O_CMD_UTIL_REPLY_FAULT_NOTIFY	0x15

/*
 * SCSI Host Bus Adapter Class
 */
#define I2O_CMD_SCSI_EXEC		0x81
#define I2O_CMD_SCSI_ABORT		0x83
#define I2O_CMD_SCSI_BUSRESET		0x27

/*
 * Random Block Storage Class
 */
#define I2O_CMD_BLOCK_READ		0x30
#define I2O_CMD_BLOCK_WRITE		0x31
#define I2O_CMD_BLOCK_CFLUSH		0x37
#define I2O_CMD_BLOCK_MLOCK		0x49
#define I2O_CMD_BLOCK_MUNLOCK		0x4B
#define I2O_CMD_BLOCK_MMOUNT		0x41
#define I2O_CMD_BLOCK_MEJECT		0x43
#define I2O_CMD_BLOCK_POWER		0x70

#define I2O_PRIVATE_MSG			0xFF

/* Command status values  */

#define I2O_CMD_IN_PROGRESS	0x01
#define I2O_CMD_REJECTED	0x02
#define I2O_CMD_FAILED		0x03
#define I2O_CMD_COMPLETED	0x04

/* I2O API function return values */

#define I2O_RTN_NO_ERROR			0
#define I2O_RTN_NOT_INIT			1
#define I2O_RTN_FREE_Q_EMPTY			2
#define I2O_RTN_TCB_ERROR			3
#define I2O_RTN_TRANSACTION_ERROR		4
#define I2O_RTN_ADAPTER_ALREADY_INIT		5
#define I2O_RTN_MALLOC_ERROR			6
#define I2O_RTN_ADPTR_NOT_REGISTERED		7
#define I2O_RTN_MSG_REPLY_TIMEOUT		8
#define I2O_RTN_NO_STATUS			9
#define I2O_RTN_NO_FIRM_VER			10
#define	I2O_RTN_NO_LINK_SPEED			11

/* Reply message status defines for all messages */

#define I2O_REPLY_STATUS_SUCCESS                    	0x00
#define I2O_REPLY_STATUS_ABORT_DIRTY                	0x01
#define I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER     	0x02
#define	I2O_REPLY_STATUS_ABORT_PARTIAL_TRANSFER		0x03
#define	I2O_REPLY_STATUS_ERROR_DIRTY			0x04
#define	I2O_REPLY_STATUS_ERROR_NO_DATA_TRANSFER		0x05
#define	I2O_REPLY_STATUS_ERROR_PARTIAL_TRANSFER		0x06
#define	I2O_REPLY_STATUS_PROCESS_ABORT_DIRTY		0x08
#define	I2O_REPLY_STATUS_PROCESS_ABORT_NO_DATA_TRANSFER	0x09
#define	I2O_REPLY_STATUS_PROCESS_ABORT_PARTIAL_TRANSFER	0x0A
#define	I2O_REPLY_STATUS_TRANSACTION_ERROR		0x0B
#define	I2O_REPLY_STATUS_PROGRESS_REPORT		0x80

/* Status codes and Error Information for Parameter functions */

#define I2O_PARAMS_STATUS_SUCCESS		0x00
#define I2O_PARAMS_STATUS_BAD_KEY_ABORT		0x01
#define I2O_PARAMS_STATUS_BAD_KEY_CONTINUE   	0x02
#define I2O_PARAMS_STATUS_BUFFER_FULL		0x03
#define I2O_PARAMS_STATUS_BUFFER_TOO_SMALL	0x04
#define I2O_PARAMS_STATUS_FIELD_UNREADABLE	0x05
#define I2O_PARAMS_STATUS_FIELD_UNWRITEABLE	0x06
#define I2O_PARAMS_STATUS_INSUFFICIENT_FIELDS	0x07
#define I2O_PARAMS_STATUS_INVALID_GROUP_ID	0x08
#define I2O_PARAMS_STATUS_INVALID_OPERATION	0x09
#define I2O_PARAMS_STATUS_NO_KEY_FIELD		0x0A
#define I2O_PARAMS_STATUS_NO_SUCH_FIELD		0x0B
#define I2O_PARAMS_STATUS_NON_DYNAMIC_GROUP	0x0C
#define I2O_PARAMS_STATUS_OPERATION_ERROR	0x0D
#define I2O_PARAMS_STATUS_SCALAR_ERROR		0x0E
#define I2O_PARAMS_STATUS_TABLE_ERROR		0x0F
#define I2O_PARAMS_STATUS_WRONG_GROUP_TYPE	0x10

/* DetailedStatusCode defines for Executive, DDM, Util and Transaction error
 * messages: Table 3-2 Detailed Status Codes.*/

#define I2O_DSC_SUCCESS                        0x0000
#define I2O_DSC_BAD_KEY                        0x0002
#define I2O_DSC_TCL_ERROR                      0x0003
#define I2O_DSC_REPLY_BUFFER_FULL              0x0004
#define I2O_DSC_NO_SUCH_PAGE                   0x0005
#define I2O_DSC_INSUFFICIENT_RESOURCE_SOFT     0x0006
#define I2O_DSC_INSUFFICIENT_RESOURCE_HARD     0x0007
#define I2O_DSC_CHAIN_BUFFER_TOO_LARGE         0x0009
#define I2O_DSC_UNSUPPORTED_FUNCTION           0x000A
#define I2O_DSC_DEVICE_LOCKED                  0x000B
#define I2O_DSC_DEVICE_RESET                   0x000C
#define I2O_DSC_INAPPROPRIATE_FUNCTION         0x000D
#define I2O_DSC_INVALID_INITIATOR_ADDRESS      0x000E
#define I2O_DSC_INVALID_MESSAGE_FLAGS          0x000F
#define I2O_DSC_INVALID_OFFSET                 0x0010
#define I2O_DSC_INVALID_PARAMETER              0x0011
#define I2O_DSC_INVALID_REQUEST                0x0012
#define I2O_DSC_INVALID_TARGET_ADDRESS         0x0013
#define I2O_DSC_MESSAGE_TOO_LARGE              0x0014
#define I2O_DSC_MESSAGE_TOO_SMALL              0x0015
#define I2O_DSC_MISSING_PARAMETER              0x0016
#define I2O_DSC_TIMEOUT                        0x0017
#define I2O_DSC_UNKNOWN_ERROR                  0x0018
#define I2O_DSC_UNKNOWN_FUNCTION               0x0019
#define I2O_DSC_UNSUPPORTED_VERSION            0x001A
#define I2O_DSC_DEVICE_BUSY                    0x001B
#define I2O_DSC_DEVICE_NOT_AVAILABLE           0x001C

/* DetailedStatusCode defines for Block Storage Operation: Table 6-7 Detailed
   Status Codes.*/

#define I2O_BSA_DSC_SUCCESS               0x0000
#define I2O_BSA_DSC_MEDIA_ERROR           0x0001
#define I2O_BSA_DSC_ACCESS_ERROR          0x0002
#define I2O_BSA_DSC_DEVICE_FAILURE        0x0003
#define I2O_BSA_DSC_DEVICE_NOT_READY      0x0004
#define I2O_BSA_DSC_MEDIA_NOT_PRESENT     0x0005
#define I2O_BSA_DSC_MEDIA_LOCKED          0x0006
#define I2O_BSA_DSC_MEDIA_FAILURE         0x0007
#define I2O_BSA_DSC_PROTOCOL_FAILURE      0x0008
#define I2O_BSA_DSC_BUS_FAILURE           0x0009
#define I2O_BSA_DSC_ACCESS_VIOLATION      0x000A
#define I2O_BSA_DSC_WRITE_PROTECTED       0x000B
#define I2O_BSA_DSC_DEVICE_RESET          0x000C
#define I2O_BSA_DSC_VOLUME_CHANGED        0x000D
#define I2O_BSA_DSC_TIMEOUT               0x000E

/* FailureStatusCodes, Table 3-3 Message Failure Codes */

#define I2O_FSC_TRANSPORT_SERVICE_SUSPENDED             0x81
#define I2O_FSC_TRANSPORT_SERVICE_TERMINATED            0x82
#define I2O_FSC_TRANSPORT_CONGESTION                    0x83
#define I2O_FSC_TRANSPORT_FAILURE                       0x84
#define I2O_FSC_TRANSPORT_STATE_ERROR                   0x85
#define I2O_FSC_TRANSPORT_TIME_OUT                      0x86
#define I2O_FSC_TRANSPORT_ROUTING_FAILURE               0x87
#define I2O_FSC_TRANSPORT_INVALID_VERSION               0x88
#define I2O_FSC_TRANSPORT_INVALID_OFFSET                0x89
#define I2O_FSC_TRANSPORT_INVALID_MSG_FLAGS             0x8A
#define I2O_FSC_TRANSPORT_FRAME_TOO_SMALL               0x8B
#define I2O_FSC_TRANSPORT_FRAME_TOO_LARGE               0x8C
#define I2O_FSC_TRANSPORT_INVALID_TARGET_ID             0x8D
#define I2O_FSC_TRANSPORT_INVALID_INITIATOR_ID          0x8E
#define I2O_FSC_TRANSPORT_INVALID_INITIATOR_CONTEXT     0x8F
#define I2O_FSC_TRANSPORT_UNKNOWN_FAILURE               0xFF

/* Device Claim Types */
#define	I2O_CLAIM_PRIMARY					0x01000000
#define	I2O_CLAIM_MANAGEMENT					0x02000000
#define	I2O_CLAIM_AUTHORIZED					0x03000000
#define	I2O_CLAIM_SECONDARY					0x04000000

/* Message header defines for VersionOffset */
#define I2OVER15	0x0001
#define I2OVER20	0x0002

/* Default is 1.5, FIXME: Need support for both 1.5 and 2.0 */
#define I2OVERSION	I2OVER15

#define SGL_OFFSET_0    I2OVERSION
#define SGL_OFFSET_4    (0x0040 | I2OVERSION)
#define SGL_OFFSET_5    (0x0050 | I2OVERSION)
#define SGL_OFFSET_6    (0x0060 | I2OVERSION)
#define SGL_OFFSET_7    (0x0070 | I2OVERSION)
#define SGL_OFFSET_8    (0x0080 | I2OVERSION)
#define SGL_OFFSET_9    (0x0090 | I2OVERSION)
#define SGL_OFFSET_10   (0x00A0 | I2OVERSION)

#define TRL_OFFSET_5    (0x0050 | I2OVERSION)
#define TRL_OFFSET_6    (0x0060 | I2OVERSION)

/* Transaction Reply Lists (TRL) Control Word structure */
#define TRL_SINGLE_FIXED_LENGTH		0x00
#define TRL_SINGLE_VARIABLE_LENGTH	0x40
#define TRL_MULTIPLE_FIXED_LENGTH	0x80

 /* msg header defines for MsgFlags */
#define MSG_STATIC	0x0100
#define MSG_64BIT_CNTXT	0x0200
#define MSG_MULTI_TRANS	0x1000
#define MSG_FAIL	0x2000
#define MSG_FINAL	0x4000
#define MSG_REPLY	0x8000

 /* minimum size msg */
#define THREE_WORD_MSG_SIZE	0x00030000
#define FOUR_WORD_MSG_SIZE	0x00040000
#define FIVE_WORD_MSG_SIZE	0x00050000
#define SIX_WORD_MSG_SIZE	0x00060000
#define SEVEN_WORD_MSG_SIZE	0x00070000
#define EIGHT_WORD_MSG_SIZE	0x00080000
#define NINE_WORD_MSG_SIZE	0x00090000
#define TEN_WORD_MSG_SIZE	0x000A0000
#define ELEVEN_WORD_MSG_SIZE	0x000B0000
#define I2O_MESSAGE_SIZE(x)	((x)<<16)

/* Special TID Assignments */

#define ADAPTER_TID		0
#define HOST_TID		1

#define MSG_FRAME_SIZE		128	/* i2o_scsi assumes >= 32 */
#define REPLY_FRAME_SIZE	17
#define SG_TABLESIZE		30
#define NMBR_MSG_FRAMES		128

#define MSG_POOL_SIZE		(MSG_FRAME_SIZE*NMBR_MSG_FRAMES*sizeof(u32))

#define I2O_POST_WAIT_OK	0
#define I2O_POST_WAIT_TIMEOUT	-ETIMEDOUT

#define I2O_CONTEXT_LIST_MIN_LENGTH	15
#define I2O_CONTEXT_LIST_USED		0x01
#define I2O_CONTEXT_LIST_DELETED	0x02

/* timeouts */
#define I2O_TIMEOUT_INIT_OUTBOUND_QUEUE	15
#define I2O_TIMEOUT_MESSAGE_GET		5
#define I2O_TIMEOUT_RESET		30
#define I2O_TIMEOUT_STATUS_GET		5
#define I2O_TIMEOUT_LCT_GET		360
#define I2O_TIMEOUT_SCSI_SCB_ABORT	240

/* retries */
#define I2O_HRT_GET_TRIES		3
#define I2O_LCT_GET_TRIES		3

/* request queue sizes */
#define I2O_MAX_SECTORS			1024
#define I2O_MAX_SEGMENTS		128

#define I2O_REQ_MEMPOOL_SIZE		32

#endif				/* __KERNEL__ */
#endif				/* _I2O_H */
