/*
 *	Functions to handle I2O controllers and I2O message handling
 *
 *	Copyright (C) 1999-2002	Red Hat Software
 *
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	A lot of the I2O message side code from this is taken from the
 *	Red Creek RCPCI45 adapter driver by Red Creek Communications
 *
 *	Fixes/additions:
 *		Philipp Rumpf
 *		Juha Siev�nen <Juha.Sievanen@cs.Helsinki.FI>
 *		Auvo H�kkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *		Deepak Saxena <deepak@plexity.net>
 *		Boji T Kannanthanam <boji.t.kannanthanam@intel.com>
 *		Alan Cox <alan@redhat.com>:
 *			Ported to Linux 2.5.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Minor fixes for 2.6.
 */

#include <linux/module.h>
#include <linux/i2o.h>

/* global I2O controller list */
LIST_HEAD(i2o_controllers);

/*
 * global I2O System Table. Contains information about all the IOPs in the
 * system. Used to inform IOPs about each others existence.
 */
static struct i2o_dma i2o_systab;

/* Module internal functions from other sources */
extern struct i2o_driver i2o_exec_driver;
extern int i2o_exec_lct_get(struct i2o_controller *);
extern void i2o_device_remove(struct i2o_device *);

extern int __init i2o_driver_init(void);
extern void __exit i2o_driver_exit(void);
extern int __init i2o_exec_init(void);
extern void __exit i2o_exec_exit(void);
extern int __init i2o_pci_init(void);
extern void __exit i2o_pci_exit(void);
extern int i2o_device_init(void);
extern void i2o_device_exit(void);

/**
 *	i2o_msg_nop - Returns a message which is not used
 *	@c: I2O controller from which the message was created
 *	@m: message which should be returned
 *
 *	If you fetch a message via i2o_msg_get, and can't use it, you must
 *	return the message with this function. Otherwise the message frame
 *	is lost.
 */
void i2o_msg_nop(struct i2o_controller *c, u32 m)
{
	struct i2o_message *msg = c->in_queue.virt + m;

	writel(THREE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_UTIL_NOP << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);
	writel(0, &msg->u.head[2]);
	writel(0, &msg->u.head[3]);
	i2o_msg_post(c, m);
};

/**
 *	i2o_msg_get_wait - obtain an I2O message from the IOP
 *	@c: I2O controller
 *	@msg: pointer to a I2O message pointer
 *	@wait: how long to wait until timeout
 *
 *	This function waits up to wait seconds for a message slot to be
 *	available.
 *
 *	On a success the message is returned and the pointer to the message is
 *	set in msg. The returned message is the physical page frame offset
 *	address from the read port (see the i2o spec). If no message is
 *	available returns I2O_QUEUE_EMPTY and msg is leaved untouched.
 */
u32 i2o_msg_get_wait(struct i2o_controller *c, struct i2o_message **msg,
		     int wait)
{
	unsigned long timeout = jiffies + wait * HZ;
	u32 m;

	while ((m = i2o_msg_get(c, msg)) == I2O_QUEUE_EMPTY) {
		if (time_after(jiffies, timeout)) {
			pr_debug("%s: Timeout waiting for message frame.\n",
				 c->name);
			return I2O_QUEUE_EMPTY;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	}

	return m;
};

#if BITS_PER_LONG == 64
/**
 *      i2o_cntxt_list_add - Append a pointer to context list and return a id
 *	@c: controller to which the context list belong
 *	@ptr: pointer to add to the context list
 *
 *	Because the context field in I2O is only 32-bit large, on 64-bit the
 *	pointer is to large to fit in the context field. The i2o_cntxt_list
 *	functions therefore map pointers to context fields.
 *
 *	Returns context id > 0 on success or 0 on failure.
 */
u32 i2o_cntxt_list_add(struct i2o_controller *c, void *ptr)
{
	struct i2o_context_list_element *entry;
	unsigned long flags;

	if (!ptr)
		printk(KERN_ERR "NULL pointer found!\n");

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		printk(KERN_ERR "i2o: Could not allocate memory for context "
		       "list element\n");
		return 0;
	}

	entry->ptr = ptr;
	entry->timestamp = jiffies;
	INIT_LIST_HEAD(&entry->list);

	spin_lock_irqsave(&c->context_list_lock, flags);

	if (unlikely(atomic_inc_and_test(&c->context_list_counter)))
		atomic_inc(&c->context_list_counter);

	entry->context = atomic_read(&c->context_list_counter);

	list_add(&entry->list, &c->context_list);

	spin_unlock_irqrestore(&c->context_list_lock, flags);

	pr_debug("Add context to list %p -> %d\n", ptr, context);

	return entry->context;
};

