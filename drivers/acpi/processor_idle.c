/*
 * processor_idle - idle state submodule to the ACPI processor driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT        0x01000000
#define ACPI_PROCESSOR_CLASS            "processor"
#define ACPI_PROCESSOR_DRIVER_NAME      "ACPI Processor Driver"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME                ("acpi_processor")

#define ACPI_PROCESSOR_FILE_POWER	"power"

#define US_TO_PM_TIMER_TICKS(t)		((t * (PM_TIMER_FREQUENCY/1000)) / 1000)
#define C2_OVERHEAD			4	/* 1us (3.579 ticks per us) */
#define C3_OVERHEAD			4	/* 1us (3.579 ticks per us) */

static void (*pm_idle_save)(void);
module_param(max_cstate, uint, 0644);

static unsigned int nocst = 0;
module_param(nocst, uint, 0000);

/*
 * bm_history -- bit-mask with a bit per jiffy of bus-master activity
 * 1000 HZ: 0xFFFFFFFF: 32 jiffies = 32ms
 * 800 HZ: 0xFFFFFFFF: 32 jiffies = 40ms
 * 100 HZ: 0x0000000F: 4 jiffies = 40ms
 * reduce history for more aggressive entry into C3
 */
static unsigned int bm_history = (HZ >= 800 ? 0xFFFFFFFF : ((1U << (HZ / 25)) - 1));
module_param(bm_history, uint, 0644);
/* --------------------------------------------------------------------------
                                Power Management
   -------------------------------------------------------------------------- */

/*
 * IBM ThinkPad R40e crashes mysteriously when going into C2 or C3.
 * For now disable this. Probably a bug somewhere else.
 *
 * To skip this limit, boot/load with a large max_cstate limit.
 */
static int no_c2c3(struct dmi_system_id *id)
{
	if (max_cstate > ACPI_PROCESSOR_MAX_POWER)
		return 0;

	printk(KERN_NOTICE PREFIX "%s detected - C2,C3 disabled."
		" Override with \"processor.max_cstate=%d\"\n", id->ident,
	       ACPI_PROCESSOR_MAX_POWER + 1);

	max_cstate = 1;

	return 0;
}




static struct dmi_system_id __initdata processor_power_dmi_table[] = {
	{ no_c2c3, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET60WW") }},
	{ no_c2c3, "Medion 41700", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
	  DMI_MATCH(DMI_BIOS_VERSION,"R01-A1J") }},
	{},
};


static inline u32
ticks_elapsed (
	u32			t1,
	u32			t2)
{
	if (t2 >= t1)
		return (t2 - t1);
	else if (!acpi_fadt.tmr_val_ext)
		return (((0x00FFFFFF - t1) + t2) & 0x00FFFFFF);
	else
		return ((0xFFFFFFFF - t1) + t2);
}


static void
acpi_processor_power_activate (
	struct acpi_processor	*pr,
	struct acpi_processor_cx  *new)
{
	struct acpi_processor_cx  *old;

	if (!pr || !new)
		return;

	old = pr->power.state;

	if (old)
		old->promotion.count = 0;
 	new->demotion.count = 0;

	/* Cleanup from old state. */
	if (old) {
		switch (old->type) {
		case ACPI_STATE_C3:
			/* Disable bus master reload */
			if (new->type != ACPI_STATE_C3)
				acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 0, ACPI_MTX_DO_NOT_LOCK);
			break;
		}
	}

	/* Prepare to use new state. */
	switch (new->type) {
	case ACPI_STATE_C3:
		/* Enable bus master reload */
		if (old->type != ACPI_STATE_C3)
			acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 1, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	pr->power.state = new;

	return;
}


