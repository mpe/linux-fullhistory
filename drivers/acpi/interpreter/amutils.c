
/******************************************************************************
 *
 * Module Name: amutils - interpreter/scanner utilities
 *              $Revision: 53 $
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
#include "acparser.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amutils")


typedef struct internal_search_st
{
	ACPI_OPERAND_OBJECT         *dest_obj;
	u32                         index;
	ACPI_OPERAND_OBJECT         *source_obj;

} INTERNAL_PKG_SEARCH_INFO;


/* Used to traverse nested packages when copying*/

INTERNAL_PKG_SEARCH_INFO        copy_level[MAX_PACKAGE_DEPTH];


static NATIVE_CHAR          hex[] =
	{'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_enter_interpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Enter the interpreter execution region
 *
 ******************************************************************************/

void
acpi_aml_enter_interpreter (void)
{

	acpi_cm_acquire_mutex (ACPI_MTX_EXECUTE);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_exit_interpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Exit the interpreter execution region
 *
 * Cases where the interpreter is unlocked:
 *      1) Completion of the execution of a control method
 *      2) Method blocked on a Sleep() AML opcode
 *      3) Method blocked on an Acquire() AML opcode
 *      4) Method blocked on a Wait() AML opcode
 *      5) Method blocked to acquire the global lock
 *      6) Method blocked to execute a serialized control method that is
 *          already executing
 *      7) About to invoke a user-installed opregion handler
 *
 ******************************************************************************/

