
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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


#include <acpi/acpi.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwregs")


/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *
 ******************************************************************************/

acpi_status
acpi_hw_clear_acpi_status (void)
{
	acpi_native_uint                i;
	acpi_native_uint                gpe_block;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("hw_clear_acpi_status");


	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %04X\n",
		ACPI_BITMASK_ALL_FIXED_STATUS,
		(u16) acpi_gbl_FADT->xpm1a_evt_blk.address));


	status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_STATUS,
			  ACPI_BITMASK_ALL_FIXED_STATUS);
	if (ACPI_FAILURE (status)) {
		goto unlock_and_exit;
	}

	/* Clear the fixed events */

	if (acpi_gbl_FADT->xpm1b_evt_blk.address) {
		status = acpi_hw_low_level_write (16, ACPI_BITMASK_ALL_FIXED_STATUS,
				 &acpi_gbl_FADT->xpm1b_evt_blk, 0);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}
	}

	/* Clear the GPE Bits */

	for (gpe_block = 0; gpe_block < ACPI_MAX_GPE_BLOCKS; gpe_block++) {
		for (i = 0; i < acpi_gbl_gpe_block_info[gpe_block].register_count; i++) {
			status = acpi_hw_low_level_write (8, 0xFF,
					 acpi_gbl_gpe_block_info[gpe_block].block_address, (u32) i);
			if (ACPI_FAILURE (status)) {
				goto unlock_and_exit;
			}
		}
	}

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_sleep_type_data
 *
 * PARAMETERS:  sleep_state         - Numeric sleep state
 *              *sleep_type_a        - Where SLP_TYPa is returned
 *              *sleep_type_b        - Where SLP_TYPb is returned
 *
 * RETURN:      Status - ACPI status
 *
 * DESCRIPTION: Obtain the SLP_TYPa and SLP_TYPb values for the requested sleep
 *              state.
 *
 ******************************************************************************/

acpi_status
acpi_get_sleep_type_data (
	u8                              sleep_state,
	u8                              *sleep_type_a,
	u8                              *sleep_type_b)
{
	acpi_status                     status = AE_OK;
	union acpi_operand_object       *obj_desc;


	ACPI_FUNCTION_TRACE ("acpi_get_sleep_type_data");


	/*
	 * Validate parameters
	 */
	if ((sleep_state > ACPI_S_STATES_MAX) ||
		!sleep_type_a || !sleep_type_b) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Evaluate the namespace object containing the values for this state
	 */
	status = acpi_ns_evaluate_by_name ((char *) acpi_gbl_db_sleep_states[sleep_state],
			  NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%s while evaluating sleep_state [%s]\n",
			acpi_format_exception (status), acpi_gbl_db_sleep_states[sleep_state]));

		return_ACPI_STATUS (status);
	}

	/* Must have a return object */

	if (!obj_desc) {
		ACPI_REPORT_ERROR (("Missing Sleep State object\n"));
		status = AE_NOT_EXIST;
	}

	/* It must be of type Package */

	else if (ACPI_GET_OBJECT_TYPE (obj_desc) != ACPI_TYPE_PACKAGE) {
		ACPI_REPORT_ERROR (("Sleep State object not a Package\n"));
		status = AE_AML_OPERAND_TYPE;
	}

	/* The package must have at least two elements */

	else if (obj_desc->package.count < 2) {
		ACPI_REPORT_ERROR (("Sleep State package does not have at least two elements\n"));
		status = AE_AML_NO_OPERAND;
	}

	/* The first two elements must both be of type Integer */

	else if ((ACPI_GET_OBJECT_TYPE (obj_desc->package.elements[0]) != ACPI_TYPE_INTEGER) ||
			 (ACPI_GET_OBJECT_TYPE (obj_desc->package.elements[1]) != ACPI_TYPE_INTEGER)) {
		ACPI_REPORT_ERROR (("Sleep State package elements are not both Integers (%s, %s)\n",
			acpi_ut_get_object_type_name (obj_desc->package.elements[0]),
			acpi_ut_get_object_type_name (obj_desc->package.elements[1])));
		status = AE_AML_OPERAND_TYPE;
	}
	else {
		/*
		 * Valid _Sx_ package size, type, and value
		 */
		*sleep_type_a = (u8) (obj_desc->package.elements[0])->integer.value;
		*sleep_type_b = (u8) (obj_desc->package.elements[1])->integer.value;
	}

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "While evaluating sleep_state [%s], bad Sleep object %p type %s\n",
			acpi_gbl_db_sleep_states[sleep_state], obj_desc, acpi_ut_get_object_type_name (obj_desc)));
	}

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_register_bit_mask
 *
 * PARAMETERS:  register_id         - Index of ACPI Register to access
 *
 * RETURN:      The bit mask to be used when accessing the register
 *
 * DESCRIPTION: Map register_id into a register bit mask.
 *
 ******************************************************************************/