static void acpi_processor_idle (void)
{
	struct acpi_processor	*pr = NULL;
	struct acpi_processor_cx *cx = NULL;
	struct acpi_processor_cx *next_state = NULL;
	int			sleep_ticks = 0;
	u32			t1, t2 = 0;

	pr = processors[_smp_processor_id()];
	if (!pr)
		return;

	/*
	 * Interrupts must be disabled during bus mastering calculations and
	 * for C2/C3 transitions.
	 */
	local_irq_disable();

	/*
	 * Check whether we truly need to go idle, or should
	 * reschedule:
	 */
	if (unlikely(need_resched())) {
		local_irq_enable();
		return;
	}

	cx = pr->power.state;
	if (!cx)
		goto easy_out;

	/*
	 * Check BM Activity
	 * -----------------
	 * Check for bus mastering activity (if required), record, and check
	 * for demotion.
	 */
	if (pr->flags.bm_check) {
		u32		bm_status = 0;
		unsigned long	diff = jiffies - pr->power.bm_check_timestamp;

		if (diff > 32)
			diff = 32;

		while (diff) {
			/* if we didn't get called, assume there was busmaster activity */
			diff--;
			if (diff)
				pr->power.bm_activity |= 0x1;
			pr->power.bm_activity <<= 1;
		}

		acpi_get_register(ACPI_BITREG_BUS_MASTER_STATUS,
			&bm_status, ACPI_MTX_DO_NOT_LOCK);
		if (bm_status) {
			pr->power.bm_activity++;
			acpi_set_register(ACPI_BITREG_BUS_MASTER_STATUS,
				1, ACPI_MTX_DO_NOT_LOCK);
		}
		/*
		 * PIIX4 Erratum #18: Note that BM_STS doesn't always reflect
		 * the true state of bus mastering activity; forcing us to
		 * manually check the BMIDEA bit of each IDE channel.
		 */
		else if (errata.piix4.bmisx) {
			if ((inb_p(errata.piix4.bmisx + 0x02) & 0x01)
				|| (inb_p(errata.piix4.bmisx + 0x0A) & 0x01))
				pr->power.bm_activity++;
		}

		pr->power.bm_check_timestamp = jiffies;

		/*
		 * Apply bus mastering demotion policy.  Automatically demote
		 * to avoid a faulty transition.  Note that the processor
		 * won't enter a low-power state during this call (to this
		 * funciton) but should upon the next.
		 *
		 * TBD: A better policy might be to fallback to the demotion
		 *      state (use it for this quantum only) istead of
		 *      demoting -- and rely on duration as our sole demotion
		 *      qualification.  This may, however, introduce DMA
		 *      issues (e.g. floppy DMA transfer overrun/underrun).
		 */
		if (pr->power.bm_activity & cx->demotion.threshold.bm) {
			local_irq_enable();
			next_state = cx->demotion.state;
			goto end;
		}
	}

	cx->usage++;

	/*
	 * Sleep:
	 * ------
	 * Invoke the current Cx state to put the processor to sleep.
	 */
	switch (cx->type) {

	case ACPI_STATE_C1:
		/*
		 * Invoke C1.
		 * Use the appropriate idle routine, the one that would
		 * be used without acpi C-states.
		 */
		if (pm_idle_save)
			pm_idle_save();
		else
			safe_halt();
		/*
                 * TBD: Can't get time duration while in C1, as resumes
		 *      go to an ISR rather than here.  Need to instrument
		 *      base interrupt handler.
		 */
		sleep_ticks = 0xFFFFFFFF;
		break;

	case ACPI_STATE_C2:
		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C2 */
		inb(cx->address);
		/* Dummy op - must do something useless after P_LVL2 read */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Re-enable interrupts */
		local_irq_enable();
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks = ticks_elapsed(t1, t2) - cx->latency_ticks - C2_OVERHEAD;
		break;

	case ACPI_STATE_C3:
		/* Disable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 1, ACPI_MTX_DO_NOT_LOCK);
		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C3 */
		inb(cx->address);
		/* Dummy op - must do something useless after P_LVL3 read */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Enable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 0, ACPI_MTX_DO_NOT_LOCK);
		/* Re-enable interrupts */
		local_irq_enable();
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks = ticks_elapsed(t1, t2) - cx->latency_ticks - C3_OVERHEAD;
		break;

	default:
		local_irq_enable();
		return;
	}

	next_state = pr->power.state;

	/*
	 * Promotion?
	 * ----------
	 * Track the number of longs (time asleep is greater than threshold)
	 * and promote when the count threshold is reached.  Note that bus
	 * mastering activity may prevent promotions.
	 * Do not promote above max_cstate.
	 */
	if (cx->promotion.state &&
	    ((cx->promotion.state - pr->power.states) <= max_cstate)) {
		if (sleep_ticks > cx->promotion.threshold.ticks) {
			cx->promotion.count++;
 			cx->demotion.count = 0;
			if (cx->promotion.count >= cx->promotion.threshold.count) {
				if (pr->flags.bm_check) {
					if (!(pr->power.bm_activity & cx->promotion.threshold.bm)) {
						next_state = cx->promotion.state;
						goto end;
					}
				}
				else {
					next_state = cx->promotion.state;
					goto end;
				}
			}
		}
	}

	/*
	 * Demotion?
	 * ---------
	 * Track the number of shorts (time asleep is less than time threshold)
	 * and demote when the usage threshold is reached.
	 */
	if (cx->demotion.state) {
		if (sleep_ticks < cx->demotion.threshold.ticks) {
			cx->demotion.count++;
			cx->promotion.count = 0;
			if (cx->demotion.count >= cx->demotion.threshold.count) {
				next_state = cx->demotion.state;
				goto end;
			}
		}
	}