/**
 *      i2o_cntxt_list_remove - Remove a pointer from the context list
 *	@c: controller to which the context list belong
 *	@ptr: pointer which should be removed from the context list
 *
 *	Removes a previously added pointer from the context list and returns
 *	the matching context id.
 *
 *	Returns context id on succes or 0 on failure.
 */
u32 i2o_cntxt_list_remove(struct i2o_controller *c, void *ptr)
{
	struct i2o_context_list_element *entry;
	u32 context = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->ptr == ptr) {
		list_del(&entry->list);
		context = entry->context;
		kfree(entry);
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!context)
		printk(KERN_WARNING "i2o: Could not remove nonexistent ptr "
		       "%p\n", ptr);

	pr_debug("remove ptr from context list %d -> %p\n", context, ptr);

	return context;
};

/**
 *      i2o_cntxt_list_get - Get a pointer from the context list and remove it
 *	@c: controller to which the context list belong
 *	@context: context id to which the pointer belong
 *
 *	Returns pointer to the matching context id on success or NULL on
 *	failure.
 */
void *i2o_cntxt_list_get(struct i2o_controller *c, u32 context)
{
	struct i2o_context_list_element *entry;
	unsigned long flags;
	void *ptr = NULL;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->context == context) {
		list_del(&entry->list);
		ptr = entry->ptr;
		kfree(entry);
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!ptr)
		printk(KERN_WARNING "i2o: context id %d not found\n", context);

	pr_debug("get ptr from context list %d -> %p\n", context, ptr);

	return ptr;
};

/**
 *      i2o_cntxt_list_get_ptr - Get a context id from the context list
 *	@c: controller to which the context list belong
 *	@ptr: pointer to which the context id should be fetched
 *
 *	Returns context id which matches to the pointer on succes or 0 on
 *	failure.
 */
u32 i2o_cntxt_list_get_ptr(struct i2o_controller * c, void *ptr)
{
	struct i2o_context_list_element *entry;
	u32 context = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->ptr == ptr) {
		context = entry->context;
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!context)
		printk(KERN_WARNING "i2o: Could not find nonexistent ptr "
		       "%p\n", ptr);

	pr_debug("get context id from context list %p -> %d\n", ptr, context);

	return context;
};
#endif

/**
 *	i2o_iop_find - Find an I2O controller by id
 *	@unit: unit number of the I2O controller to search for
 *
 *	Lookup the I2O controller on the controller list.
 *
 *	Returns pointer to the I2O controller on success or NULL if not found.
 */
struct i2o_controller *i2o_find_iop(int unit)
{
	struct i2o_controller *c;

	list_for_each_entry(c, &i2o_controllers, list) {
		if (c->unit == unit)
			return c;
	}

	return NULL;
};

/**
 *	i2o_iop_find_device - Find a I2O device on an I2O controller
 *	@c: I2O controller where the I2O device hangs on
 *	@tid: TID of the I2O device to search for
 *
 *	Searches the devices of the I2O controller for a device with TID tid and
 *	returns it.
 *
 *	Returns a pointer to the I2O device if found, otherwise NULL.
 */
struct i2o_device *i2o_iop_find_device(struct i2o_controller *c, u16 tid)
{
	struct i2o_device *dev;

	list_for_each_entry(dev, &c->devices, list)
	    if (dev->lct_data.tid == tid)
		return dev;

	return NULL;
};

/**
 *	i2o_quiesce_controller - quiesce controller
 *	@c: controller
 *
 *	Quiesce an IOP. Causes IOP to make external operation quiescent
 *	(i2o 'READY' state). Internal operation of the IOP continues normally.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_quiesce(struct i2o_controller *c)
{
	struct i2o_message *msg;
	u32 m;
	i2o_status_block *sb = c->status_block.virt;
	int rc;

	i2o_status_get(c);

	/* SysQuiesce discarded if IOP not in READY or OPERATIONAL state */
	if ((sb->iop_state != ADAPTER_STATE_READY) &&
	    (sb->iop_state != ADAPTER_STATE_OPERATIONAL))
		return 0;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_SYS_QUIESCE << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);

	/* Long timeout needed for quiesce if lots of devices */
	if ((rc = i2o_msg_post_wait(c, m, 240)))
		printk(KERN_INFO "%s: Unable to quiesce (status=%#x).\n",
		       c->name, -rc);
	else
		pr_debug("%s: Quiesced.\n", c->name);

	i2o_status_get(c);	// Entered READY state

	return rc;
};