struct acpi_bit_register_info *
acpi_hw_get_bit_register_info (
	u32                             register_id)
{
	ACPI_FUNCTION_NAME ("hw_get_bit_register_info");


	if (register_id > ACPI_BITREG_MAX) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid bit_register ID: %X\n", register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_register
 *
 * PARAMETERS:  register_id         - Index of ACPI Register to access
 *              use_lock            - Lock the hardware
 *
 * RETURN:      Value is read from specified Register.  Value returned is
 *              normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function.
 *
 ******************************************************************************/

acpi_status
acpi_get_register (
	u32                             register_id,
	u32                             *return_value,
	u32                             flags)
{
	u32                             register_value = 0;
	struct acpi_bit_register_info   *bit_reg_info;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("acpi_get_register");


	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info (register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (flags & ACPI_MTX_LOCK) {
		status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	status = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK,
			  bit_reg_info->parent_register, &register_value);

	if (flags & ACPI_MTX_LOCK) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	if (ACPI_SUCCESS (status)) {
		/* Normalize the value that was read */

		register_value = ((register_value & bit_reg_info->access_bit_mask)
				   >> bit_reg_info->bit_position);

		*return_value = register_value;

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "Read value %X\n", register_value));
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_set_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              Value           - (only used on write) value to write to the
 *                                Register, NOT pre-normalized to the bit pos.
 *              Flags           - Lock the hardware or not
 *
 * RETURN:      None
 *
 * DESCRIPTION: ACPI Bit Register write function.
 *
 ******************************************************************************/

acpi_status
acpi_set_register (
	u32                             register_id,
	u32                             value,
	u32                             flags)
{
	u32                             register_value = 0;
	struct acpi_bit_register_info   *bit_reg_info;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE_U32 ("acpi_set_register", register_id);


	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info (register_id);
	if (!bit_reg_info) {
		ACPI_REPORT_ERROR (("Bad ACPI HW register_id: %X\n", register_id));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (flags & ACPI_MTX_LOCK) {
		status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Always do a register read first so we can insert the new bits  */

	status = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK,
			  bit_reg_info->parent_register, &register_value);
	if (ACPI_FAILURE (status)) {
		goto unlock_and_exit;
	}

	/*
	 * Decode the Register ID
	 * Register id = Register block id | bit id
	 *
	 * Check bit id to fine locate Register offset.
	 * Check Mask to determine Register offset, and then read-write.
	 */
	switch (bit_reg_info->parent_register) {
	case ACPI_REGISTER_PM1_STATUS:

		/*
		 * Status Registers are different from the rest.  Clear by
		 * writing 1, writing 0 has no effect.  So, the only relevent
		 * information is the single bit we're interested in, all others should
		 * be written as 0 so they will be left unchanged
		 */
		value = ACPI_REGISTER_PREPARE_BITS (value,
				 bit_reg_info->bit_position, bit_reg_info->access_bit_mask);
		if (value) {
			status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK,
					 ACPI_REGISTER_PM1_STATUS, (u16) value);
			register_value = 0;
		}
		break;


	case ACPI_REGISTER_PM1_ENABLE:

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position,
				bit_reg_info->access_bit_mask, value);

		status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK,
				  ACPI_REGISTER_PM1_ENABLE, (u16) register_value);
		break;


	case ACPI_REGISTER_PM1_CONTROL:

		/*
		 * Read the PM1 Control register.
		 * Note that at this level, the fact that there are actually TWO
		 * registers (A and B - and that B may not exist) is abstracted.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM1 control: Read %X\n", register_value));

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position,
				bit_reg_info->access_bit_mask, value);

		status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, register_id,
				(u16) register_value);
		break;


	case ACPI_REGISTER_PM2_CONTROL:

		status = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK,
				 ACPI_REGISTER_PM2_CONTROL, &register_value);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM2 control: Read %X from %8.8X%8.8X\n",
			register_value,
			ACPI_HIDWORD (acpi_gbl_FADT->xpm2_cnt_blk.address),
			ACPI_LODWORD (acpi_gbl_FADT->xpm2_cnt_blk.address)));

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position,
				bit_reg_info->access_bit_mask, value);

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %4.4X to %8.8X%8.8X\n",
			register_value,
			ACPI_HIDWORD (acpi_gbl_FADT->xpm2_cnt_blk.address),
			ACPI_LODWORD (acpi_gbl_FADT->xpm2_cnt_blk.address)));

		status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK,
				   ACPI_REGISTER_PM2_CONTROL, (u8) (register_value));
		break;


	default:
		break;
	}


unlock_and_exit:

	if (flags & ACPI_MTX_LOCK) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	/* Normalize the value that was read */

	ACPI_DEBUG_EXEC (register_value = ((register_value & bit_reg_info->access_bit_mask) >> bit_reg_info->bit_position));

	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "ACPI Register Write actual %X\n", register_value));
	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_read
 *
 * PARAMETERS:  use_lock               - Mutex hw access.
 *              register_id            - register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register read function.  Registers are read at the
 *              given offset.
 *
 ******************************************************************************/