end:
	/*
	 * Demote if current state exceeds max_cstate
	 */
	if ((pr->power.state - pr->power.states) > max_cstate) {
		if (cx->demotion.state)
			next_state = cx->demotion.state;
	}

	/*
	 * New Cx State?
	 * -------------
	 * If we're going to start using a new Cx state we must clean up
	 * from the previous and prepare to use the new.
	 */
	if (next_state != pr->power.state)
		acpi_processor_power_activate(pr, next_state);

	return;

 easy_out:
	/* do C1 instead of busy loop */
	if (pm_idle_save)
		pm_idle_save();
	else
		safe_halt();
	return;
}


static int
acpi_processor_set_power_policy (
	struct acpi_processor	*pr)
{
	unsigned int i;
	unsigned int state_is_set = 0;
	struct acpi_processor_cx *lower = NULL;
	struct acpi_processor_cx *higher = NULL;
	struct acpi_processor_cx *cx;

 	ACPI_FUNCTION_TRACE("acpi_processor_set_power_policy");

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * This function sets the default Cx state policy (OS idle handler).
	 * Our scheme is to promote quickly to C2 but more conservatively
	 * to C3.  We're favoring C2  for its characteristics of low latency
	 * (quick response), good power savings, and ability to allow bus
	 * mastering activity.  Note that the Cx state policy is completely
	 * customizable and can be altered dynamically.
	 */

	/* startup state */
	for (i=1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (!state_is_set)
			pr->power.state = cx;
		state_is_set++;
		break;
 	}

	if (!state_is_set)
		return_VALUE(-ENODEV);

	/* demotion */
	for (i=1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (lower) {
			cx->demotion.state = lower;
			cx->demotion.threshold.ticks = cx->latency_ticks;
			cx->demotion.threshold.count = 1;
			if (cx->type == ACPI_STATE_C3)
				cx->demotion.threshold.bm = bm_history;
		}

		lower = cx;
	}

	/* promotion */
	for (i = (ACPI_PROCESSOR_MAX_POWER - 1); i > 0; i--) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (higher) {
			cx->promotion.state  = higher;
			cx->promotion.threshold.ticks = cx->latency_ticks;
			if (cx->type >= ACPI_STATE_C2)
				cx->promotion.threshold.count = 4;
			else
				cx->promotion.threshold.count = 10;
			if (higher->type == ACPI_STATE_C3)
				cx->promotion.threshold.bm = bm_history;
		}

		higher = cx;
	}

 	return_VALUE(0);
}


static int acpi_processor_get_power_info_fadt (struct acpi_processor *pr)
{
	int i;

	ACPI_FUNCTION_TRACE("acpi_processor_get_power_info_fadt");

	if (!pr)
		return_VALUE(-EINVAL);

	if (!pr->pblk)
		return_VALUE(-ENODEV);

	for (i = 0; i < ACPI_PROCESSOR_MAX_POWER; i++)
		memset(pr->power.states, 0, sizeof(struct acpi_processor_cx));

	/* if info is obtained from pblk/fadt, type equals state */
	pr->power.states[ACPI_STATE_C1].type = ACPI_STATE_C1;
	pr->power.states[ACPI_STATE_C2].type = ACPI_STATE_C2;
	pr->power.states[ACPI_STATE_C3].type = ACPI_STATE_C3;

	/* the C0 state only exists as a filler in our array,
	 * and all processors need to support C1 */
	pr->power.states[ACPI_STATE_C0].valid = 1;
	pr->power.states[ACPI_STATE_C1].valid = 1;

	/* determine C2 and C3 address from pblk */
	pr->power.states[ACPI_STATE_C2].address = pr->pblk + 4;
	pr->power.states[ACPI_STATE_C3].address = pr->pblk + 5;

	/* determine latencies from FADT */
	pr->power.states[ACPI_STATE_C2].latency = acpi_fadt.plvl2_lat;
	pr->power.states[ACPI_STATE_C3].latency = acpi_fadt.plvl3_lat;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "lvl2[0x%08x] lvl3[0x%08x]\n",
			  pr->power.states[ACPI_STATE_C2].address,
			  pr->power.states[ACPI_STATE_C3].address));

	return_VALUE(0);
}


