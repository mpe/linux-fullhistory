/*
 *  osd.c - Linux specific code
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include "acpi.h"

#define ACPI_MAX_THROTTLE 10
#define ACPI_INVALID ~0UL
#define ACPI_INFINITE ~0UL

static struct acpi_facp *acpi_facp = NULL;

static spinlock_t acpi_event_lock = SPIN_LOCK_UNLOCKED;
static volatile u32 acpi_event_status = 0;
static volatile acpi_sstate_t acpi_event_state = ACPI_S0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_event_wait);
static int acpi_irq_irq = 0;
static OSD_HANDLER acpi_irq_handler = NULL;
static void *acpi_irq_context = NULL;

static unsigned long acpi_pblk = ACPI_INVALID;
static unsigned long acpi_c2_exit_latency = ACPI_INFINITE;
static unsigned long acpi_c3_exit_latency = ACPI_INFINITE;
static unsigned long acpi_c2_enter_latency = ACPI_INFINITE;
static unsigned long acpi_c3_enter_latency = ACPI_INFINITE;
static int acpi_c2_tested = 0;
static int acpi_c3_tested = 0;

struct acpi_intrp_entry
{
	int priority;
	OSD_EXECUTION_CALLBACK callback;
	void *context;
	struct list_head list;
};

struct acpi_enter_sx_ctx
{
	wait_queue_head_t wait;
	int state;
};

static volatile int acpi_intrp_pid = -1;
static DECLARE_WAIT_QUEUE_HEAD(acpi_intrp_wait);
static LIST_HEAD(acpi_intrp_exec);
static spinlock_t acpi_intrp_exec_lock = SPIN_LOCK_UNLOCKED;

#define ACPI_SLP_TYP(typa, typb) (((int)(typa) << 8) | (int)(typb))
#define ACPI_SLP_TYPA(value) \
        ((((value) >> 8) << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK)
#define ACPI_SLP_TYPB(value) \
        ((((value) & 0xff) << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK)

static volatile acpi_sstate_t acpi_sleep_state = ACPI_S0;
static unsigned long acpi_slptyp[ACPI_S5 + 1] = {ACPI_INVALID,};

static struct ctl_table_header *acpi_sysctl = NULL;

static int acpi_do_ulong(ctl_table * ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t * len);
static int acpi_do_sleep(ctl_table * ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t * len);
static int acpi_do_event(ctl_table * ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t * len);

static struct ctl_table acpi_table[] =
{
	{ACPI_P_LVL2_LAT, "c2_exit_latency",
	 &acpi_c2_exit_latency, sizeof(acpi_c2_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL2_LAT, "c2_enter_latency",
	 &acpi_c2_enter_latency, sizeof(acpi_c2_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3_LAT, "c3_exit_latency",
	 &acpi_c3_exit_latency, sizeof(acpi_c3_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL3_LAT, "c3_enter_latency",
	 &acpi_c3_enter_latency, sizeof(acpi_c3_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &acpi_do_sleep},

	{ACPI_EVENT, "event", NULL, 0, 0400, NULL, &acpi_do_event},

	{0}
};

static struct ctl_table acpi_dir_table[] =
{
	{CTL_ACPI, "acpi", NULL, 0, 0555, acpi_table},
	{0}
};

static u32 FASTCALL(acpi_read_pm1_control(struct acpi_facp *));
static u32 FASTCALL(acpi_read_pm1_status(struct acpi_facp *));
static void FASTCALL(acpi_write_pm1_control(struct acpi_facp *, u32));
static void FASTCALL(acpi_write_pm1_status(struct acpi_facp *, u32));

/*
 * Get the value of the PM1 control register (ACPI_SCI_EN, ...)
 */
static u32
acpi_read_pm1_control(struct acpi_facp *facp)
{
	u32 value = 0;
	if (facp->pm1a_cnt)
		value = inw(facp->pm1a_cnt);
	if (facp->pm1b_cnt)
		value |= inw(facp->pm1b_cnt);
	return value;
}

/*
 * Set the value of the PM1 control register (ACPI_BM_RLD, ...)
 */
static void 
acpi_write_pm1_control(struct acpi_facp *facp, u32 value)
{
	if (facp->pm1a_cnt)
		outw(value, facp->pm1a_cnt);
	if (facp->pm1b_cnt)
		outw(value, facp->pm1b_cnt);
}