/**
 *	i2o_iop_enable - move controller from ready to OPERATIONAL
 *	@c: I2O controller
 *
 *	Enable IOP. This allows the IOP to resume external operations and
 *	reverses the effect of a quiesce. Returns zero or an error code if
 *	an error occurs.
 */
static int i2o_iop_enable(struct i2o_controller *c)
{
	struct i2o_message *msg;
	u32 m;
	i2o_status_block *sb = c->status_block.virt;
	int rc;

	i2o_status_get(c);

	/* Enable only allowed on READY state */
	if (sb->iop_state != ADAPTER_STATE_READY)
		return -EINVAL;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_SYS_ENABLE << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);

	/* How long of a timeout do we need? */
	if ((rc = i2o_msg_post_wait(c, m, 240)))
		printk(KERN_ERR "%s: Could not enable (status=%#x).\n",
		       c->name, -rc);
	else
		pr_debug("%s: Enabled.\n", c->name);

	i2o_status_get(c);	// entered OPERATIONAL state

	return rc;
};

/**
 *	i2o_iop_quiesce_all - Quiesce all I2O controllers on the system
 *
 *	Quiesce all I2O controllers which are connected to the system.
 */
static inline void i2o_iop_quiesce_all(void)
{
	struct i2o_controller *c, *tmp;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list) {
		if (!c->no_quiesce)
			i2o_iop_quiesce(c);
	}
};

/**
 *	i2o_iop_enable_all - Enables all controllers on the system
 *
 *	Enables all I2O controllers which are connected to the system.
 */
static inline void i2o_iop_enable_all(void)
{
	struct i2o_controller *c, *tmp;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list)
	    i2o_iop_enable(c);
};

/**
 *	i2o_clear_controller - Bring I2O controller into HOLD state
 *	@c: controller
 *
 *	Clear an IOP to HOLD state, ie. terminate external operations, clear all
 *	input queues and prepare for a system restart. IOP's internal operation
 *	continues normally and the outbound queue is alive. The IOP is not
 *	expected to rebuild its LCT.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_clear(struct i2o_controller *c)
{
	struct i2o_message *msg;
	u32 m;
	int rc;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	/* Quiesce all IOPs first */
	i2o_iop_quiesce_all();

	writel(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_ADAPTER_CLEAR << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);

	if ((rc = i2o_msg_post_wait(c, m, 30)))
		printk(KERN_INFO "%s: Unable to clear (status=%#x).\n",
		       c->name, -rc);
	else
		pr_debug("%s: Cleared.\n", c->name);

	/* Enable all IOPs */
	i2o_iop_enable_all();

	i2o_status_get(c);

	return rc;
}

/**
 *	i2o_iop_reset - reset an I2O controller
 *	@c: controller to reset
 *
 *	Reset the IOP into INIT state and wait until IOP gets into RESET state.
 *	Terminate all external operations, clear IOP's inbound and outbound
 *	queues, terminate all DDMs, and reload the IOP's operating environment
 *	and all local DDMs. The IOP rebuilds its LCT.
 */
static int i2o_iop_reset(struct i2o_controller *c)
{
	u8 *status = c->status.virt;
	struct i2o_message *msg;
	u32 m;
	unsigned long timeout;
	i2o_status_block *sb = c->status_block.virt;
	int rc = 0;

	pr_debug("Resetting controller\n");

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	memset(status, 0, 4);

	/* Quiesce all IOPs first */
	i2o_iop_quiesce_all();

	writel(EIGHT_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_ADAPTER_RESET << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);
	writel(i2o_exec_driver.context, &msg->u.s.icntxt);
	writel(0, &msg->u.s.tcntxt);	//FIXME: use reasonable transaction context
	writel(0, &msg->body[0]);
	writel(0, &msg->body[1]);
	writel(i2o_ptr_low((void *)c->status.phys), &msg->body[2]);
	writel(i2o_ptr_high((void *)c->status.phys), &msg->body[3]);

	i2o_msg_post(c, m);

	/* Wait for a reply */
	timeout = jiffies + I2O_TIMEOUT_RESET * HZ;
	while (!*status) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "IOP reset timeout.\n");
			rc = -ETIMEDOUT;
			goto exit;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);

		rmb();
	}

	if (*status == I2O_CMD_IN_PROGRESS) {
		/*
		 * Once the reset is sent, the IOP goes into the INIT state
		 * which is indeterminate.  We need to wait until the IOP
		 * has rebooted before we can let the system talk to
		 * it. We read the inbound Free_List until a message is
		 * available. If we can't read one in the given ammount of
		 * time, we assume the IOP could not reboot properly.
		 */
		pr_debug("%s: Reset in progress, waiting for reboot...\n",
			 c->name);

		m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_RESET);
		while (m == I2O_QUEUE_EMPTY) {
			if (time_after(jiffies, timeout)) {
				printk(KERN_ERR "IOP reset timeout.\n");
				rc = -ETIMEDOUT;
				goto exit;
			}
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);

			m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_RESET);
		}
		i2o_msg_nop(c, m);
	}

	/* from here all quiesce commands are safe */
	c->no_quiesce = 0;

	/* If IopReset was rejected or didn't perform reset, try IopClear */
	i2o_status_get(c);
	if (*status == I2O_CMD_REJECTED || sb->iop_state != ADAPTER_STATE_RESET) {
		printk(KERN_WARNING "%s: Reset rejected, trying to clear\n",
		       c->name);
		i2o_iop_clear(c);
	} else
		pr_debug("%s: Reset completed.\n", c->name);

      exit:
	/* Enable all IOPs */
	i2o_iop_enable_all();

	return rc;
};