static int acpi_processor_get_power_info_cst (struct acpi_processor *pr)
{
	acpi_status		status = 0;
	acpi_integer		count;
	int			i;
	struct acpi_buffer	buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object	*cst;

	ACPI_FUNCTION_TRACE("acpi_processor_get_power_info_cst");

	if (errata.smp)
		return_VALUE(-ENODEV);

	if (nocst)
		return_VALUE(-ENODEV);

	pr->power.count = 0;
	for (i = 0; i < ACPI_PROCESSOR_MAX_POWER; i++)
		memset(pr->power.states, 0, sizeof(struct acpi_processor_cx));

	status = acpi_evaluate_object(pr->handle, "_CST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No _CST, giving up\n"));
		return_VALUE(-ENODEV);
 	}

	cst = (union acpi_object *) buffer.pointer;

	/* There must be at least 2 elements */
	if (!cst || (cst->type != ACPI_TYPE_PACKAGE) || cst->package.count < 2) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "not enough elements in _CST\n"));
		status = -EFAULT;
		goto end;
	}

	count = cst->package.elements[0].integer.value;

	/* Validate number of power states. */
	if (count < 1 || count != cst->package.count - 1) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "count given by _CST is not valid\n"));
		status = -EFAULT;
		goto end;
	}

	/* We support up to ACPI_PROCESSOR_MAX_POWER. */
	if (count > ACPI_PROCESSOR_MAX_POWER) {
		printk(KERN_WARNING "Limiting number of power states to max (%d)\n", ACPI_PROCESSOR_MAX_POWER);
		printk(KERN_WARNING "Please increase ACPI_PROCESSOR_MAX_POWER if needed.\n");
		count = ACPI_PROCESSOR_MAX_POWER;
	}

	/* Tell driver that at least _CST is supported. */
	pr->flags.has_cst = 1;

	for (i = 1; i <= count; i++) {
		union acpi_object *element;
		union acpi_object *obj;
		struct acpi_power_register *reg;
		struct acpi_processor_cx cx;

		memset(&cx, 0, sizeof(cx));

		element = (union acpi_object *) &(cst->package.elements[i]);
		if (element->type != ACPI_TYPE_PACKAGE)
			continue;

		if (element->package.count != 4)
			continue;

		obj = (union acpi_object *) &(element->package.elements[0]);

		if (obj->type != ACPI_TYPE_BUFFER)
			continue;

		reg = (struct acpi_power_register *) obj->buffer.pointer;

		if (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO &&
			(reg->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE))
			continue;

		cx.address = (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE) ?
			0 : reg->address;

		/* There should be an easy way to extract an integer... */
		obj = (union acpi_object *) &(element->package.elements[1]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.type = obj->integer.value;

		if ((cx.type != ACPI_STATE_C1) &&
		    (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO))
			continue;

		if ((cx.type < ACPI_STATE_C1) ||
		    (cx.type > ACPI_STATE_C3))
			continue;

		obj = (union acpi_object *) &(element->package.elements[2]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.latency = obj->integer.value;

		obj = (union acpi_object *) &(element->package.elements[3]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.power = obj->integer.value;

		(pr->power.count)++;
		memcpy(&(pr->power.states[pr->power.count]), &cx, sizeof(cx));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d power states\n", pr->power.count));

	/* Validate number of power states discovered */
	if (pr->power.count < 2)
		status = -ENODEV;

end:
	acpi_os_free(buffer.pointer);

	return_VALUE(status);
}


static void acpi_processor_power_verify_c2(struct acpi_processor_cx *cx)
{
	ACPI_FUNCTION_TRACE("acpi_processor_get_power_verify_c2");

	if (!cx->address)
		return_VOID;

	/*
	 * C2 latency must be less than or equal to 100
	 * microseconds.
	 */
	else if (cx->latency > ACPI_PROCESSOR_MAX_C2_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "latency too large [%d]\n",
				  cx->latency));
		return_VOID;
	}

	/* We're (currently) only supporting C2 on UP */
	else if (errata.smp) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "C2 not supported in SMP mode\n"));
		return_VOID;
	}

	/*
	 * Otherwise we've met all of our C2 requirements.
	 * Normalize the C2 latency to expidite policy
	 */
	cx->valid = 1;
	cx->latency_ticks = US_TO_PM_TIMER_TICKS(cx->latency);

	return_VOID;
}