/*
 * Get the value of the fixed event status register
 */
static u32 
acpi_read_pm1_status(struct acpi_facp *facp)
{
	u32 value = 0;
	if (facp->pm1a_evt)
		value = inw(facp->pm1a_evt);
	if (facp->pm1b_evt)
		value |= inw(facp->pm1b_evt);
	return value;
}

/*
 * Set the value of the fixed event status register (clear events)
 */
static void 
acpi_write_pm1_status(struct acpi_facp *facp, u32 value)
{
	if (facp->pm1a_evt)
		outw(value, facp->pm1a_evt);
	if (facp->pm1b_evt)
		outw(value, facp->pm1b_evt);
}

/*
 * Examine/modify value
 */
static int 
acpi_do_ulong(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	char str[2 * sizeof(unsigned long) + 4], *strend;
	unsigned long val;
	int size;

	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}

		val = *(unsigned long *) ctl->data;
		size = sprintf(str, "0x%08lx\n", val);
		if (*len >= size) {
			copy_to_user(buffer, str, size);
			*len = size;
		}
		else
			*len = 0;
	}
	else {
		size = sizeof(str) - 1;
		if (size > *len)
			size = *len;
		copy_from_user(str, buffer, size);
		str[size] = '\0';
		val = simple_strtoul(str, &strend, 0);
		if (strend == str)
			return -EINVAL;
		*(unsigned long *) ctl->data = val;
	}

	file->f_pos += *len;
	return 0;
}

/*
 * Handle ACPI event
 */
u32
acpi_event(void *context)
{
	unsigned long flags;
	int event = (int) context;
	int mask = 0;

	switch (event) {
	case ACPI_EVENT_POWER_BUTTON:
		mask = ACPI_PWRBTN;
		break;
	case ACPI_EVENT_SLEEP_BUTTON:
		mask = ACPI_SLPBTN;
		break;
	}

	if (mask) {
		// notify process waiting on /dev/acpi
		spin_lock_irqsave(&acpi_event_lock, flags);
		acpi_event_status |= mask;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		acpi_event_state = acpi_sleep_state;
		wake_up_interruptible(&acpi_event_wait);
	}

	return AE_OK;
}

/*
 * Wait for next event
 */