/**
 *	i2o_iop_init_outbound_queue - setup the outbound message queue
 *	@c: I2O controller
 *
 *	Clear and (re)initialize IOP's outbound queue and post the message
 *	frames to the IOP.
 *
 *	Returns 0 on success or a negative errno code on failure.
 */
int i2o_iop_init_outbound_queue(struct i2o_controller *c)
{
	u8 *status = c->status.virt;
	u32 m;
	struct i2o_message *msg;
	ulong timeout;
	int i;

	pr_debug("%s: Initializing Outbound Queue...\n", c->name);

	memset(status, 0, 4);

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(EIGHT_WORD_MSG_SIZE | TRL_OFFSET_6, &msg->u.head[0]);
	writel(I2O_CMD_OUTBOUND_INIT << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);
	writel(i2o_exec_driver.context, &msg->u.s.icntxt);
	writel(0x0106, &msg->u.s.tcntxt);	/* FIXME: why 0x0106, maybe in
						   Spec? */
	writel(PAGE_SIZE, &msg->body[0]);
	writel(MSG_FRAME_SIZE << 16 | 0x80, &msg->body[1]);	/* Outbound msg frame
								   size in words and Initcode */
	writel(0xd0000004, &msg->body[2]);
	writel(i2o_ptr_low((void *)c->status.phys), &msg->body[3]);
	writel(i2o_ptr_high((void *)c->status.phys), &msg->body[4]);

	i2o_msg_post(c, m);

	timeout = jiffies + I2O_TIMEOUT_INIT_OUTBOUND_QUEUE * HZ;
	while (*status <= I2O_CMD_IN_PROGRESS) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_WARNING "%s: Timeout Initializing\n",
			       c->name);
			return -ETIMEDOUT;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);

		rmb();
	}

	m = c->out_queue.phys;

	/* Post frames */
	for (i = 0; i < NMBR_MSG_FRAMES; i++) {
		i2o_flush_reply(c, m);
		m += MSG_FRAME_SIZE * 4;
	}

	return 0;
}

/**
 *	i2o_iop_activate - Bring controller up to HOLD
 *	@c: controller
 *
 *	This function brings an I2O controller into HOLD state. The adapter
 *	is reset if necessary and then the queues and resource table are read.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_activate(struct i2o_controller *c)
{
	i2o_status_block *sb = c->status_block.virt;
	int rc;
	/* In INIT state, Wait Inbound Q to initialize (in i2o_status_get) */
	/* In READY state, Get status */

	rc = i2o_status_get(c);
	if (rc) {
		printk(KERN_INFO "Unable to obtain status of %s, "
		       "attempting a reset.\n", c->name);
		if (i2o_iop_reset(c))
			return rc;
	}

	if (sb->i2o_version > I2OVER15) {
		printk(KERN_ERR "%s: Not running vrs. 1.5. of the I2O "
		       "Specification.\n", c->name);
		return -ENODEV;
	}

	switch (sb->iop_state) {
	case ADAPTER_STATE_FAULTED:
		printk(KERN_CRIT "%s: hardware fault\n", c->name);
		return -ENODEV;

	case ADAPTER_STATE_READY:
	case ADAPTER_STATE_OPERATIONAL:
	case ADAPTER_STATE_HOLD:
	case ADAPTER_STATE_FAILED:
		pr_debug("already running, trying to reset...\n");
		if (i2o_iop_reset(c))
			return -ENODEV;
	}

	rc = i2o_iop_init_outbound_queue(c);
	if (rc)
		return rc;

	/* In HOLD state */

	rc = i2o_hrt_get(c);
	if (rc)
		return rc;

	return 0;
};