static void acpi_processor_power_verify_c3(
	struct acpi_processor *pr,
	struct acpi_processor_cx *cx)
{
	ACPI_FUNCTION_TRACE("acpi_processor_get_power_verify_c3");

	if (!cx->address)
		return_VOID;

	/*
	 * C3 latency must be less than or equal to 1000
	 * microseconds.
	 */
	else if (cx->latency > ACPI_PROCESSOR_MAX_C3_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "latency too large [%d]\n",
				  cx->latency));
		return_VOID;
	}

	/* bus mastering control is necessary */
	else if (!pr->flags.bm_control) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "C3 support requires bus mastering control\n"));
		return_VOID;
	}

	/* We're (currently) only supporting C2 on UP */
	else if (errata.smp) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "C3 not supported in SMP mode\n"));
		return_VOID;
	}

	/*
	 * PIIX4 Erratum #18: We don't support C3 when Type-F (fast)
	 * DMA transfers are used by any ISA device to avoid livelock.
	 * Note that we could disable Type-F DMA (as recommended by
	 * the erratum), but this is known to disrupt certain ISA
	 * devices thus we take the conservative approach.
	 */
	else if (errata.piix4.fdma) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"C3 not supported on PIIX4 with Type-F DMA\n"));
		return_VOID;
	}

	/*
	 * Otherwise we've met all of our C3 requirements.
	 * Normalize the C3 latency to expidite policy.  Enable
	 * checking of bus mastering status (bm_check) so we can
	 * use this in our C3 policy
	 */
	cx->valid = 1;
	cx->latency_ticks = US_TO_PM_TIMER_TICKS(cx->latency);
	pr->flags.bm_check = 1;

	return_VOID;
}


static int acpi_processor_power_verify(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int working = 0;

	for (i=1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		struct acpi_processor_cx *cx = &pr->power.states[i];

		switch (cx->type) {
		case ACPI_STATE_C1:
			cx->valid = 1;
			break;

		case ACPI_STATE_C2:
			acpi_processor_power_verify_c2(cx);
			break;

		case ACPI_STATE_C3:
			acpi_processor_power_verify_c3(pr, cx);
			break;
		}

		if (cx->valid)
			working++;
	}

	return (working);
}

static int acpi_processor_get_power_info (
	struct acpi_processor	*pr)
{
	unsigned int i;
	int result;

	ACPI_FUNCTION_TRACE("acpi_processor_get_power_info");

	/* NOTE: the idle thread may not be running while calling
	 * this function */

	result = acpi_processor_get_power_info_cst(pr);
	if ((result) || (acpi_processor_power_verify(pr) < 2)) {
		result = acpi_processor_get_power_info_fadt(pr);
		if (result)
			return_VALUE(result);

		if (acpi_processor_power_verify(pr) < 2)
			return_VALUE(-ENODEV);
	}

	/*
	 * Set Default Policy
	 * ------------------
	 * Now that we know which states are supported, set the default
	 * policy.  Note that this policy can be changed dynamically
	 * (e.g. encourage deeper sleeps to conserve battery life when
	 * not on AC).
	 */
	result = acpi_processor_set_power_policy(pr);
	if (result)
		return_VALUE(result);

	/*
	 * if one state of type C2 or C3 is available, mark this
	 * CPU as being "idle manageable"
	 */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		if (pr->power.states[i].valid)
			pr->power.count = i;
		if ((pr->power.states[i].valid) &&
		    (pr->power.states[i].type >= ACPI_STATE_C2))
			pr->flags.power = 1;
	}

	return_VALUE(0);
}

int acpi_processor_cst_has_changed (struct acpi_processor *pr)
{
 	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_cst_has_changed");

	if (!pr)
 		return_VALUE(-EINVAL);

	if (errata.smp || nocst) {
		return_VALUE(-ENODEV);
	}

	if (!pr->flags.power_setup_done)
		return_VALUE(-ENODEV);

	/* Fall back to the default idle loop */
	pm_idle = pm_idle_save;
	synchronize_kernel();

	pr->flags.power = 0;
	result = acpi_processor_get_power_info(pr);
	if ((pr->flags.power == 1) && (pr->flags.power_setup_done))
		pm_idle = acpi_processor_idle;

	return_VALUE(result);
}

/* proc interface */