int
acpi_do_event(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	u32 event_status = 0;
	acpi_sstate_t event_state = 0;
	char str[27];
	int size;

	if (write)
		return -EPERM;
	if (*len < sizeof(str)) {
		*len = 0;
		return 0;
	}

	for (;;) {
		unsigned long flags;

		// we need an atomic exchange here
		spin_lock_irqsave(&acpi_event_lock, flags);
		event_status = acpi_event_status;
		acpi_event_status = 0;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		event_state = acpi_event_state;

		if (event_status)
			break;

		// wait for an event to arrive
		interruptible_sleep_on(&acpi_event_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	size = sprintf(str,
		       "0x%08x 0x%08x 0x%01x\n",
		       event_status,
		       0,
		       event_state);
	copy_to_user(buffer, str, size);
	*len = size;
	file->f_pos += size;

	return 0;
}

/*
 * Enter system sleep state
 */
static void
acpi_enter_sx(void *context)
{
	struct acpi_enter_sx_ctx *ctx = (struct acpi_enter_sx_ctx*) context;
	struct acpi_facp *facp = acpi_facp;
	ACPI_OBJECT_LIST arg_list;
	ACPI_OBJECT arg;
	u16 value;

	/*
         * _PSW methods could be run here to enable wake-on keyboard, LAN, etc.
	 */

	// run the _PTS method
	memset(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_NUMBER;
	arg.number.value = ctx->state;

	acpi_evaluate_object(NULL, "\\_PTS", &arg_list, NULL);
	
	// clear wake status
	acpi_write_pm1_status(facp, ACPI_WAK);
	
	acpi_sleep_state = ctx->state;

	// set ACPI_SLP_TYPA/b and ACPI_SLP_EN
	__cli();
	if (facp->pm1a_cnt) {
		value = inw(facp->pm1a_cnt) & ~ACPI_SLP_TYP_MASK;
		value |= (ACPI_SLP_TYPA(acpi_slptyp[ctx->state])
			  | ACPI_SLP_EN);
		outw(value, facp->pm1a_cnt);
	}
	if (facp->pm1b_cnt) {
		value = inw(facp->pm1b_cnt) & ~ACPI_SLP_TYP_MASK;
		value |= (ACPI_SLP_TYPB(acpi_slptyp[ctx->state])
			  | ACPI_SLP_EN);
		outw(value, facp->pm1b_cnt);
	}
	__sti();
	
	if (ctx->state != ACPI_S1) {
		printk(KERN_ERR "ACPI: S%d failed\n", ctx->state);
		goto out;
	}

	// wait until S1 is entered
	while (!(acpi_read_pm1_status(facp) & ACPI_WAK))
		safe_halt();

	// run the _WAK method
	memset(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_NUMBER;
	arg.number.value = ctx->state;

	acpi_evaluate_object(NULL, "\\_WAK", &arg_list, NULL);

 out:
	acpi_sleep_state = ACPI_S0;

	if (waitqueue_active(&ctx->wait))
		wake_up_interruptible(&ctx->wait);
}

/*
 * Enter system sleep state and wait for completion
 */
static int
acpi_enter_sx_and_wait(acpi_sstate_t state)
{
	struct acpi_enter_sx_ctx ctx;

	if (!acpi_facp
	    || acpi_facp->hdr.signature != ACPI_FACP_SIG
	    || acpi_slptyp[state] == ACPI_INVALID)
		return -EINVAL;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = state;

	if (acpi_os_queue_for_execution(0, acpi_enter_sx, &ctx))
		return -1;

	interruptible_sleep_on(&ctx.wait);
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

/*
 * Enter system sleep state
 */
static int 
acpi_do_sleep(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}
	}
	else {
#ifdef CONFIG_ACPI_S1_SLEEP
		int status = acpi_enter_sx_and_wait(ACPI_S1);
		if (status)
			return status;
#endif
	}
	file->f_pos += *len;
	return 0;
}

/*
 * Clear busmaster activity flag
 */
static inline void
acpi_clear_bm_activity(struct acpi_facp *facp)
{
	acpi_write_pm1_status(facp, ACPI_BM);
}

/*
 * Returns 1 if there has been busmaster activity
 */
static inline int
acpi_bm_activity(struct acpi_facp *facp)
{
	return acpi_read_pm1_status(facp) & ACPI_BM;
}

/*
 * Set system to sleep through busmaster requests
 */
static void
acpi_sleep_on_busmaster(struct acpi_facp *facp)
{
	u32 pm1_cntr = acpi_read_pm1_control(facp);
	if (pm1_cntr & ACPI_BM_RLD) {
		pm1_cntr &= ~ACPI_BM_RLD;
		acpi_write_pm1_control(facp, pm1_cntr);
	}
}

/*
 * Set system to wake on busmaster requests
 */
static void
acpi_wake_on_busmaster(struct acpi_facp *facp)
{
	u32 pm1_cntr = acpi_read_pm1_control(facp);
	if (!(pm1_cntr & ACPI_BM_RLD)) {
		pm1_cntr |= ACPI_BM_RLD;
		acpi_write_pm1_control(facp, pm1_cntr);
	}
	acpi_clear_bm_activity(facp);
}

/* The ACPI timer is just the low 24 bits */
#define TIME_BEGIN(tmr) inl(tmr)
#define TIME_END(tmr, begin) ((inl(tmr) - (begin)) & 0x00ffffff)

/*
 * Idle loop (uniprocessor only)
 */
void
acpi_idle(void)
{
	static int sleep_level = 1;
	struct acpi_facp *facp = acpi_facp;

	if (!facp
	    || facp->hdr.signature != ACPI_FACP_SIG
	    || !facp->pm_tmr
	    || !acpi_pblk)
		goto not_initialized;

	/*
	 * start from the previous sleep level..
	 */
	if (sleep_level == 1)
		goto sleep1;
	if (sleep_level == 2)
		goto sleep2;
      sleep3:
	sleep_level = 3;
	if (!acpi_c3_tested) {
		printk(KERN_DEBUG "ACPI C3 works\n");
		acpi_c3_tested = 1;
	}
	acpi_wake_on_busmaster(facp);
	if (facp->pm2_cnt)
		goto sleep3_with_arbiter;

	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		if (acpi_bm_activity(facp))
			goto sleep2;

		time = TIME_BEGIN(pm_tmr);
		inb(acpi_pblk + ACPI_P_LVL3);
		/* Dummy read, force synchronization with the PMU */
		inl(pm_tmr);
		time = TIME_END(pm_tmr, time);

		__sti();
		if (time < acpi_c3_exit_latency)
			goto sleep2;
	}

      sleep3_with_arbiter:
	for (;;) {
		unsigned long time;
		u8 arbiter;
		unsigned int pm2_cntr = facp->pm2_cnt;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		if (acpi_bm_activity(facp))
			goto sleep2;

		time = TIME_BEGIN(pm_tmr);
		arbiter = inb(pm2_cntr) & ~ACPI_ARB_DIS;
		/* Disable arbiter, park on CPU */
		outb(arbiter | ACPI_ARB_DIS, pm2_cntr);
		inb(acpi_pblk + ACPI_P_LVL3);
		/* Dummy read, force synchronization with the PMU */
		inl(pm_tmr);
		time = TIME_END(pm_tmr, time);
		/* Enable arbiter again.. */
		outb(arbiter, pm2_cntr);

		__sti();
		if (time < acpi_c3_exit_latency)
			goto sleep2;
	}

      sleep2:
	sleep_level = 2;
	if (!acpi_c2_tested) {
		printk(KERN_DEBUG "ACPI C2 works\n");
		acpi_c2_tested = 1;
	}
	acpi_wake_on_busmaster(facp);	/* Required to track BM activity.. */
	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;

		time = TIME_BEGIN(pm_tmr);
		inb(acpi_pblk + ACPI_P_LVL2);
		/* Dummy read, force synchronization with the PMU */
		inl(pm_tmr);
		time = TIME_END(pm_tmr, time);

		__sti();
		if (time < acpi_c2_exit_latency)
			goto sleep1;
		if (acpi_bm_activity(facp)) {
			acpi_clear_bm_activity(facp);
			continue;
		}
		if (time > acpi_c3_enter_latency)
			goto sleep3;
	}

      sleep1:
	sleep_level = 1;
	acpi_sleep_on_busmaster(facp);
	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		time = TIME_BEGIN(pm_tmr);
		safe_halt();
		time = TIME_END(pm_tmr, time);
		if (time > acpi_c2_enter_latency)
			goto sleep2;
	}

      not_initialized:
	for (;;) {
		__cli();
		if (current->need_resched)
			goto out;
		safe_halt();
	}

      out:
	__sti();
}