/**
 *	i2o_iop_systab_set - Set the I2O System Table of the specified IOP
 *	@c: I2O controller to which the system table should be send
 *
 *	Before the systab could be set i2o_systab_build() must be called.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_systab_set(struct i2o_controller *c)
{
	struct i2o_message *msg;
	u32 m;
	i2o_status_block *sb = c->status_block.virt;
	struct device *dev = &c->pdev->dev;
	struct resource *root;
	int rc;

	if (sb->current_mem_size < sb->desired_mem_size) {
		struct resource *res = &c->mem_resource;
		res->name = c->pdev->bus->name;
		res->flags = IORESOURCE_MEM;
		res->start = 0;
		res->end = 0;
		printk("%s: requires private memory resources.\n", c->name);
		root = pci_find_parent_resource(c->pdev, res);
		if (root == NULL)
			printk("Can't find parent resource!\n");
		if (root && allocate_resource(root, res, sb->desired_mem_size, sb->desired_mem_size, sb->desired_mem_size, 1 << 20,	/* Unspecified, so use 1Mb and play safe */
					      NULL, NULL) >= 0) {
			c->mem_alloc = 1;
			sb->current_mem_size = 1 + res->end - res->start;
			sb->current_mem_base = res->start;
			printk(KERN_INFO
			       "%s: allocated %ld bytes of PCI memory at 0x%08lX.\n",
			       c->name, 1 + res->end - res->start, res->start);
		}
	}

	if (sb->current_io_size < sb->desired_io_size) {
		struct resource *res = &c->io_resource;
		res->name = c->pdev->bus->name;
		res->flags = IORESOURCE_IO;
		res->start = 0;
		res->end = 0;
		printk("%s: requires private memory resources.\n", c->name);
		root = pci_find_parent_resource(c->pdev, res);
		if (root == NULL)
			printk("Can't find parent resource!\n");
		if (root && allocate_resource(root, res, sb->desired_io_size, sb->desired_io_size, sb->desired_io_size, 1 << 20,	/* Unspecified, so use 1Mb and play safe */
					      NULL, NULL) >= 0) {
			c->io_alloc = 1;
			sb->current_io_size = 1 + res->end - res->start;
			sb->current_mem_base = res->start;
			printk(KERN_INFO
			       "%s: allocated %ld bytes of PCI I/O at 0x%08lX.\n",
			       c->name, 1 + res->end - res->start, res->start);
		}
	}

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	i2o_systab.phys = dma_map_single(dev, i2o_systab.virt, i2o_systab.len,
					 PCI_DMA_TODEVICE);
	if (!i2o_systab.phys) {
		i2o_msg_nop(c, m);
		return -ENOMEM;
	}

	writel(I2O_MESSAGE_SIZE(12) | SGL_OFFSET_6, &msg->u.head[0]);
	writel(I2O_CMD_SYS_TAB_SET << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);

	/*
	 * Provide three SGL-elements:
	 * System table (SysTab), Private memory space declaration and
	 * Private i/o space declaration
	 *
	 * FIXME: is this still true?
	 * Nasty one here. We can't use dma_alloc_coherent to send the
	 * same table to everyone. We have to go remap it for them all
	 */

	writel(c->unit + 2, &msg->body[0]);
	writel(0, &msg->body[1]);
	writel(0x54000000 | i2o_systab.phys, &msg->body[2]);
	writel(i2o_systab.phys, &msg->body[3]);
	writel(0x54000000 | sb->current_mem_size, &msg->body[4]);
	writel(sb->current_mem_base, &msg->body[5]);
	writel(0xd4000000 | sb->current_io_size, &msg->body[6]);
	writel(sb->current_io_base, &msg->body[6]);

	rc = i2o_msg_post_wait(c, m, 120);

	dma_unmap_single(dev, i2o_systab.phys, i2o_systab.len,
			 PCI_DMA_TODEVICE);

	if (rc < 0)
		printk(KERN_ERR "%s: Unable to set SysTab (status=%#x).\n",
		       c->name, -rc);
	else
		pr_debug("%s: SysTab set.\n", c->name);

	i2o_status_get(c);	// Entered READY state

	return rc;
}

/**
 *	i2o_iop_online - Bring a controller online into OPERATIONAL state.
 *	@c: I2O controller
 *
 *	Send the system table and enable the I2O controller.
 *
 *	Returns 0 on success or negativer error code on failure.
 */