static int acpi_processor_power_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;
	unsigned int		i;

	ACPI_FUNCTION_TRACE("acpi_processor_power_seq_show");

	if (!pr)
		goto end;

	seq_printf(seq, "active state:            C%zd\n"
			"max_cstate:              C%d\n"
			"bus master activity:     %08x\n",
			pr->power.state ? pr->power.state - pr->power.states : 0,
			max_cstate,
			(unsigned)pr->power.bm_activity);

	seq_puts(seq, "states:\n");

	for (i = 1; i <= pr->power.count; i++) {
		seq_printf(seq, "   %cC%d:                  ",
			(&pr->power.states[i] == pr->power.state?'*':' '), i);

		if (!pr->power.states[i].valid) {
			seq_puts(seq, "<not supported>\n");
			continue;
		}

		switch (pr->power.states[i].type) {
		case ACPI_STATE_C1:
			seq_printf(seq, "type[C1] ");
			break;
		case ACPI_STATE_C2:
			seq_printf(seq, "type[C2] ");
			break;
		case ACPI_STATE_C3:
			seq_printf(seq, "type[C3] ");
			break;
		default:
			seq_printf(seq, "type[--] ");
			break;
		}

		if (pr->power.states[i].promotion.state)
			seq_printf(seq, "promotion[C%zd] ",
				(pr->power.states[i].promotion.state -
				 pr->power.states));
		else
			seq_puts(seq, "promotion[--] ");

		if (pr->power.states[i].demotion.state)
			seq_printf(seq, "demotion[C%zd] ",
				(pr->power.states[i].demotion.state -
				 pr->power.states));
		else
			seq_puts(seq, "demotion[--] ");

		seq_printf(seq, "latency[%03d] usage[%08d]\n",
			pr->power.states[i].latency,
			pr->power.states[i].usage);
	}

end:
	return_VALUE(0);
}

static int acpi_processor_power_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_power_seq_show,
						PDE(inode)->data);
}

static struct file_operations acpi_processor_power_fops = {
	.open 		= acpi_processor_power_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


int acpi_processor_power_init(struct acpi_processor *pr, struct acpi_device *device)
{
	acpi_status		status = 0;
	static int		first_run = 0;
	struct proc_dir_entry	*entry = NULL;
	unsigned int i;

	ACPI_FUNCTION_TRACE("acpi_processor_power_init");

	if (!first_run) {
		dmi_check_system(processor_power_dmi_table);
		if (max_cstate < ACPI_C_STATES_MAX)
			printk(KERN_NOTICE "ACPI: processor limited to max C-state %d\n", max_cstate);
		first_run++;
	}

	if (!errata.smp && (pr->id == 0) && acpi_fadt.cst_cnt && !nocst) {
		status = acpi_os_write_port(acpi_fadt.smi_cmd, acpi_fadt.cst_cnt, 8);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Notifying BIOS of _CST ability failed\n"));
		}
	}

	acpi_processor_get_power_info(pr);

	/*
	 * Install the idle handler if processor power management is supported.
	 * Note that we use previously set idle handler will be used on
	 * platforms that only support C1.
	 */
	if ((pr->flags.power) && (!boot_option_idle_override)) {
		printk(KERN_INFO PREFIX "CPU%d (power states:", pr->id);
		for (i = 1; i <= pr->power.count; i++)
			if (pr->power.states[i].valid)
				printk(" C%d[C%d]", i, pr->power.states[i].type);
		printk(")\n");

		if (pr->id == 0) {
			pm_idle_save = pm_idle;
			pm_idle = acpi_processor_idle;
		}
	}

	/* 'power' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_POWER,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_POWER));
	else {
		entry->proc_fops = &acpi_processor_power_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	pr->flags.power_setup_done = 1;

	return_VALUE(0);
}

int acpi_processor_power_exit(struct acpi_processor *pr, struct acpi_device *device)
{
	ACPI_FUNCTION_TRACE("acpi_processor_power_exit");

	pr->flags.power_setup_done = 0;

	if (acpi_device_dir(device))
		remove_proc_entry(ACPI_PROCESSOR_FILE_POWER,acpi_device_dir(device));

	/* Unregister the idle handler when processor #0 is removed. */
	if (pr->id == 0) {
		pm_idle = pm_idle_save;

		/*
		 * We are about to unload the current idle thread pm callback
		 * (pm_idle), Wait for all processors to update cached/local
		 * copies of pm_idle before proceeding.
		 */
		cpu_idle_wait();
	}

	return_VALUE(0);
}
