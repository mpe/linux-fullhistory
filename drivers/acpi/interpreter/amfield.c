/******************************************************************************
 *
 * Module Name: amfield - ACPI AML (p-code) execution - field manipulation
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
#include "dispatch.h"
#include "interp.h"
#include "amlcode.h"
#include "namesp.h"
#include "hardware.h"
#include "events.h"


#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amfield");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_setup_field
 *
 * PARAMETERS:  *Obj_desc           - Field to be read or written
 *              *Rgn_desc           - Region containing field
 *              Field_bit_width     - Field Width in bits (8, 16, or 32)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common processing for Acpi_aml_read_field and Acpi_aml_write_field
 *
 *  ACPI SPECIFICATION REFERENCES:
 *  Each of the Type1_opcodes is defined as specified in in-line
 *  comments below. For each one, use the following definitions.
 *
 *  Def_bit_field   :=  Bit_field_op    Src_buf Bit_idx Destination
 *  Def_byte_field  :=  Byte_field_op   Src_buf Byte_idx Destination
 *  Def_create_field := Create_field_op Src_buf Bit_idx Num_bits Name_string
 *  Def_dWord_field :=  DWord_field_op  Src_buf Byte_idx Destination
 *  Def_word_field  :=  Word_field_op   Src_buf Byte_idx Destination
 *  Bit_index       :=  Term_arg=>Integer
 *  Byte_index      :=  Term_arg=>Integer
 *  Destination     :=  Name_string
 *  Num_bits        :=  Term_arg=>Integer
 *  Source_buf      :=  Term_arg=>Buffer
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_setup_field (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	ACPI_OBJECT_INTERNAL    *rgn_desc,
	s32                     field_bit_width)
{
	ACPI_STATUS             status = AE_OK;
	s32                     field_byte_width;


	/* Parameter validation */

	if (!obj_desc || !rgn_desc) {
		return (AE_AML_NO_OPERAND);
	}

	if (ACPI_TYPE_REGION != rgn_desc->common.type) {
		return (AE_AML_OPERAND_TYPE);
	}


	/*
	 * Init and validate Field width
	 * Possible values are 1, 2, 4
	 */

	field_byte_width = DIV_8 (field_bit_width);

	if ((field_bit_width != 8) &&
		(field_bit_width != 16) &&
		(field_bit_width != 32))
	{
		return (AE_AML_OPERAND_VALUE);
	}


	/*
	 * If the address and length have not been previously evaluated,
	 * evaluate them and save the results.
	 */
	if (!(rgn_desc->region.region_flags & REGION_AGRUMENT_DATA_VALID)) {

		status = acpi_ds_get_region_arguments (rgn_desc);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}


	/*
	 * If (offset rounded up to next multiple of field width)
	 * exceeds region length, indicate an error.
	 */

	if (rgn_desc->region.length <
	   (obj_desc->field.offset & ~((u32) field_byte_width - 1)) +
			field_byte_width)
	{
		/*
		 * Offset rounded up to next multiple of field width
		 * exceeds region length, indicate an error
		 */

		return (AE_AML_REGION_LIMIT);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_access_named_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              Named_field         - Handle for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length         - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_access_named_field (
	s32                     mode,
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     buffer_length)
{
	ACPI_OBJECT_INTERNAL    *obj_desc = NULL;
	ACPI_STATUS             status = AE_OK;
	u8                      locked = FALSE;
	u32                     bit_granularity = 0;
	u32                     byte_granularity;
	u32                     datum_length;
	u32                     actual_byte_length;
	u32                     byte_field_length;


	/* Get the attached field object */

	obj_desc = acpi_ns_get_attached_object (named_field);
	if (!obj_desc) {
		return (AE_AML_INTERNAL);
	}

	/* Check the type */

	if (INTERNAL_TYPE_DEF_FIELD != acpi_ns_get_type (named_field)) {
		return (AE_AML_OPERAND_TYPE);
	}

	/* Obj_desc valid and Named_field is a defined field */


	/* Double-check that the attached object is also a field */

	if (INTERNAL_TYPE_DEF_FIELD != obj_desc->common.type) {
		return (AE_AML_OPERAND_TYPE);
	}


	/*
	 * Granularity was decoded from the field access type
	 * (Any_acc will be the same as Byte_acc)
	 */

	bit_granularity = obj_desc->field_unit.granularity;
	byte_granularity = DIV_8 (bit_granularity);

	/*
	 * Check if request is too large for the field, and silently truncate
	 * if necessary
	 */

	/* TBD: [Errors] should an error be returned in this case? */

	byte_field_length = (u32) DIV_8 (obj_desc->field_unit.length + 7);


	actual_byte_length = buffer_length;
	if (buffer_length > byte_field_length) {
		actual_byte_length = byte_field_length;

	}


	/* Convert byte count to datum count, round up if necessary */

	datum_length = (actual_byte_length + (byte_granularity-1)) / byte_granularity;


	/* Get the global lock if needed */

	locked = acpi_aml_acquire_global_lock (obj_desc->field_unit.lock_rule);


	/* Perform the actual read or write of the buffer */

	switch (mode)
	{
	case ACPI_READ:

		status = acpi_aml_read_field (obj_desc, buffer, buffer_length,
				  actual_byte_length, datum_length,
				  bit_granularity, byte_granularity);
		break;


	case ACPI_WRITE:

		status = acpi_aml_write_field (obj_desc, buffer, buffer_length,
				  actual_byte_length, datum_length,
				  bit_granularity, byte_granularity);
		break;


	default:

		status = AE_BAD_PARAMETER;
		break;
	}


	/* Release global lock if we acquired it earlier */

	acpi_aml_release_global_lock (locked);

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_set_named_field_value
 *
 * PARAMETERS:  Named_field         - Handle for field to be set
 *              Buffer              - Bytes to be stored
 *              Buffer_length       - Number of bytes to be stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the given value into the field
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_set_named_field_value (
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     buffer_length)
{
	ACPI_STATUS             status;


	if (!named_field) {
		return (AE_AML_INTERNAL);
	}

	status = acpi_aml_access_named_field (ACPI_WRITE, named_field, buffer,
			 buffer_length);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_get_named_field_value
 *
 * PARAMETERS:  Named_field         - Handle for field to be read
 *              *Buffer             - Where to store value read from field
 *              Buffer_length       - Max length to read
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value of the given field
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_get_named_field_value (
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     buffer_length)
{
	ACPI_STATUS             status;


	if ((!named_field) || (!buffer)) {
		return (AE_AML_INTERNAL);
	}

	status = acpi_aml_access_named_field (ACPI_READ, named_field, buffer,
			 buffer_length);
	return (status);
}

