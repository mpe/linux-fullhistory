/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
 *              $Revision: 3 $
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
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evgpe")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_gpe_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the GPE data structures
 *
 ******************************************************************************/

acpi_status
acpi_ev_gpe_initialize (void)
{
	NATIVE_UINT_MAX32       i;
	NATIVE_UINT_MAX32       j;
	u32                     gpe_block;
	u32                     gpe_register;
	u32                     gpe_number_index;
	u32                     gpe_number;
	ACPI_GPE_REGISTER_INFO  *gpe_register_info;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_gpe_initialize");


	/*
	 * Initialize the GPE Block globals
	 *
	 * Why the GPE register block lengths are divided by 2:  From the ACPI Spec,
	 * section "General-Purpose Event Registers", we have:
	 *
	 * "Each register block contains two registers of equal length
	 *  GPEx_STS and GPEx_EN (where x is 0 or 1). The length of the
	 *  GPE0_STS and GPE0_EN registers is equal to half the GPE0_LEN
	 *  The length of the GPE1_STS and GPE1_EN registers is equal to
	 *  half the GPE1_LEN. If a generic register block is not supported
	 *  then its respective block pointer and block length values in the
	 *  FADT table contain zeros. The GPE0_LEN and GPE1_LEN do not need
	 *  to be the same size."
	 */
	acpi_gbl_gpe_block_info[0].register_count = 0;
	acpi_gbl_gpe_block_info[1].register_count = 0;

	acpi_gbl_gpe_block_info[0].block_address = &acpi_gbl_FADT->Xgpe0_blk;
	acpi_gbl_gpe_block_info[1].block_address = &acpi_gbl_FADT->Xgpe1_blk;

	acpi_gbl_gpe_block_info[0].block_base_number = 0;
	acpi_gbl_gpe_block_info[1].block_base_number = acpi_gbl_FADT->gpe1_base;


	/*
	 * Determine the maximum GPE number for this machine.
	 *
	 * Note: both GPE0 and GPE1 are optional, and either can exist without
	 * the other.
	 * If EITHER the register length OR the block address are zero, then that
	 * particular block is not supported.
	 */
	if (acpi_gbl_FADT->Xgpe0_blk.register_bit_width && acpi_gbl_FADT->Xgpe0_blk.address) {
		/* GPE block 0 exists (has both length and address > 0) */

		acpi_gbl_gpe_block_info[0].register_count = (u16) (acpi_gbl_FADT->Xgpe0_blk.register_bit_width / (ACPI_GPE_REGISTER_WIDTH * 2));
		acpi_gbl_gpe_number_max               = (acpi_gbl_gpe_block_info[0].register_count * ACPI_GPE_REGISTER_WIDTH) - 1;
	}

	if (acpi_gbl_FADT->Xgpe1_blk.register_bit_width && acpi_gbl_FADT->Xgpe1_blk.address) {
		/* GPE block 1 exists (has both length and address > 0) */

		acpi_gbl_gpe_block_info[1].register_count = (u16) (acpi_gbl_FADT->Xgpe1_blk.register_bit_width / (ACPI_GPE_REGISTER_WIDTH * 2));

		/* Check for GPE0/GPE1 overlap (if both banks exist) */

		if ((acpi_gbl_gpe_block_info[0].register_count) &&
			(acpi_gbl_gpe_number_max >= acpi_gbl_FADT->gpe1_base)) {
			ACPI_REPORT_ERROR ((
				"GPE0 block (GPE 0 to %d) overlaps the GPE1 block (GPE %d to %d) - Ignoring GPE1\n",
				acpi_gbl_gpe_number_max, acpi_gbl_FADT->gpe1_base,
				acpi_gbl_FADT->gpe1_base + ((acpi_gbl_gpe_block_info[1].register_count * ACPI_GPE_REGISTER_WIDTH) - 1)));

			/* Ignore GPE1 block by setting the register count to zero */

			acpi_gbl_gpe_block_info[1].register_count = 0;
		}
		else {
			/*
			 * GPE0 and GPE1 do not have to be contiguous in the GPE number space,
			 * But, GPE0 always starts at zero.
			 */
			acpi_gbl_gpe_number_max = acpi_gbl_FADT->gpe1_base +
					 ((acpi_gbl_gpe_block_info[1].register_count * ACPI_GPE_REGISTER_WIDTH) - 1);
		}
	}

	/* Exit if there are no GPE registers */

	acpi_gbl_gpe_register_count = acpi_gbl_gpe_block_info[0].register_count +
			 acpi_gbl_gpe_block_info[1].register_count;
	if (!acpi_gbl_gpe_register_count) {
		/* GPEs are not required by ACPI, this is OK */

		ACPI_REPORT_INFO (("There are no GPE blocks defined in the FADT\n"));
		return_ACPI_STATUS (AE_OK);
	}

	/* Check for Max GPE number out-of-range */

	if (acpi_gbl_gpe_number_max > ACPI_GPE_MAX) {
		ACPI_REPORT_ERROR (("Maximum GPE number from FADT is too large: 0x%X\n",
			acpi_gbl_gpe_number_max));
		return_ACPI_STATUS (AE_BAD_VALUE);
	}

	/* Allocate the GPE number-to-index translation table */

	acpi_gbl_gpe_number_to_index = ACPI_MEM_CALLOCATE (
			   sizeof (ACPI_GPE_INDEX_INFO) *
			   ((ACPI_SIZE) acpi_gbl_gpe_number_max + 1));
	if (!acpi_gbl_gpe_number_to_index) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not allocate the Gpe_number_to_index table\n"));
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Set the Gpe index table to GPE_INVALID */

	ACPI_MEMSET (acpi_gbl_gpe_number_to_index, (int) ACPI_GPE_INVALID,
			sizeof (ACPI_GPE_INDEX_INFO) * ((ACPI_SIZE) acpi_gbl_gpe_number_max + 1));

	/* Allocate the GPE register information block */

	acpi_gbl_gpe_register_info = ACPI_MEM_CALLOCATE (
			  (ACPI_SIZE) acpi_gbl_gpe_register_count *
			  sizeof (ACPI_GPE_REGISTER_INFO));
	if (!acpi_gbl_gpe_register_info) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not allocate the Gpe_register_info table\n"));
		goto error_exit1;
	}

	/*
	 * Allocate the GPE dispatch handler block.  There are eight distinct GPEs
	 * per register.  Initialization to zeros is sufficient.
	 */
	acpi_gbl_gpe_number_info = ACPI_MEM_CALLOCATE (
			  (ACPI_SIZE) (acpi_gbl_gpe_register_count * ACPI_GPE_REGISTER_WIDTH) *
			  sizeof (ACPI_GPE_NUMBER_INFO));
	if (!acpi_gbl_gpe_number_info) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not allocate the Gpe_number_info table\n"));
		goto error_exit2;
	}

	/*
	 * Initialize the GPE information and validation tables.  A goal of these
	 * tables is to hide the fact that there are two separate GPE register sets
	 * in a given gpe hardware block, the status registers occupy the first half,
	 * and the enable registers occupy the second half.  Another goal is to hide
	 * the fact that there may be multiple GPE hardware blocks.
	 */
	gpe_register = 0;
	gpe_number_index = 0;

	for (gpe_block = 0; gpe_block < ACPI_MAX_GPE_BLOCKS; gpe_block++) {
		for (i = 0; i < acpi_gbl_gpe_block_info[gpe_block].register_count; i++) {
			gpe_register_info = &acpi_gbl_gpe_register_info[gpe_register];

			/* Init the Register info for this entire GPE register (8 GPEs) */

			gpe_register_info->base_gpe_number = (u8) (acpi_gbl_gpe_block_info[gpe_block].block_base_number
					   + (i * ACPI_GPE_REGISTER_WIDTH));

			ACPI_STORE_ADDRESS (gpe_register_info->status_address.address,
					   (acpi_gbl_gpe_block_info[gpe_block].block_address->address
							  + i));

			ACPI_STORE_ADDRESS (gpe_register_info->enable_address.address,
					   (acpi_gbl_gpe_block_info[gpe_block].block_address->address
							  + i
							  + acpi_gbl_gpe_block_info[gpe_block].register_count));

			gpe_register_info->status_address.address_space_id = acpi_gbl_gpe_block_info[gpe_block].block_address->address_space_id;
			gpe_register_info->enable_address.address_space_id = acpi_gbl_gpe_block_info[gpe_block].block_address->address_space_id;
			gpe_register_info->status_address.register_bit_width = ACPI_GPE_REGISTER_WIDTH;
			gpe_register_info->enable_address.register_bit_width = ACPI_GPE_REGISTER_WIDTH;
			gpe_register_info->status_address.register_bit_offset = ACPI_GPE_REGISTER_WIDTH;
			gpe_register_info->enable_address.register_bit_offset = ACPI_GPE_REGISTER_WIDTH;

			/* Init the Index mapping info for each GPE number within this register */

			for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
				gpe_number = gpe_register_info->base_gpe_number + j;
				acpi_gbl_gpe_number_to_index[gpe_number].number_index = (u8) gpe_number_index;

				acpi_gbl_gpe_number_info[gpe_number_index].bit_mask = acpi_gbl_decode_to8bit[j];
				gpe_number_index++;
			}

			/*
			 * Clear the status/enable registers.  Note that status registers
			 * are cleared by writing a '1', while enable registers are cleared
			 * by writing a '0'.
			 */
			status = acpi_hw_low_level_write (ACPI_GPE_REGISTER_WIDTH, 0x00, &gpe_register_info->enable_address, 0);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			status = acpi_hw_low_level_write (ACPI_GPE_REGISTER_WIDTH, 0xFF, &gpe_register_info->status_address, 0);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			gpe_register++;
		}

		if (i) {
			/* Dump info about this valid GPE block */

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "GPE Block%d: %X registers at %8.8X%8.8X\n",
				(s32) gpe_block, acpi_gbl_gpe_block_info[0].register_count,
				ACPI_HIDWORD (acpi_gbl_gpe_block_info[gpe_block].block_address->address),
				ACPI_LODWORD (acpi_gbl_gpe_block_info[gpe_block].block_address->address)));

			ACPI_REPORT_INFO (("GPE Block%d defined as GPE%d to GPE%d\n",
				(s32) gpe_block,
				(u32) acpi_gbl_gpe_block_info[gpe_block].block_base_number,
				(u32) (acpi_gbl_gpe_block_info[gpe_block].block_base_number +
					((acpi_gbl_gpe_block_info[gpe_block].register_count * ACPI_GPE_REGISTER_WIDTH) -1))));
		}
	}

	return_ACPI_STATUS (AE_OK);


	/* Error cleanup */