/*
 * Enter soft-off (S5)
 */
static void
acpi_power_off(void)
{
	struct acpi_enter_sx_ctx ctx;

	if (!acpi_facp
	    || acpi_facp->hdr.signature != ACPI_FACP_SIG
	    || acpi_slptyp[ACPI_S5] == ACPI_INVALID)
		return;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = ACPI_S5;
	acpi_enter_sx(&ctx);
}

/*
 * Get processor information
 */
static ACPI_STATUS
acpi_get_cpu_info(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_OBJECT obj;
	ACPI_CX_STATE lat[4];
	ACPI_CPU_THROTTLING_STATE throttle[ACPI_MAX_THROTTLE];
	ACPI_BUFFER buf;
	int i, count;

	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, NULL, NULL, &buf)))
		return AE_OK;

	printk(KERN_INFO "ACPI: PBLK %d @ 0x%04x:%d\n",
	       obj.processor.proc_id,
	       obj.processor.pblk_address,
	       obj.processor.pblk_length);
	if (acpi_pblk != ACPI_INVALID
	    || !obj.processor.pblk_address
	    || obj.processor.pblk_length != 6)
		return AE_OK;

	acpi_pblk = obj.processor.pblk_address;

	buf.length = sizeof(lat);
	buf.pointer = lat;
	if (!ACPI_SUCCESS(acpi_get_processor_cx_info(handle, &buf)))
		return AE_OK;

	if (lat[2].latency < MAX_CX_STATE_LATENCY) {
		printk(KERN_INFO "ACPI: C2 supported\n");
		acpi_c2_exit_latency = lat[2].latency;
	}
	if (lat[3].latency < MAX_CX_STATE_LATENCY) {
		printk(KERN_INFO "ACPI: C3 supported\n");
		acpi_c3_exit_latency = lat[3].latency;
	}

	memset(throttle, 0, sizeof(throttle));
	buf.length = sizeof(throttle);
	buf.pointer = throttle;

	if (!ACPI_SUCCESS(acpi_get_processor_throttling_info(handle, &buf)))
		return AE_OK;

	for (i = 0, count = 0; i < ACPI_MAX_THROTTLE; i++) {
		if (throttle[i].percent_of_clock)
			count++;
	}
	count--;
	if (count > 0)
		printk(KERN_INFO "ACPI: %d throttling states\n", count);

	return AE_OK;
}

