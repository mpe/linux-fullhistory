
/******************************************************************************
 *
 * Module Name: cmeval - Object evaluation
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
#include "namesp.h"
#include "interp.h"


#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cmeval");


/****************************************************************************
 *
 * FUNCTION:    Acpi_cm_evaluate_numeric_object
 *
 * PARAMETERS:  Acpi_device         - NTE for the device
 *              *Address            - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: evaluates a numeric namespace object for a selected device
 *              and stores results in *Address.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ***************************************************************************/

ACPI_STATUS
acpi_cm_evaluate_numeric_object (
	char                    *object_name,
	ACPI_NAMED_OBJECT       *acpi_device,
	u32                     *address)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status;


	/* Execute the method */

	status = acpi_ns_evaluate_relative (acpi_device, object_name, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {

		return (status);
	}


	/* Did we get a return object? */

	if (!obj_desc) {
		return (AE_TYPE);
	}

	/* Is the return object of the correct type? */

	if (obj_desc->common.type != ACPI_TYPE_NUMBER) {
		status = AE_TYPE;
	}
	else {
		/*
		 * Since the structure is a union, setting any field will set all
		 * of the variables in the union
		 */
		*address = obj_desc->number.value;
	}

	/* On exit, we must delete the return object */

	acpi_cm_remove_reference (obj_desc);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_cm_execute_HID
 *
 * PARAMETERS:  Acpi_device         - NTE for the device
 *              *Hid                - Where the HID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _HID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ***************************************************************************/

ACPI_STATUS
acpi_cm_execute_HID (
	ACPI_NAMED_OBJECT       *acpi_device,
	DEVICE_ID               *hid)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status;


	/* Execute the method */

	status = acpi_ns_evaluate_relative (acpi_device,
			 METHOD_NAME__HID, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {


		return (status);
	}

	/* Did we get a return object? */

	if (!obj_desc) {
		return (AE_TYPE);
	}

	/*
	 *  A _HID can return either a Number (32 bit compressed EISA ID) or
	 *  a string
	 */

	if ((obj_desc->common.type != ACPI_TYPE_NUMBER) &&
		(obj_desc->common.type != ACPI_TYPE_STRING))
	{
		status = AE_TYPE;
	}

	else {
		if (obj_desc->common.type == ACPI_TYPE_NUMBER) {
			/* Convert the Numeric HID to string */

			acpi_aml_eisa_id_to_string (obj_desc->number.value, hid->data.buffer);
			hid->type = STRING_DEVICE_ID;
		}

		else {
			/* Copy the String HID from the returned object */

			hid->data.string_ptr = obj_desc->string.pointer;
			hid->type = STRING_PTR_DEVICE_ID;
		}
	}


	/* On exit, we must delete the return object */

	acpi_cm_remove_reference (obj_desc);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_cm_execute_UID
 *
 * PARAMETERS:  Acpi_device         - NTE for the device
 *              *Uid                - Where the UID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _UID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ***************************************************************************/

ACPI_STATUS
acpi_cm_execute_UID (
	ACPI_NAMED_OBJECT       *acpi_device,
	DEVICE_ID               *uid)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status;


	/* Execute the method */

	status = acpi_ns_evaluate_relative (acpi_device,
			 METHOD_NAME__UID, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {


		return (status);
	}

	/* Did we get a return object? */

	if (!obj_desc) {
		return (AE_TYPE);
	}

	/*
	 *  A _UID can return either a Number (32 bit compressed EISA ID) or
	 *  a string
	 */

	if ((obj_desc->common.type != ACPI_TYPE_NUMBER) &&
		(obj_desc->common.type != ACPI_TYPE_STRING))
	{
		status = AE_TYPE;
	}

	else {
		if (obj_desc->common.type == ACPI_TYPE_NUMBER) {
			/* Convert the Numeric HID to string */

			uid->data.number = obj_desc->number.value;
		}

		else {
			/* Copy the String HID from the returned object */

			uid->data.string_ptr = obj_desc->string.pointer;
			uid->type = STRING_PTR_DEVICE_ID;
		}
	}


	/* On exit, we must delete the return object */

	acpi_cm_remove_reference (obj_desc);

	return (status);
}

/****************************************************************************
 *
 * FUNCTION:    Acpi_cm_execute_STA
 *
 * PARAMETERS:  Acpi_device         - NTE for the device
 *              *Flags              - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ***************************************************************************/

ACPI_STATUS
acpi_cm_execute_STA (
	ACPI_NAMED_OBJECT       *acpi_device,
	u32                     *flags)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status;


	/* Execute the method */

	status = acpi_ns_evaluate_relative (acpi_device,
			 METHOD_NAME__STA, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {


		return (status);
	}


	/* Did we get a return object? */

	if (!obj_desc) {
		return (AE_TYPE);
	}

	/* Is the return object of the correct type? */

	if (obj_desc->common.type != ACPI_TYPE_NUMBER) {
		status = AE_TYPE;
	}

	else {
		/* Extract the status flags */

		*flags = obj_desc->number.value;
	}


	/* On exit, we must delete the return object */

	acpi_cm_remove_reference (obj_desc);

	return (status);
}