error_exit2:
	ACPI_MEM_FREE (acpi_gbl_gpe_register_info);

error_exit1:
	ACPI_MEM_FREE (acpi_gbl_gpe_number_to_index);
	return_ACPI_STATUS (AE_NO_MEMORY);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_save_method_info
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Called from Acpi_walk_namespace. Expects each object to be a
 *              control method under the _GPE portion of the namespace.
 *              Extract the name and GPE type from the object, saving this
 *              information for quick lookup during GPE dispatch
 *
 *              The name of each GPE control method is of the form:
 *                  "_Lnn" or "_Enn"
 *              Where:
 *                  L      - means that the GPE is level triggered
 *                  E      - means that the GPE is edge triggered
 *                  nn     - is the GPE number [in HEX]
 *
 ******************************************************************************/

static acpi_status
acpi_ev_save_method_info (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *obj_desc,
	void                    **return_value)
{
	u32                     gpe_number;
	u32                     gpe_number_index;
	char                    name[ACPI_NAME_SIZE + 1];
	u8                      type;
	acpi_status             status;


	ACPI_FUNCTION_NAME ("Ev_save_method_info");


	/* Extract the name from the object and convert to a string */

	ACPI_MOVE_UNALIGNED32_TO_32 (name,
			 &((acpi_namespace_node *) obj_handle)->name.integer);
	name[ACPI_NAME_SIZE] = 0;

	/*
	 * Edge/Level determination is based on the 2nd character of the method name
	 */
	switch (name[1]) {
	case 'L':
		type = ACPI_EVENT_LEVEL_TRIGGERED;
		break;

	case 'E':
		type = ACPI_EVENT_EDGE_TRIGGERED;
		break;

	default:
		/* Unknown method type, just ignore it! */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown GPE method type: %s (name not of form _Lnn or _Enn)\n",
			name));
		return (AE_OK);
	}

	/* Convert the last two characters of the name to the GPE Number */

	gpe_number = ACPI_STRTOUL (&name[2], NULL, 16);
	if (gpe_number == ACPI_UINT32_MAX) {
		/* Conversion failed; invalid method, just ignore it */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not extract GPE number from name: %s (name not of form _Lnn or _Enn)\n",
			name));
		return (AE_OK);
	}

	/* Get GPE index and ensure that we have a valid GPE number */

	gpe_number_index = acpi_ev_get_gpe_number_index (gpe_number);
	if (gpe_number_index == ACPI_GPE_INVALID) {
		/* Not valid, all we can do here is ignore it */

		return (AE_OK);
	}

	/*
	 * Now we can add this information to the Gpe_info block
	 * for use during dispatch of this GPE.
	 */
	acpi_gbl_gpe_number_info [gpe_number_index].type  = type;
	acpi_gbl_gpe_number_info [gpe_number_index].method_node = (acpi_namespace_node *) obj_handle;

	/*
	 * Enable the GPE (SCIs should be disabled at this point)
	 */
	status = acpi_hw_enable_gpe (gpe_number);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Registered GPE method %s as GPE number %2.2X\n",
		name, gpe_number));
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_init_gpe_control_methods
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain the control methods associated with the GPEs.
 *              NOTE: Must be called AFTER namespace initialization!
 *
 ******************************************************************************/