/*
 * Fetch the FACP information
 */
static int
acpi_fetch_facp(void)
{
	ACPI_BUFFER buffer;

	buffer.pointer = acpi_facp;
	buffer.length = sizeof(*acpi_facp);
	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_FACP, 1, &buffer))) {
		printk(KERN_ERR "ACPI: no FACP\n");
		kfree(acpi_facp);
		return -ENODEV;
	}

	if (acpi_facp->p_lvl2_lat
	    && acpi_facp->p_lvl2_lat <= ACPI_MAX_P_LVL2_LAT) {
		acpi_c2_exit_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl2_lat);
		acpi_c2_enter_latency
			= ACPI_uS_TO_TMR_TICKS(ACPI_TMR_HZ / 1000);
	}
	if (acpi_facp->p_lvl3_lat
	    && acpi_facp->p_lvl3_lat <= ACPI_MAX_P_LVL3_LAT) {
		acpi_c3_exit_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl3_lat);
		acpi_c3_enter_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl3_lat * 5);
	}

	return 0;
}

/*
 * Execute callbacks (interpret methods)
 */
static void
acpi_intrp_run(void)
{
	for (;;) {
		struct acpi_intrp_entry *entry;
		unsigned long flags;

		interruptible_sleep_on(&acpi_intrp_wait);
		if (signal_pending(current))
			return;

		for (;;) {
			entry = NULL;

			spin_lock_irqsave(&acpi_intrp_exec_lock, flags);
			if (!list_empty(&acpi_intrp_exec)) {
				entry = list_entry(acpi_intrp_exec.next,
						   struct acpi_intrp_entry,
						   list);
				list_del(&entry->list);
			}
			spin_unlock_irqrestore(&acpi_intrp_exec_lock, flags);

			if (!entry)
				break;

			(*entry->callback)(entry->context);

			kfree(entry);
		}
	}
}

/*
 * Initialize and run interpreter within a kernel thread
 */
static int
acpi_intrp_thread(void *context)
{
	ACPI_STATUS status;
	u8 sx, typa, typb;

	/*
	 * initialize interpreter
	 */
	exit_files(current);
	daemonize();
	strcpy(current->comm, "acpi");

	if (!ACPI_SUCCESS(acpi_initialize(NULL))) {
		printk(KERN_ERR "ACPI: initialize failed\n");
		return -ENODEV;
	}

	if (!ACPI_SUCCESS(acpi_load_firmware_tables())) {
		printk(KERN_ERR "ACPI: table load failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	if (acpi_fetch_facp()) {
		acpi_terminate();
		return -ENODEV;
	}

	status = acpi_load_namespace();
	if (!ACPI_SUCCESS(status) && status != AE_CTRL_PENDING) {
		printk(KERN_ERR "ACPI: namespace load failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	printk(KERN_INFO "ACPI: ACPI support found\n");

	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
			    ACPI_ROOT_OBJECT,
			    ACPI_INT32_MAX,
			    acpi_get_cpu_info,
			    NULL,
			    NULL);

	for (sx = ACPI_S0; sx <= ACPI_S5; sx++) {
		int ca_sx = (sx <= ACPI_S4) ? sx : (sx + 1);
		if (ACPI_SUCCESS(
			   acpi_hw_obtain_sleep_type_register_data(ca_sx,
								   &typa,
								   &typb)))
			acpi_slptyp[sx] = ACPI_SLP_TYP(typa, typb);
		else
			acpi_slptyp[sx] = ACPI_INVALID;
	}
	if (acpi_slptyp[ACPI_S1] != ACPI_INVALID)
		printk(KERN_INFO "ACPI: S1 supported\n");
	if (acpi_slptyp[ACPI_S5] != ACPI_INVALID)
		printk(KERN_INFO "ACPI: S5 supported\n");

	if (!ACPI_SUCCESS(acpi_enable())) {
		printk(KERN_ERR "ACPI: enable failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
		ACPI_EVENT_POWER_BUTTON,
		acpi_event,
		(void *) ACPI_EVENT_POWER_BUTTON))) {
		printk(KERN_ERR "ACPI: power button enable failed\n");
	}
	if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
		ACPI_EVENT_SLEEP_BUTTON,
		acpi_event,
		(void *) ACPI_EVENT_SLEEP_BUTTON))) {
		printk(KERN_ERR "ACPI: sleep button enable failed\n");
	}