void
acpi_aml_exit_interpreter (void)
{

	acpi_cm_release_mutex (ACPI_MTX_EXECUTE);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_validate_object_type
 *
 * PARAMETERS:  Type            Object type to validate
 *
 * DESCRIPTION: Determine if a type is a valid ACPI object type
 *
 ******************************************************************************/

u8
acpi_aml_validate_object_type (
	ACPI_OBJECT_TYPE        type)
{

	if ((type > ACPI_TYPE_MAX && type < INTERNAL_TYPE_BEGIN) ||
		(type > INTERNAL_TYPE_MAX))
	{
		return (FALSE);
	}

	return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_buf_seq
 *
 * RETURN:      The next buffer descriptor sequence number
 *
 * DESCRIPTION: Provide a unique sequence number for each Buffer descriptor
 *              allocated during the interpreter's existence.  These numbers
 *              are used to relate Field_unit descriptors to the Buffers
 *              within which the fields are defined.
 *
 *              Just increment the global counter and return it.
 *
 ******************************************************************************/

u32
acpi_aml_buf_seq (void)
{

	return (++acpi_gbl_buf_seq);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_acquire_global_lock
 *
 * PARAMETERS:  Rule            - Lock rule: Always_lock, Never_lock
 *
 * RETURN:      TRUE/FALSE indicating whether the lock was actually acquired
 *
 * DESCRIPTION: Obtain the global lock and keep track of this fact via two
 *              methods.  A global variable keeps the state of the lock, and
 *              the state is returned to the caller.
 *
 ******************************************************************************/

u8
acpi_aml_acquire_global_lock (
	u32                     rule)
{
	u8                      locked = FALSE;
	ACPI_STATUS             status;


	/*  Only attempt lock if the Rule says so */

	if (rule == (u32) GLOCK_ALWAYS_LOCK) {
		/*  OK to get the lock   */

		status = acpi_ev_acquire_global_lock ();

		if (ACPI_SUCCESS (status)) {
			acpi_gbl_global_lock_set = TRUE;
			locked = TRUE;
		}
	}

	return (locked);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_release_global_lock
 *
 * PARAMETERS:  Locked_by_me    - Return value from corresponding call to
 *                                Acquire_global_lock.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the global lock if it is locked.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_release_global_lock (
	u8                      locked_by_me)
{


	/* Only attempt unlock if the caller locked it */

	if (locked_by_me) {
		/* Double check against the global flag */

		if (acpi_gbl_global_lock_set) {
			/* OK, now release the lock */

			acpi_ev_release_global_lock ();
			acpi_gbl_global_lock_set = FALSE;
		}

	}


	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_digits_needed
 *
 * PARAMETERS:  val             - Value to be represented
 *              base            - Base of representation
 *
 * RETURN:      the number of digits needed to represent val in base
 *
 ******************************************************************************/

u32
acpi_aml_digits_needed (
	u32                     val,
	u32                     base)
{
	u32                     num_digits = 0;


	if (base < 1) {
		/*  impossible base */

		REPORT_ERROR ("Aml_digits_needed: Impossible base");
	}

	else {
		for (num_digits = 1 + (val < 0) ; val /= base ; ++num_digits) { ; }
	}

	return (num_digits);
}


/*******************************************************************************
 *
 * FUNCTION:    ntohl
 *
 * PARAMETERS:  Value           - Value to be converted
 *
 * RETURN:      Convert a 32-bit value to big-endian (swap the bytes)
 *
 ******************************************************************************/

u32
_ntohl (
	u32                     value)
{
	union
	{
		u32                 value;
		u8                  bytes[4];
	} out;

	union
	{
		u32                 value;
		u8                  bytes[4];
	} in;


	in.value = value;

	out.bytes[0] = in.bytes[3];
	out.bytes[1] = in.bytes[2];
	out.bytes[2] = in.bytes[1];
	out.bytes[3] = in.bytes[0];

	return (out.value);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_eisa_id_to_string
 *
 * PARAMETERS:  Numeric_id      - EISA ID to be converted
 *              Out_string      - Where to put the converted string (8 bytes)
 *
 * RETURN:      Convert a numeric EISA ID to string representation
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_eisa_id_to_string (
	u32                     numeric_id,
	NATIVE_CHAR             *out_string)
{
	u32                     id;

	/* swap to big-endian to get contiguous bits */

	id = _ntohl (numeric_id);

	out_string[0] = (char) ('@' + ((id >> 26) & 0x1f));
	out_string[1] = (char) ('@' + ((id >> 21) & 0x1f));
	out_string[2] = (char) ('@' + ((id >> 16) & 0x1f));
	out_string[3] = hex[(id >> 12) & 0xf];
	out_string[4] = hex[(id >> 8) & 0xf];
	out_string[5] = hex[(id >> 4) & 0xf];
	out_string[6] = hex[id & 0xf];
	out_string[7] = 0;

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_build_copy_internal_package_object
 *
 * PARAMETERS:  *Source_obj     - Pointer to the source package object
 *              *Dest_obj       - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to copy an internal package object
 *              into another internal package object.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_build_copy_internal_package_object (
	ACPI_OPERAND_OBJECT     *source_obj,
	ACPI_OPERAND_OBJECT     *dest_obj,
	ACPI_WALK_STATE         *walk_state)
{
	u32                         current_depth = 0;
	ACPI_STATUS                 status = AE_OK;
	u32                         length = 0;
	u32                         this_index;
	u32                         object_space = 0;
	ACPI_OPERAND_OBJECT         *this_dest_obj;
	ACPI_OPERAND_OBJECT         *this_source_obj;
	INTERNAL_PKG_SEARCH_INFO    *level_ptr;


	/*
	 * Initialize the working variables
	 */

	MEMSET ((void *) copy_level, 0, sizeof(copy_level));

	copy_level[0].dest_obj  = dest_obj;
	copy_level[0].source_obj = source_obj;
	level_ptr               = &copy_level[0];
	current_depth           = 0;

	dest_obj->common.type       = source_obj->common.type;
	dest_obj->package.count     = source_obj->package.count;


	/*
	 * Build an array of ACPI_OBJECTS in the buffer
	 * and move the free space past it
	 */

	dest_obj->package.elements  = acpi_cm_callocate (
			 (dest_obj->package.count + 1) *
			 sizeof (void *));
	if (!dest_obj->package.elements) {
		/* Package vector allocation failure   */

		REPORT_ERROR ("Aml_build_copy_internal_package_object: Package vector allocation failure");
		return (AE_NO_MEMORY);
	}

	dest_obj->package.next_element = dest_obj->package.elements;


	while (1) {
		this_index      = level_ptr->index;
		this_dest_obj   = (ACPI_OPERAND_OBJECT  *) level_ptr->dest_obj->package.elements[this_index];
		this_source_obj = (ACPI_OPERAND_OBJECT  *) level_ptr->source_obj->package.elements[this_index];

		if (IS_THIS_OBJECT_TYPE (this_source_obj, ACPI_TYPE_PACKAGE)) {
			/*
			 * If this object is a package then we go one deeper
			 */
			if (current_depth >= MAX_PACKAGE_DEPTH-1) {
				/*
				 * Too many nested levels of packages for us to handle
				 */
				return (AE_LIMIT);
			}

			/*
			 * Build the package object
			 */
			this_dest_obj = acpi_cm_create_internal_object (ACPI_TYPE_PACKAGE);
			level_ptr->dest_obj->package.elements[this_index] = this_dest_obj;


			this_dest_obj->common.type      = ACPI_TYPE_PACKAGE;
			this_dest_obj->package.count    = this_dest_obj->package.count;

			/*
			 * Save space for the array of objects (Package elements)
			 * update the buffer length counter
			 */
			object_space            = this_dest_obj->package.count *
					  sizeof (ACPI_OPERAND_OBJECT);
			length                  += object_space;
			current_depth++;
			level_ptr               = &copy_level[current_depth];
			level_ptr->dest_obj     = this_dest_obj;
			level_ptr->source_obj   = this_source_obj;
			level_ptr->index        = 0;

		}   /* if object is a package */

		else {

			this_dest_obj = acpi_cm_create_internal_object (
					   this_source_obj->common.type);
			level_ptr->dest_obj->package.elements[this_index] = this_dest_obj;

			status = acpi_aml_store_object_to_object(this_source_obj, this_dest_obj, walk_state);

			if (ACPI_FAILURE (status)) {
				/*
				 * Failure get out
				 */
				return (status);
			}

			length      +=object_space;

			level_ptr->index++;
			while (level_ptr->index >= level_ptr->dest_obj->package.count) {
				/*
				 * We've handled all of the objects at this level,  This means
				 * that we have just completed a package.  That package may
				 * have contained one or more packages itself
				 */
				if (current_depth == 0) {
					/*
					 * We have handled all of the objects in the top level
					 * package just add the length of the package objects
					 * and exit
					 */
					return (AE_OK);
				}

				/*
				 * Go back up a level and move the index past the just
				 * completed package object.
				 */
				current_depth--;
				level_ptr = &copy_level[current_depth];
				level_ptr->index++;
			}
		}   /* else object is NOT a package */
	}   /* while (1)  */
}