acpi_status
acpi_ev_init_gpe_control_methods (void)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_init_gpe_control_methods");


	/* Get a permanent handle to the _GPE object */

	status = acpi_get_handle (NULL, "\\_GPE", &acpi_gbl_gpe_obj_handle);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Traverse the namespace under \_GPE to find all methods there */

	status = acpi_walk_namespace (ACPI_TYPE_METHOD, acpi_gbl_gpe_obj_handle,
			  ACPI_UINT32_MAX, acpi_ev_save_method_info,
			  NULL, NULL);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_gpe_detect
 *
 * PARAMETERS:  None
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect if any GP events have occurred.  This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_detect (void)
{
	u32                     int_status = ACPI_INTERRUPT_NOT_HANDLED;
	u32                     i;
	u32                     j;
	u8                      enabled_status_byte;
	u8                      bit_mask;
	ACPI_GPE_REGISTER_INFO  *gpe_register_info;
	u32                     in_value;
	acpi_status             status;


	ACPI_FUNCTION_NAME ("Ev_gpe_detect");


	/*
	 * Read all of the 8-bit GPE status and enable registers
	 * in both of the register blocks, saving all of it.
	 * Find all currently active GP events.
	 */
	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		gpe_register_info = &acpi_gbl_gpe_register_info[i];

		status = acpi_hw_low_level_read (ACPI_GPE_REGISTER_WIDTH, &in_value, &gpe_register_info->status_address, 0);
		gpe_register_info->status = (u8) in_value;
		if (ACPI_FAILURE (status)) {
			return (ACPI_INTERRUPT_NOT_HANDLED);
		}

		status = acpi_hw_low_level_read (ACPI_GPE_REGISTER_WIDTH, &in_value, &gpe_register_info->enable_address, 0);
		gpe_register_info->enable = (u8) in_value;
		if (ACPI_FAILURE (status)) {
			return (ACPI_INTERRUPT_NOT_HANDLED);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
			"GPE block at %8.8X%8.8X - Values: Enable %02X Status %02X\n",
			ACPI_HIDWORD (gpe_register_info->enable_address.address),
			ACPI_LODWORD (gpe_register_info->enable_address.address),
			gpe_register_info->enable,
			gpe_register_info->status));

		/* First check if there is anything active at all in this register */

		enabled_status_byte = (u8) (gpe_register_info->status &
				   gpe_register_info->enable);
		if (!enabled_status_byte) {
			/* No active GPEs in this register, move on */

			continue;
		}

		/* Now look at the individual GPEs in this byte register */

		for (j = 0, bit_mask = 1; j < ACPI_GPE_REGISTER_WIDTH; j++, bit_mask <<= 1) {
			/* Examine one GPE bit */

			if (enabled_status_byte & bit_mask) {
				/*
				 * Found an active GPE.  Dispatch the event to a handler
				 * or method.
				 */
				int_status |= acpi_ev_gpe_dispatch (
						  gpe_register_info->base_gpe_number + j);
			}
		}
	}

	return (int_status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_asynch_execute_gpe_method
 *
 * PARAMETERS:  Gpe_number      - The 0-based GPE number
 *
 * RETURN:      None
 *
 * DESCRIPTION: Perform the actual execution of a GPE control method.  This
 *              function is called from an invocation of Acpi_os_queue_for_execution
 *              (and therefore does NOT execute at interrupt level) so that
 *              the control method itself is not executed in the context of
 *              the SCI interrupt handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
acpi_ev_asynch_execute_gpe_method (
	void                    *context)
{
	u32                     gpe_number = (u32) ACPI_TO_INTEGER (context);
	u32                     gpe_number_index;
	ACPI_GPE_NUMBER_INFO    gpe_info;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_asynch_execute_gpe_method");


	gpe_number_index = acpi_ev_get_gpe_number_index (gpe_number);
	if (gpe_number_index == ACPI_GPE_INVALID) {
		return_VOID;
	}

	/*
	 * Take a snapshot of the GPE info for this level - we copy the
	 * info to prevent a race condition with Remove_handler.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_VOID;
	}

	gpe_info = acpi_gbl_gpe_number_info [gpe_number_index];
	status = acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_VOID;
	}

	if (gpe_info.method_node) {
		/*
		 * Invoke the GPE Method (_Lxx, _Exx):
		 * (Evaluate the _Lxx/_Exx control method that corresponds to this GPE.)
		 */
		status = acpi_ns_evaluate_by_handle (gpe_info.method_node, NULL, NULL);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("%s while evaluating method [%4.4s] for GPE[%2.2X]\n",
				acpi_format_exception (status),
				gpe_info.method_node->name.ascii, gpe_number));
		}
	}

	if (gpe_info.type & ACPI_EVENT_LEVEL_TRIGGERED) {
		/*
		 * GPE is level-triggered, we clear the GPE status bit after handling
		 * the event.
		 */
		status = acpi_hw_clear_gpe (gpe_number);
		if (ACPI_FAILURE (status)) {
			return_VOID;
		}
	}

	/*
	 * Enable the GPE.
	 */
	(void) acpi_hw_enable_gpe (gpe_number);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_gpe_dispatch
 *
 * PARAMETERS:  Gpe_number      - The 0-based GPE number
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.  This function executes
 *              at interrupt level.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_dispatch (
	u32                     gpe_number)
{
	u32                     gpe_number_index;
	ACPI_GPE_NUMBER_INFO    *gpe_info;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_gpe_dispatch");


	gpe_number_index = acpi_ev_get_gpe_number_index (gpe_number);
	if (gpe_number_index == ACPI_GPE_INVALID) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "GPE[%X] is not a valid event\n", gpe_number));
		return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
	}

	/*
	 * We don't have to worry about mutex on Gpe_info because we are
	 * executing at interrupt level.
	 */
	gpe_info = &acpi_gbl_gpe_number_info [gpe_number_index];

	/*
	 * If edge-triggered, clear the GPE status bit now.  Note that
	 * level-triggered events are cleared after the GPE is serviced.
	 */
	if (gpe_info->type & ACPI_EVENT_EDGE_TRIGGERED) {
		status = acpi_hw_clear_gpe (gpe_number);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: Unable to clear GPE[%2.2X]\n", gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/*
	 * Dispatch the GPE to either an installed handler, or the control
	 * method associated with this GPE (_Lxx or _Exx).
	 * If a handler exists, we invoke it and do not attempt to run the method.
	 * If there is neither a handler nor a method, we disable the level to
	 * prevent further events from coming in here.
	 */
	if (gpe_info->handler) {
		/* Invoke the installed handler (at interrupt level) */

		gpe_info->handler (gpe_info->context);
	}
	else if (gpe_info->method_node) {
		/*
		 * Disable GPE, so it doesn't keep firing before the method has a
		 * chance to run.
		 */
		status = acpi_hw_disable_gpe (gpe_number);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: Unable to disable GPE[%2.2X]\n", gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}

		/*
		 * Execute the method associated with the GPE.
		 */
		if (ACPI_FAILURE (acpi_os_queue_for_execution (OSD_PRIORITY_GPE,
				 acpi_ev_asynch_execute_gpe_method,
				 ACPI_TO_POINTER (gpe_number)))) {
			ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: Unable to queue handler for GPE[%2.2X], event is disabled\n", gpe_number));
		}
	}
	else {
		/* No handler or method to run! */

		ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: No handler or method for GPE[%2.2X], disabling event\n", gpe_number));

		/*
		 * Disable the GPE.  The GPE will remain disabled until the ACPI
		 * Core Subsystem is restarted, or the handler is reinstalled.
		 */
		status = acpi_hw_disable_gpe (gpe_number);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: Unable to disable GPE[%2.2X]\n", gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/*
	 * It is now safe to clear level-triggered evnets.
	 */
	if (gpe_info->type & ACPI_EVENT_LEVEL_TRIGGERED) {
		status = acpi_hw_clear_gpe (gpe_number);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Acpi_ev_gpe_dispatch: Unable to clear GPE[%2.2X]\n", gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	return_VALUE (ACPI_INTERRUPT_HANDLED);
}