#ifdef CONFIG_SMP
	if (smp_num_cpus == 1)
		pm_idle = acpi_idle;
#else
	pm_idle = acpi_idle;
#endif
	pm_power_off = acpi_power_off;

	acpi_sysctl = register_sysctl_table(acpi_dir_table, 1);

	/*
	 * run interpreter
	 */
	acpi_intrp_run();

	/*
	 * terminate interpreter
	 */
	unregister_sysctl_table(acpi_sysctl);
	acpi_terminate();

	acpi_intrp_pid = -1;

	return 0;
}

/*
 * Start the interpreter
 */
int __init
acpi_init(void)
{
	acpi_facp = kmalloc(sizeof(*acpi_facp), GFP_KERNEL);
	if (!acpi_facp)
		return -ENOMEM;
	memset(acpi_facp, 0, sizeof(*acpi_facp));

	acpi_intrp_pid = kernel_thread(acpi_intrp_thread,
				       NULL,
				       (CLONE_FS | CLONE_FILES
					| CLONE_SIGHAND | SIGCHLD));
	return ((acpi_intrp_pid >= 0) ? 0:-ENODEV);
}

/*
 * Terminate the interpreter
 */
void __exit
acpi_exit(void)
{
	int count;

	if (!kill_proc(acpi_intrp_pid, SIGTERM, 1)) {
		// wait until interpreter thread terminates (at most 5 seconds)
		count = 5 * HZ;
		while (acpi_intrp_pid >= 0 && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	// clean up after the interpreter

	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_irq_irq,
						  acpi_irq_handler);
	}

	if (pm_power_off == acpi_power_off)
		pm_power_off = NULL;
	if (pm_idle == acpi_idle)
		pm_idle = NULL;

	kfree(acpi_facp);
}

module_init(acpi_init);
module_exit(acpi_exit);

/*
 * OS-dependent functions
 *
 */

char *
strupr(char *str)
{
	char *s = str;
	while (*s) {
		*s = TOUPPER(*s);
		s++;
	}
	return str;
}

ACPI_STATUS
acpi_os_initialize(void)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_terminate(void)
{
	return AE_OK;
}

s32
acpi_os_printf(const char *fmt,...)
{
	s32 size;
	va_list args;
	va_start(args, fmt);
	size = acpi_os_vprintf(fmt, args);
	va_end(args);
	return size;
}

s32
acpi_os_vprintf(const char *fmt, va_list args)
{
	static char buffer[512];
	int size = vsprintf(buffer, fmt, args);
	printk(KERN_DEBUG "%s", buffer);
	return size;
}

void *
acpi_os_allocate(u32 size)
{
	return kmalloc(size, GFP_KERNEL);
}

void *
acpi_os_callocate(u32 size)
{
	void *ptr = acpi_os_allocate(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void
acpi_os_free(void *ptr)
{
	kfree(ptr);
}

ACPI_STATUS
acpi_os_map_memory(void *phys, u32 size, void **virt)
{
	if ((unsigned long) phys < virt_to_phys(high_memory)) {
		*virt = phys_to_virt((unsigned long) phys);
		return AE_OK;
	}

	*virt = ioremap((unsigned long) phys, size);
	if (!*virt)
		return AE_ERROR;

	return AE_OK;
}

void
acpi_os_unmap_memory(void *virt, u32 size)
{
	if (virt >= high_memory)
		iounmap(virt);
}

static void
acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	(*acpi_irq_handler)(acpi_irq_context);
}

ACPI_STATUS
acpi_os_install_interrupt_handler(u32 irq, OSD_HANDLER handler, void *context)
{
	acpi_irq_irq = irq;
	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq,
			acpi_irq,
			SA_INTERRUPT | SA_SHIRQ,
			"acpi",
			acpi_irq)) {
		printk(KERN_ERR "ACPI: SCI (IRQ%d) allocation failed\n", irq);
		return AE_ERROR;
	}
	return AE_OK;
}

