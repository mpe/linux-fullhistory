
/******************************************************************************
 *
 * Name: hwtimer.c - ACPI Power Management Timer Interface
 *              $Revision: 19 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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

#include "acpi.h"
#include "achware.h"

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwtimer")


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_timer_resolution
 *
 * PARAMETERS:  none
 *
 * RETURN:      Number of bits of resolution in the PM Timer (24 or 32).
 *
 * DESCRIPTION: Obtains resolution of the ACPI PM Timer.
 *
 ******************************************************************************/

acpi_status
acpi_get_timer_resolution (
	u32                     *resolution)
{
	ACPI_FUNCTION_TRACE ("Acpi_get_timer_resolution");


	if (!resolution) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (0 == acpi_gbl_FADT->tmr_val_ext) {
		*resolution = 24;
	}
	else {
		*resolution = 32;
	}

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_timer
 *
 * PARAMETERS:  none
 *
 * RETURN:      Current value of the ACPI PM Timer (in ticks).
 *
 * DESCRIPTION: Obtains current value of ACPI PM Timer.
 *
 ******************************************************************************/

acpi_status
acpi_get_timer (
	u32                     *ticks)
{
	ACPI_FUNCTION_TRACE ("Acpi_get_timer");


	if (!ticks) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	acpi_os_read_port ((ACPI_IO_ADDRESS)
		ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm_tmr_blk.address), ticks, 32);

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_timer_duration
 *
 * PARAMETERS:  Start_ticks
 *              End_ticks
 *              Time_elapsed
 *
 * RETURN:      Time_elapsed
 *
 * DESCRIPTION: Computes the time elapsed (in microseconds) between two
 *              PM Timer time stamps, taking into account the possibility of
 *              rollovers, the timer resolution, and timer frequency.
 *
 *              The PM Timer's clock ticks at roughly 3.6 times per
 *              _microsecond_, and its clock continues through Cx state
 *              transitions (unlike many CPU timestamp counters) -- making it
 *              a versatile and accurate timer.
 *
 *              Note that this function accomodates only a single timer
 *              rollover.  Thus for 24-bit timers, this function should only
 *              be used for calculating durations less than ~4.6 seconds
 *              (~20 hours for 32-bit timers).
 *
 ******************************************************************************/

acpi_status
acpi_get_timer_duration (
	u32                     start_ticks,
	u32                     end_ticks,
	u32                     *time_elapsed)
{
	u32                     delta_ticks = 0;
	uint64_overlay          normalized_ticks;
	acpi_status             status;
	acpi_integer            out_quotient;


	ACPI_FUNCTION_TRACE ("Acpi_get_timer_duration");


	if (!time_elapsed) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Compute Tick Delta:
	 * -------------------
	 * Handle (max one) timer rollovers on 24- versus 32-bit timers.
	 */
	if (start_ticks < end_ticks) {
		delta_ticks = end_ticks - start_ticks;
	}
	else if (start_ticks > end_ticks) {
		if (0 == acpi_gbl_FADT->tmr_val_ext) {
			/* 24-bit Timer */

			delta_ticks = (((0x00FFFFFF - start_ticks) + end_ticks) & 0x00FFFFFF);
		}
		else {
			/* 32-bit Timer */

			delta_ticks = (0xFFFFFFFF - start_ticks) + end_ticks;
		}
	}
	else {
		*time_elapsed = 0;
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Compute Duration:
	 * -----------------
	 *
	 * Requires a 64-bit divide:
	 *
	 * Time_elapsed = (Delta_ticks * 1000000) / PM_TIMER_FREQUENCY;
	 */
	normalized_ticks.full = ((u64) delta_ticks) * 1000000;

	status = acpi_ut_short_divide (&normalized_ticks.full, PM_TIMER_FREQUENCY,
			   &out_quotient, NULL);

	*time_elapsed = (u32) out_quotient;
	return_ACPI_STATUS (status);
}


