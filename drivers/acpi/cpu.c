/*
 *  cpu.c - Processor handling
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
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("cpu")

unsigned long acpi_c2_exit_latency = ACPI_INFINITE;
unsigned long acpi_c3_exit_latency = ACPI_INFINITE;
unsigned long acpi_c2_enter_latency = ACPI_INFINITE;
unsigned long acpi_c3_enter_latency = ACPI_INFINITE;

static unsigned long acpi_pblk = ACPI_INVALID;
static int acpi_c2_tested = 0;
static int acpi_c3_tested = 0;
static int acpi_max_c_state = 1;

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
static void
acpi_idle(void)
{
	static int sleep_level = 1;
	struct acpi_facp *facp = &acpi_facp;

	if (!facp
	    || facp->hdr.signature != ACPI_FACP_SIG
	    || !facp->pm_tmr
	    || !acpi_pblk)
		goto not_initialized;

	/*
	 * start from the previous sleep level..
	 */
	if (sleep_level == 1
	    || acpi_max_c_state < 2)
		goto sleep1;

	if (sleep_level == 2
	    || acpi_max_c_state < 3)
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
		if (time > acpi_c3_enter_latency
		    && acpi_max_c_state >= 3)
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
		if (time > acpi_c2_enter_latency
		    && acpi_max_c_state >= 2)
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
 * Get processor information
 */
static ACPI_STATUS
acpi_find_cpu(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
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
		printk(KERN_INFO "ACPI: C2");
		acpi_c2_exit_latency = lat[2].latency;
		acpi_max_c_state = 2;
	
		if (lat[3].latency < MAX_CX_STATE_LATENCY) {
			printk(", C3 supported\n");
			acpi_c3_exit_latency = lat[3].latency;
			acpi_max_c_state = 3;
		}
		else {
			printk(" supported\n");
		}
			
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

int
acpi_cpu_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    acpi_find_cpu,
			    NULL,
			    NULL);

#ifdef CONFIG_SMP
	if (smp_num_cpus == 1)
		pm_idle = acpi_idle;
#else
	pm_idle = acpi_idle;
#endif

	return 0;
}