ACPI_STATUS
acpi_os_remove_interrupt_handler(u32 irq, OSD_HANDLER handler)
{
	if (acpi_irq_handler) {
		free_irq(irq, acpi_irq);
		acpi_irq_handler = NULL;
	}
	return AE_OK;
}

/*
 * Running in interpreter thread context, safe to sleep
 */

void
acpi_os_sleep(u32 sec, u32 ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ * sec + (ms * HZ) / 1000);
}

void
acpi_os_sleep_usec(u32 us)
{
	udelay(us);
}

u8
acpi_os_in8(ACPI_IO_ADDRESS port)
{
	return inb(port);
}

u16
acpi_os_in16(ACPI_IO_ADDRESS port)
{
	return inw(port);
}

u32
acpi_os_in32(ACPI_IO_ADDRESS port)
{
	return inl(port);
}

void
acpi_os_out8(ACPI_IO_ADDRESS port, u8 val)
{
	outb(val, port);
}

void
acpi_os_out16(ACPI_IO_ADDRESS port, u16 val)
{
	outw(val, port);
}

void
acpi_os_out32(ACPI_IO_ADDRESS port, u32 val)
{
	outl(val, port);
}

ACPI_STATUS
acpi_os_read_pci_cfg_byte(
				  u32 bus,
				  u32 func,
				  u32 addr,
				  u8 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_byte(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_read_pci_cfg_word(
				  u32 bus,
				  u32 func,
				  u32 addr,
				  u16 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_word(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_read_pci_cfg_dword(
				   u32 bus,
				   u32 func,
				   u32 addr,
				   u32 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_dword(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_byte(
				   u32 bus,
				   u32 func,
				   u32 addr,
				   u8 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_byte(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_word(
				   u32 bus,
				   u32 func,
				   u32 addr,
				   u16 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_word(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_dword(
				    u32 bus,
				    u32 func,
				    u32 addr,
				    u32 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_dword(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

/*
 * Queue for interpreter thread
 */

ACPI_STATUS
acpi_os_queue_for_execution(
				    u32 priority,
				    OSD_EXECUTION_CALLBACK callback,
				    void *context)
{
	struct acpi_intrp_entry *entry;
	unsigned long flags;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return AE_ERROR;

	memset(entry, 0, sizeof(entry));
	entry->priority = priority;
	entry->callback = callback;
	entry->context = context;
	INIT_LIST_HEAD(&entry->list);

	if (!waitqueue_active(&acpi_intrp_wait)) {
		kfree(entry);
		return AE_ERROR;
	}

	spin_lock_irqsave(&acpi_intrp_exec_lock, flags);
	list_add(&entry->list, &acpi_intrp_exec);
	wake_up(&acpi_intrp_wait);
	spin_unlock_irqrestore(&acpi_intrp_exec_lock, flags);

	return AE_OK;
}

/*
 * Semaphores are unused, interpreter access is single threaded
 */

ACPI_STATUS
acpi_os_create_semaphore(u32 max_units, u32 init, ACPI_HANDLE * handle)
{
	*handle = (ACPI_HANDLE) 0;
	return AE_OK;
}

ACPI_STATUS
acpi_os_delete_semaphore(ACPI_HANDLE handle)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_wait_semaphore(ACPI_HANDLE handle, u32 units, u32 timeout)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_signal_semaphore(ACPI_HANDLE handle, u32 units)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_breakpoint(char *msg)
{
	acpi_os_printf("breakpoint: %s", msg);
	return AE_OK;
}

void
acpi_os_dbg_trap(char *msg)
{
	acpi_os_printf("trap: %s", msg);
}

void
acpi_os_dbg_assert(void *failure, void *file, u32 line, char *msg)
{
	acpi_os_printf("assert: %s", msg);
}

u32
acpi_os_get_line(char *buffer)
{
	return 0;
}

/*
 * We just have to assume we're dealing with valid memory
 */

BOOLEAN
acpi_os_readable(void *ptr, u32 len)
{
	return 1;
}

BOOLEAN
acpi_os_writable(void *ptr, u32 len)
{
	return 1;
}
