
/******************************************************************************
 *
 * Module Name: cminit - Common ACPI subsystem initialization
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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
#include "hardware.h"
#include "namesp.h"
#include "events.h"
#include "parser.h"
#include "dispatch.h"

#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cminit");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_cm_facp_register_error
 *
 * PARAMETERS:  *Register_name          - Pointer to string identifying register
 *              Value                   - Actual register contents value
 *              Acpi_test_spec_section  - TDS section containing assertion
 *              Acpi_assertion          - Assertion number being tested
 *
 * RETURN:      none
 *
 * DESCRIPTION: Display failure message and link failure to TDS assertion
 *
 ******************************************************************************/

void
acpi_cm_facp_register_error (
	char                    *register_name,
	u32                     value)
{

	REPORT_ERROR ("Invalid FACP register value");

}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_hardware_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and validate various ACPI registers
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_hardware_initialize (void)
{
	ACPI_STATUS             status = AE_OK;
	s32                     index;


	/* Are we running on the actual hardware */

	if (!acpi_gbl_acpi_hardware_present) {
		/* No, just return */

		return (AE_OK);
	}

	/* We must have the ACPI tables by the time we get here */

	if (!acpi_gbl_FACP) {
		acpi_gbl_restore_acpi_chipset = FALSE;

		return (AE_NO_ACPI_TABLES);
	}

	/* Must support *some* mode! */
/*
	if (!(System_flags & SYS_MODES_MASK)) {
		Restore_acpi_chipset = FALSE;

		return (AE_ERROR);
	}

*/


	switch (acpi_gbl_system_flags & SYS_MODES_MASK)
	{
		/* Identify current ACPI/legacy mode   */

	case (SYS_MODE_ACPI):

		acpi_gbl_original_mode = SYS_MODE_ACPI;
		break;


	case (SYS_MODE_LEGACY):

		acpi_gbl_original_mode = SYS_MODE_LEGACY;
		break;


	case (SYS_MODE_ACPI | SYS_MODE_LEGACY):

		if (acpi_hw_get_mode () == SYS_MODE_ACPI) {
			acpi_gbl_original_mode = SYS_MODE_ACPI;
		}
		else {
			acpi_gbl_original_mode = SYS_MODE_LEGACY;
		}

		break;
	}


	if (acpi_gbl_system_flags & SYS_MODE_ACPI) {
		/* Target system supports ACPI mode */

		/*
		 * The purpose of this block of code is to save the initial state
		 * of the ACPI event enable registers. An exit function will be
		 * registered which will restore this state when the application
		 * exits. The exit function will also clear all of the ACPI event
		 * status bits prior to restoring the original mode.
		 *
		 * The location of the PM1a_evt_blk enable registers is defined as the
		 * base of PM1a_evt_blk + PM1a_evt_blk_length / 2. Since the spec further
		 * fully defines the PM1a_evt_blk to be a total of 4 bytes, the offset
		 * for the enable registers is always 2 from the base. It is hard
		 * coded here. If this changes in the spec, this code will need to
		 * be modified. The PM1b_evt_blk behaves as expected.
		 */

		acpi_gbl_pm1_enable_register_save =
			acpi_os_in16 ((acpi_gbl_FACP->pm1a_evt_blk + 2));
		if (acpi_gbl_FACP->pm1b_evt_blk) {
			acpi_gbl_pm1_enable_register_save |=
				acpi_os_in16 ((acpi_gbl_FACP->pm1b_evt_blk + 2));
		}


		/*
		 * The GPEs behave similarly, except that the length of the register
		 * block is not fixed, so the buffer must be allocated with malloc
		 */

		if (acpi_gbl_FACP->gpe0blk && acpi_gbl_FACP->gpe0blk_len) {
			/* GPE0 specified in FACP  */

			acpi_gbl_gpe0enable_register_save =
				acpi_cm_allocate (DIV_2 (acpi_gbl_FACP->gpe0blk_len));
			if (!acpi_gbl_gpe0enable_register_save) {
				return (AE_NO_MEMORY);
			}

			/* Save state of GPE0 enable bits */

			for (index = 0; index < DIV_2 (acpi_gbl_FACP->gpe0blk_len); index++) {
				acpi_gbl_gpe0enable_register_save[index] =
					acpi_os_in8 (acpi_gbl_FACP->gpe0blk +
					DIV_2 (acpi_gbl_FACP->gpe0blk_len));
			}
		}

		else {
			acpi_gbl_gpe0enable_register_save = NULL;
		}

		if (acpi_gbl_FACP->gpe1_blk && acpi_gbl_FACP->gpe1_blk_len) {
			/* GPE1 defined */

			acpi_gbl_gpe1_enable_register_save =
				acpi_cm_allocate (DIV_2 (acpi_gbl_FACP->gpe1_blk_len));
			if (!acpi_gbl_gpe1_enable_register_save) {
				return (AE_NO_MEMORY);
			}

			/* save state of GPE1 enable bits */

			for (index = 0; index < DIV_2 (acpi_gbl_FACP->gpe1_blk_len); index++) {
				acpi_gbl_gpe1_enable_register_save[index] =
					acpi_os_in8 (acpi_gbl_FACP->gpe1_blk +
					DIV_2 (acpi_gbl_FACP->gpe1_blk_len));
			}
		}

		else {
			acpi_gbl_gpe1_enable_register_save = NULL;
		}


		/*
		 * Verify Fixed ACPI Description Table fields,
		 * but don't abort on any problems, just display error
		 */

		if (acpi_gbl_FACP->pm1_evt_len < 4) {
			acpi_cm_facp_register_error ("PM1_EVT_LEN",
					 (u32) acpi_gbl_FACP->pm1_evt_len);
		}

		if (!acpi_gbl_FACP->pm1_cnt_len) {
			acpi_cm_facp_register_error ("PM1_CNT_LEN",
					 (u32) acpi_gbl_FACP->pm1_cnt_len);
		}

		if (!acpi_gbl_FACP->pm1a_evt_blk) {
			acpi_cm_facp_register_error ("PM1a_EVT_BLK", acpi_gbl_FACP->pm1a_evt_blk);
		}

		if (!acpi_gbl_FACP->pm1a_cnt_blk) {
			acpi_cm_facp_register_error ("PM1a_CNT_BLK", acpi_gbl_FACP->pm1a_cnt_blk);
		}

		if (!acpi_gbl_FACP->pm_tmr_blk) {
			acpi_cm_facp_register_error ("PM_TMR_BLK", acpi_gbl_FACP->pm_tmr_blk);
		}

		if (acpi_gbl_FACP->pm2_cnt_blk && !acpi_gbl_FACP->pm2_cnt_len) {
			acpi_cm_facp_register_error ("PM2_CNT_LEN",
					 (u32) acpi_gbl_FACP->pm2_cnt_len);
		}

		if (acpi_gbl_FACP->pm_tm_len < 4) {
			acpi_cm_facp_register_error ("PM_TM_LEN",
					 (u32) acpi_gbl_FACP->pm_tm_len);
		}

		/* length not multiple of 2    */
		if (acpi_gbl_FACP->gpe0blk && (acpi_gbl_FACP->gpe0blk_len & 1)) {
			acpi_cm_facp_register_error ("GPE0_BLK_LEN",
					 (u32) acpi_gbl_FACP->gpe0blk_len);
		}

		/* length not multiple of 2    */
		if (acpi_gbl_FACP->gpe1_blk && (acpi_gbl_FACP->gpe1_blk_len & 1)) {
			acpi_cm_facp_register_error ("GPE1_BLK_LEN",
					 (u32) acpi_gbl_FACP->gpe1_blk_len);
		}
	}


	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: free memory allocated for table storage.
 *
 ******************************************************************************/

void
acpi_cm_terminate (void)
{


	/* Free global tables, etc. */

	if (acpi_gbl_gpe0enable_register_save) {
		acpi_cm_free (acpi_gbl_gpe0enable_register_save);
	}

	if (acpi_gbl_gpe1_enable_register_save) {
		acpi_cm_free (acpi_gbl_gpe1_enable_register_save);
	}


	return;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_subsystem_shutdown
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Shutdown the various subsystems.  Don't delete the mutex
 *              objects here -- because the AML debugger may be still running.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_subsystem_shutdown (void)
{

	/* Just exit if subsystem is already shutdown */

	if (acpi_gbl_shutdown) {
		return (AE_OK);
	}

	/* Subsystem appears active, go ahead and shut it down */

	acpi_gbl_shutdown = TRUE;

	/* Close the Namespace */

	acpi_ns_terminate ();

	/* Close the Acpi_event Handling */

	acpi_ev_terminate ();

	/* Close the globals */

	acpi_cm_terminate ();

	/* Flush the local cache(s) */

	acpi_cm_delete_generic_state_cache ();
	acpi_cm_delete_object_cache ();
	acpi_ds_delete_walk_state_cache ();

	/* Close the Parser */

	/* TBD: [Restructure] Acpi_ps_terminate () */

	acpi_ps_delete_parse_cache ();

	/* Debug only - display leftover memory allocation, if any */

	acpi_cm_dump_current_allocations (ACPI_UINT32_MAX, NULL);

	BREAKPOINT3;

	return (AE_OK);
}


