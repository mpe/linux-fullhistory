/*
 *  sys.c - System management (suspend, ...)
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

#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define ACPI_SLP_TYP(typa, typb) (((int)(typa) << 8) | (int)(typb))
#define ACPI_SLP_TYPA(value) \
        ((((value) >> 8) << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK)
#define ACPI_SLP_TYPB(value) \
        ((((value) & 0xff) << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK)

struct acpi_enter_sx_ctx
{
	wait_queue_head_t wait;
	int state;
};

volatile acpi_sstate_t acpi_sleep_state = ACPI_S0;
static unsigned long acpi_slptyp[ACPI_S5 + 1] = {ACPI_INVALID,};

/*
 * Enter system sleep state
 */
static void
acpi_enter_sx_async(void *context)
{
	struct acpi_enter_sx_ctx *ctx = (struct acpi_enter_sx_ctx*) context;
	struct acpi_facp *facp = &acpi_facp;
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
 * Enter soft-off (S5)
 */
static void
acpi_power_off(void)
{
	struct acpi_enter_sx_ctx ctx;

	if (acpi_facp.hdr.signature != ACPI_FACP_SIG
	    || acpi_slptyp[ACPI_S5] == ACPI_INVALID)
		return;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = ACPI_S5;
	acpi_enter_sx_async(&ctx);
}

/*
 * Enter system sleep state and wait for completion
 */
int
acpi_enter_sx(acpi_sstate_t state)
{
	struct acpi_enter_sx_ctx ctx;

	if (acpi_facp.hdr.signature != ACPI_FACP_SIG
	    || acpi_slptyp[state] == ACPI_INVALID)
		return -EINVAL;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = state;

	if (acpi_os_queue_for_execution(0, acpi_enter_sx_async, &ctx))
		return -1;

	interruptible_sleep_on(&ctx.wait);
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

int
acpi_sys_init(void)
{
	u8 sx, typa, typb;

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

	pm_power_off = acpi_power_off;

	return 0;
}