static int i2o_iop_online(struct i2o_controller *c)
{
	int rc;

	rc = i2o_iop_systab_set(c);
	if (rc)
		return rc;

	/* In READY state */
	pr_debug("%s: Attempting to enable...\n", c->name);
	rc = i2o_iop_enable(c);
	if (rc)
		return rc;

	return 0;
};

/**
 *	i2o_iop_remove - Remove the I2O controller from the I2O core
 *	@c: I2O controller
 *
 *	Remove the I2O controller from the I2O core. If devices are attached to
 *	the controller remove these also and finally reset the controller.
 */
void i2o_iop_remove(struct i2o_controller *c)
{
	struct i2o_device *dev, *tmp;

	pr_debug("Deleting controller %s\n", c->name);

	i2o_driver_notify_controller_remove_all(c);

	list_del(&c->list);

	list_for_each_entry_safe(dev, tmp, &c->devices, list)
	    i2o_device_remove(dev);

	/* Ask the IOP to switch to RESET state */
	i2o_iop_reset(c);
}

/**
 *	i2o_systab_build - Build system table
 *
 *	The system table contains information about all the IOPs in the system
 *	(duh) and is used by the Executives on the IOPs to establish peer2peer
 *	connections. We're not supporting peer2peer at the moment, but this
 *	will be needed down the road for things like lan2lan forwarding.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_systab_build(void)
{
	struct i2o_controller *c, *tmp;
	int num_controllers = 0;
	u32 change_ind = 0;
	int count = 0;
	struct i2o_sys_tbl *systab = i2o_systab.virt;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list)
	    num_controllers++;

	if (systab) {
		change_ind = systab->change_ind;
		kfree(i2o_systab.virt);
	}

	/* Header + IOPs */
	i2o_systab.len = sizeof(struct i2o_sys_tbl) + num_controllers *
	    sizeof(struct i2o_sys_tbl_entry);

	systab = i2o_systab.virt = kmalloc(i2o_systab.len, GFP_KERNEL);
	if (!systab) {
		printk(KERN_ERR "i2o: unable to allocate memory for System "
		       "Table\n");
		return -ENOMEM;
	}
	memset(systab, 0, i2o_systab.len);

	systab->version = I2OVERSION;
	systab->change_ind = change_ind + 1;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list) {
		i2o_status_block *sb;

		if (count >= num_controllers) {
			printk(KERN_ERR "i2o: controller added while building "
			       "system table\n");
			break;
		}

		sb = c->status_block.virt;

		/*
		 * Get updated IOP state so we have the latest information
		 *
		 * We should delete the controller at this point if it
		 * doesn't respond since if it's not on the system table
		 * it is techninically not part of the I2O subsystem...
		 */
		if (unlikely(i2o_status_get(c))) {
			printk(KERN_ERR "%s: Deleting b/c could not get status"
			       " while attempting to build system table\n",
			       c->name);
			i2o_iop_remove(c);
			continue;	// try the next one
		}

		systab->iops[count].org_id = sb->org_id;
		systab->iops[count].iop_id = c->unit + 2;
		systab->iops[count].seg_num = 0;
		systab->iops[count].i2o_version = sb->i2o_version;
		systab->iops[count].iop_state = sb->iop_state;
		systab->iops[count].msg_type = sb->msg_type;
		systab->iops[count].frame_size = sb->inbound_frame_size;
		systab->iops[count].last_changed = change_ind;
		systab->iops[count].iop_capabilities = sb->iop_capabilities;
		systab->iops[count].inbound_low = i2o_ptr_low(c->post_port);
		systab->iops[count].inbound_high = i2o_ptr_high(c->post_port);

		count++;
	}

	systab->num_entries = count;

	return 0;
};

/**
 *	i2o_parse_hrt - Parse the hardware resource table.
 *	@c: I2O controller
 *
 *	We don't do anything with it except dumping it (in debug mode).
 *
 *	Returns 0.
 */
static int i2o_parse_hrt(struct i2o_controller *c)
{
	i2o_dump_hrt(c);
	return 0;
};

/**
 *	i2o_status_get - Get the status block from the I2O controller
 *	@c: I2O controller
 *
 *	Issue a status query on the controller. This updates the attached
 *	status block. The status block could then be accessed through
 *	c->status_block.
 *
 *	Returns 0 on sucess or negative error code on failure.
 */