acpi_status
acpi_hw_register_read (
	u8                              use_lock,
	u32                             register_id,
	u32                             *return_value)
{
	u32                             value1 = 0;
	u32                             value2 = 0;
	u32                             bank_offset;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("hw_register_read");


	if (ACPI_MTX_LOCK == use_lock) {
		status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:           /* 16-bit access */

		status = acpi_hw_low_level_read (16, &value1, &acpi_gbl_FADT->xpm1a_evt_blk, 0);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_read (16, &value2, &acpi_gbl_FADT->xpm1b_evt_blk, 0);
		value1 |= value2;
		break;


	case ACPI_REGISTER_PM1_ENABLE:           /* 16-bit access*/

		bank_offset = ACPI_DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		status = acpi_hw_low_level_read (16, &value1, &acpi_gbl_FADT->xpm1a_evt_blk, bank_offset);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_read (16, &value2, &acpi_gbl_FADT->xpm1b_evt_blk, bank_offset);
		value1 |= value2;
		break;


	case ACPI_REGISTER_PM1_CONTROL:          /* 16-bit access */

		status = acpi_hw_low_level_read (16, &value1, &acpi_gbl_FADT->xpm1a_cnt_blk, 0);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_read (16, &value2, &acpi_gbl_FADT->xpm1b_cnt_blk, 0);
		value1 |= value2;
		break;


	case ACPI_REGISTER_PM2_CONTROL:          /* 8-bit access */

		status = acpi_hw_low_level_read (8, &value1, &acpi_gbl_FADT->xpm2_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM_TIMER:             /* 32-bit access */

		status = acpi_hw_low_level_read (32, &value1, &acpi_gbl_FADT->xpm_tmr_blk, 0);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:    /* 8-bit access */

		status = acpi_os_read_port (acpi_gbl_FADT->smi_cmd, &value1, 8);
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Register ID: %X\n", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	if (ACPI_MTX_LOCK == use_lock) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	if (ACPI_SUCCESS (status)) {
		*return_value = value1;
	}

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_write
 *
 * PARAMETERS:  use_lock               - Mutex hw access.
 *              register_id            - register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register Write function.  Registers are written at the
 *              given offset.
 *
 ******************************************************************************/

acpi_status
acpi_hw_register_write (
	u8                              use_lock,
	u32                             register_id,
	u32                             value)
{
	u32                             bank_offset;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("hw_register_write");


	if (ACPI_MTX_LOCK == use_lock) {
		status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:           /* 16-bit access */

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1a_evt_blk, 0);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1b_evt_blk, 0);
		break;


	case ACPI_REGISTER_PM1_ENABLE:           /* 16-bit access*/

		bank_offset = ACPI_DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1a_evt_blk, bank_offset);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1b_evt_blk, bank_offset);
		break;


	case ACPI_REGISTER_PM1_CONTROL:          /* 16-bit access */

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1a_cnt_blk, 0);
		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1b_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM1A_CONTROL:         /* 16-bit access */

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1a_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM1B_CONTROL:         /* 16-bit access */

		status = acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->xpm1b_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM2_CONTROL:          /* 8-bit access */

		status = acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->xpm2_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM_TIMER:             /* 32-bit access */

		status = acpi_hw_low_level_write (32, value, &acpi_gbl_FADT->xpm_tmr_blk, 0);
		break;


	case ACPI_REGISTER_SMI_COMMAND_BLOCK:    /* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		status = acpi_os_write_port (acpi_gbl_FADT->smi_cmd, (acpi_integer) value, 8);
		break;


	default:
		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	if (ACPI_MTX_LOCK == use_lock) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_read
 *
 * PARAMETERS:  Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *              Width               - 8, 16, or 32
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_read (
	u32                             width,
	u32                             *value,
	struct acpi_generic_address     *reg,
	u32                             offset)
{
	acpi_physical_address           mem_address;
	acpi_io_address                 io_address;
	struct acpi_pci_id              pci_id;
	u16                             pci_register;
	acpi_status                     status;


	ACPI_FUNCTION_NAME ("hw_low_level_read");


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if ((!reg) ||
		(!reg->address)) {
		return (AE_OK);
	}
	*value = 0;

	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (reg->address
				  + (acpi_physical_address) offset);

		status = acpi_os_read_memory (mem_address, value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (acpi_io_address) (reg->address
				   + (acpi_physical_address) offset);

		status = acpi_os_read_port (io_address, value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (reg->address);
		pci_id.function = ACPI_PCI_FUNCTION (reg->address);
		pci_register   = (u16) (ACPI_PCI_REGISTER (reg->address)
				  + offset);

		status = acpi_os_read_pci_configuration (&pci_id, pci_register, value, width);
		break;


	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unsupported address space: %X\n", reg->address_space_id));
		status = AE_BAD_PARAMETER;
		break;
	}

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_write
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - To be written
 *              Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_write (
	u32                             width,
	u32                             value,
	struct acpi_generic_address     *reg,
	u32                             offset)
{
	acpi_physical_address           mem_address;
	acpi_io_address                 io_address;
	struct acpi_pci_id              pci_id;
	u16                             pci_register;
	acpi_status                     status;


	ACPI_FUNCTION_NAME ("hw_low_level_write");


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if ((!reg) ||
		(!reg->address)) {
		return (AE_OK);
	}
	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (reg->address
				  + (acpi_physical_address) offset);

		status = acpi_os_write_memory (mem_address, (acpi_integer) value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (acpi_io_address) (reg->address
				   + (acpi_physical_address) offset);

		status = acpi_os_write_port (io_address, (acpi_integer) value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (reg->address);
		pci_id.function = ACPI_PCI_FUNCTION (reg->address);
		pci_register   = (u16) (ACPI_PCI_REGISTER (reg->address)
				  + offset);

		status = acpi_os_write_pci_configuration (&pci_id, pci_register, (acpi_integer) value, width);
		break;


	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unsupported address space: %X\n", reg->address_space_id));
		status = AE_BAD_PARAMETER;
		break;
	}

	return (status);
}