int i2o_status_get(struct i2o_controller *c)
{
	struct i2o_message *msg;
	u32 m;
	u8 *status_block;
	unsigned long timeout;

	status_block = (u8 *) c->status_block.virt;
	memset(status_block, 0, sizeof(i2o_status_block));

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(NINE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_STATUS_GET << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);
	writel(i2o_exec_driver.context, &msg->u.s.icntxt);
	writel(0, &msg->u.s.tcntxt);	// FIXME: use resonable transaction context
	writel(0, &msg->body[0]);
	writel(0, &msg->body[1]);
	writel(i2o_ptr_low((void *)c->status_block.phys), &msg->body[2]);
	writel(i2o_ptr_high((void *)c->status_block.phys), &msg->body[3]);
	writel(sizeof(i2o_status_block), &msg->body[4]);	/* always 88 bytes */

	i2o_msg_post(c, m);

	/* Wait for a reply */
	timeout = jiffies + I2O_TIMEOUT_STATUS_GET * HZ;
	while (status_block[87] != 0xFF) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "%s: Get status timeout.\n", c->name);
			return -ETIMEDOUT;
		}

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);

		rmb();
	}

#ifdef DEBUG
	i2o_debug_state(c);
#endif

	return 0;
}

/*
 *	i2o_hrt_get - Get the Hardware Resource Table from the I2O controller
 *	@c: I2O controller from which the HRT should be fetched
 *
 *	The HRT contains information about possible hidden devices but is
 *	mostly useless to us.
 *
 *	Returns 0 on success or negativer error code on failure.
 */
int i2o_hrt_get(struct i2o_controller *c)
{
	int rc;
	int i;
	i2o_hrt *hrt = c->hrt.virt;
	u32 size = sizeof(i2o_hrt);
	struct device *dev = &c->pdev->dev;

	for (i = 0; i < I2O_HRT_GET_TRIES; i++) {
		struct i2o_message *msg;
		u32 m;

		m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
		if (m == I2O_QUEUE_EMPTY)
			return -ETIMEDOUT;

		writel(SIX_WORD_MSG_SIZE | SGL_OFFSET_4, &msg->u.head[0]);
		writel(I2O_CMD_HRT_GET << 24 | HOST_TID << 12 | ADAPTER_TID,
		       &msg->u.head[1]);
		writel(0xd0000000 | c->hrt.len, &msg->body[0]);
		writel(c->hrt.phys, &msg->body[1]);

		rc = i2o_msg_post_wait_mem(c, m, 20, &c->hrt);

		if (rc < 0) {
			printk(KERN_ERR "%s: Unable to get HRT (status=%#x)\n",
			       c->name, -rc);
			return rc;
		}

		size = hrt->num_entries * hrt->entry_len << 2;
		if (size > c->hrt.len) {
			if (i2o_dma_realloc(dev, &c->hrt, size, GFP_KERNEL))
				return -ENOMEM;
			else
				hrt = c->hrt.virt;
		} else
			return i2o_parse_hrt(c);
	}

	printk(KERN_ERR "%s: Unable to get HRT after %d tries, giving up\n",
	       c->name, I2O_HRT_GET_TRIES);

	return -EBUSY;
}

/**
 *	i2o_iop_alloc - Allocate and initialize a i2o_controller struct
 *
 *	Allocate the necessary memory for a i2o_controller struct and
 *	initialize the lists.
 *
 *	Returns a pointer to the I2O controller or a negative error code on
 *	failure.
 */
struct i2o_controller *i2o_iop_alloc(void)
{
	static int unit = 0;	/* 0 and 1 are NULL IOP and Local Host */
	struct i2o_controller *c;

	c = kmalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		printk(KERN_ERR "i2o: Insufficient memory to allocate the "
		       "controller.\n");
		return ERR_PTR(-ENOMEM);
	}
	memset(c, 0, sizeof(*c));

	INIT_LIST_HEAD(&c->devices);
	c->lock = SPIN_LOCK_UNLOCKED;
	init_MUTEX(&c->lct_lock);
	c->unit = unit++;
	sprintf(c->name, "iop%d", c->unit);

#if BITS_PER_LONG == 64
	c->context_list_lock = SPIN_LOCK_UNLOCKED;
	atomic_set(&c->context_list_counter, 0);
	INIT_LIST_HEAD(&c->context_list);
#endif

	return c;
};

/**
 *	i2o_iop_free - Free the i2o_controller struct
 *	@c: I2O controller to free
 */
void i2o_iop_free(struct i2o_controller *c)
{
	kfree(c);
};

/**
 *	i2o_iop_add - Initialize the I2O controller and add him to the I2O core
 *	@c: controller
 *
 *	Initialize the I2O controller and if no error occurs add him to the I2O
 *	core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_iop_add(struct i2o_controller *c)
{
	int rc;

	printk(KERN_INFO "%s: Activating I2O controller...\n", c->name);
	printk(KERN_INFO "%s: This may take a few minutes if there are many "
	       "devices\n", c->name);

	if ((rc = i2o_iop_activate(c))) {
		printk(KERN_ERR "%s: controller could not activated\n",
		       c->name);
		i2o_iop_reset(c);
		return rc;
	}

	pr_debug("building sys table %s...\n", c->name);

	if ((rc = i2o_systab_build())) {
		i2o_iop_reset(c);
		return rc;
	}

	pr_debug("online controller %s...\n", c->name);

	if ((rc = i2o_iop_online(c))) {
		i2o_iop_reset(c);
		return rc;
	}

	pr_debug("getting LCT %s...\n", c->name);

	if ((rc = i2o_exec_lct_get(c))) {
		i2o_iop_reset(c);
		return rc;
	}

	list_add(&c->list, &i2o_controllers);

	i2o_driver_notify_controller_add_all(c);

	printk(KERN_INFO "%s: Controller added\n", c->name);

	return 0;
};

/**
 *	i2o_event_register - Turn on/off event notification for a I2O device
 *	@dev: I2O device which should receive the event registration request
 *	@drv: driver which want to get notified
 *	@tcntxt: transaction context to use with this notifier
 *	@evt_mask: mask of events
 *
 *	Create and posts an event registration message to the task. No reply
 *	is waited for, or expected. If you do not want further notifications,
 *	call the i2o_event_register again with a evt_mask of 0.
 *
 *	Returns 0 on success or -ETIMEDOUT if no message could be fetched for
 *	sending the request.
 */
int i2o_event_register(struct i2o_device *dev, struct i2o_driver *drv,
		       int tcntxt, u32 evt_mask)
{
	struct i2o_controller *c = dev->iop;
	struct i2o_message *msg;
	u32 m;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_UTIL_EVT_REGISTER << 24 | HOST_TID << 12 | dev->lct_data.
	       tid, &msg->u.head[1]);
	writel(drv->context, &msg->u.s.icntxt);
	writel(tcntxt, &msg->u.s.tcntxt);
	writel(evt_mask, &msg->body[0]);

	i2o_msg_post(c, m);

	return 0;
};

/**
 *	i2o_iop_init - I2O main initialization function
 *
 *	Initialize the I2O drivers (OSM) functions, register the Executive OSM,
 *	initialize the I2O PCI part and finally initialize I2O device stuff.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_iop_init(void)
{
	int rc = 0;

	printk(KERN_INFO "I2O Core - (C) Copyright 1999 Red Hat Software\n");

	rc = i2o_device_init();
	if (rc)
		goto exit;

	rc = i2o_driver_init();
	if (rc)
		goto device_exit;

	rc = i2o_exec_init();
	if (rc)
		goto driver_exit;

	rc = i2o_pci_init();
	if (rc < 0)
		goto exec_exit;

	return 0;

      exec_exit:
	i2o_exec_exit();

      driver_exit:
	i2o_driver_exit();

      device_exit:
	i2o_device_exit();

      exit:
	return rc;
}

/**
 *	i2o_iop_exit - I2O main exit function
 *
 *	Removes I2O controllers from PCI subsystem and shut down OSMs.
 */
static void __exit i2o_iop_exit(void)
{
	i2o_pci_exit();
	i2o_exec_exit();
	i2o_driver_exit();
	i2o_device_exit();
};

module_init(i2o_iop_init);
module_exit(i2o_iop_exit);

MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O Core");
MODULE_LICENSE("GPL");

#if BITS_PER_LONG == 64
EXPORT_SYMBOL(i2o_cntxt_list_add);
EXPORT_SYMBOL(i2o_cntxt_list_get);
EXPORT_SYMBOL(i2o_cntxt_list_remove);
EXPORT_SYMBOL(i2o_cntxt_list_get_ptr);
#endif
EXPORT_SYMBOL(i2o_msg_get_wait);
EXPORT_SYMBOL(i2o_msg_nop);
EXPORT_SYMBOL(i2o_find_iop);
EXPORT_SYMBOL(i2o_iop_find_device);
EXPORT_SYMBOL(i2o_event_register);
EXPORT_SYMBOL(i2o_status_get);
EXPORT_SYMBOL(i2o_hrt_get);
EXPORT_SYMBOL(i2o_controllers);
