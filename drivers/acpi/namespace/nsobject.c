
/******************************************************************************
 *
 * Module Name: nsobject - Utilities for objects attached to namespace
 *                          table entries
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
#include "amlcode.h"
#include "namesp.h"
#include "interp.h"
#include "tables.h"


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nsobject");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_attach_object
 *
 * PARAMETERS:  Handle              - Handle of nte
 *              Object              - Object to be attached
 *              Type                - Type of object, or ACPI_TYPE_ANY if not
 *                                      known
 *
 * DESCRIPTION: Record the given object as the value associated with the
 *              name whose ACPI_HANDLE is passed.  If Object is NULL
 *              and Type is ACPI_TYPE_ANY, set the name as having no value.
 *
 * MUTEX:       Assumes namespace is locked
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_attach_object (
	ACPI_HANDLE             handle,
	ACPI_HANDLE             object,
	OBJECT_TYPE_INTERNAL    type)
{
	ACPI_NAMED_OBJECT       *this_entry = (ACPI_NAMED_OBJECT*) handle;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_OBJECT_INTERNAL    *previous_obj_desc;
	OBJECT_TYPE_INTERNAL    obj_type = ACPI_TYPE_ANY;
	u8                      flags;
	u16                     opcode;


	/*
	 * Parameter validation
	 */

	if (!acpi_gbl_root_object->child_table) {
		/* Name space not initialized  */

		REPORT_ERROR ("Ns_attach_object: Name space not initialized");
		return (AE_NO_NAMESPACE);
	}

	if (!handle) {
		/* Invalid handle */

		REPORT_ERROR ("Ns_attach_object: Null name handle");
		return (AE_BAD_PARAMETER);
	}

	if (!object && (ACPI_TYPE_ANY != type)) {
		/* Null object */

		REPORT_ERROR ("Ns_attach_object: Null object, but type"
				  "not ACPI_TYPE_ANY");
		return (AE_BAD_PARAMETER);
	}

	if (!VALID_DESCRIPTOR_TYPE (handle, ACPI_DESC_TYPE_NAMED)) {
		/* Not a name handle */

		REPORT_ERROR ("Ns_attach_object: Invalid handle");
		return (AE_BAD_PARAMETER);
	}

	/* Check if this object is already attached */

	if (this_entry->object == object) {
		return (AE_OK);
	}


	/* Get the current flags field of the NTE */

	flags = this_entry->flags;
	flags &= ~NTE_AML_ATTACHMENT;


	/* If null object, we will just install it */

	if (!object) {
		obj_desc = NULL;
		obj_type = ACPI_TYPE_ANY;
	}

	/*
	 * If the object is an NTE with an attached object,
	 * we will use that (attached) object
	 */

	else if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED) &&
			((ACPI_NAMED_OBJECT*) object)->object)
	{
		/*
		 * Value passed is a name handle and that name has a
		 * non-null value.  Use that name's value and type.
		 */

		obj_desc = ((ACPI_NAMED_OBJECT*) object)->object;
		obj_type = ((ACPI_NAMED_OBJECT*) object)->type;

		/*
		 * Copy appropriate flags
		 */

		if (((ACPI_NAMED_OBJECT*) object)->flags & NTE_AML_ATTACHMENT) {
			flags |= NTE_AML_ATTACHMENT;
		}
	}


	/*
	 * Otherwise, we will use the parameter object, but we must type
	 * it first
	 */

	else {
		obj_desc = (ACPI_OBJECT_INTERNAL *) object;


		/* If a valid type (non-ANY) was given, just use it */

		if (ACPI_TYPE_ANY != type) {
			obj_type = type;
		}


		/*
		 * Type is TYPE_Any, we must try to determinte the
		 * actual type of the object
		 */

		/*
		 * Check if value points into the AML code
		 */
		else if (acpi_tb_system_table_pointer (object)) {
			/*
			 * Object points into the AML stream.
			 * Set a flag bit in the NTE to indicate this
			 */

			flags |= NTE_AML_ATTACHMENT;

			/*
			 * The next byte (perhaps the next two bytes)
			 * will be the AML opcode
			 */

			MOVE_UNALIGNED16_TO_16 (&opcode, object);

			/* Check for a recognized Op_code */

			switch ((u8) opcode)
			{

			case AML_OP_PREFIX:

				if (opcode != AML_REVISION_OP) {
					/*
					 * Op_prefix is unrecognized unless part
					 * of Revision_op
					 */

					break;
				}

				/* Else fall through to set type as Number */


			case AML_ZERO_OP: case AML_ONES_OP: case AML_ONE_OP:
			case AML_BYTE_OP: case AML_WORD_OP: case AML_DWORD_OP:

				obj_type = ACPI_TYPE_NUMBER;
				break;


			case AML_STRING_OP:

				obj_type = ACPI_TYPE_STRING;
				break;


			case AML_BUFFER_OP:

				obj_type = ACPI_TYPE_BUFFER;
				break;


			case AML_MUTEX_OP:

				obj_type = ACPI_TYPE_MUTEX;
				break;


			case AML_PACKAGE_OP:

				obj_type = ACPI_TYPE_PACKAGE;
				break;


			default:

				return (AE_TYPE);
				break;
			}
		}

		else {
			/*
			 * Cannot figure out the type -- set to Def_any which
			 * will print as an error in the name table dump
			 */


			obj_type = INTERNAL_TYPE_DEF_ANY;
		}
	}


	/*
	 * Must increment the new value's reference count
	 * (if it is an internal object)
	 */

	acpi_cm_add_reference (obj_desc);

	/* Save the existing object (if any) for deletion later */

	previous_obj_desc = this_entry->object;

	/* Install the object and set the type, flags */

	this_entry->object  = obj_desc;
	this_entry->type    = (u8) obj_type;
	this_entry->flags   = flags;


	/*
	 * Delete an existing attached object.
	 */

	if (previous_obj_desc) {
		/* One for the attach to the NTE */
		acpi_cm_remove_reference (previous_obj_desc);
		/* Now delete */
		acpi_cm_remove_reference (previous_obj_desc);
	}

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_attach_method
 *
 * PARAMETERS:  Handle              - Handle of nte to be set
 *              Offset              - Value to be set
 *              Length              - Length associated with value
 *
 * DESCRIPTION: Record the given offset and p-code length of the method
 *              whose handle is passed
 *
 * MUTEX:       Assumes namespace is locked
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_attach_method (
	ACPI_HANDLE             handle,
	u8                      *pcode_addr,
	u32                     pcode_length)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_OBJECT_INTERNAL    *previous_obj_desc;
	ACPI_NAMED_OBJECT       *this_entry = (ACPI_NAMED_OBJECT*) handle;


	/* Parameter validation */

	if (!acpi_gbl_root_object->child_table) {
		/* Name space uninitialized */

		REPORT_ERROR ("Ns_attach_method: name space uninitialized");
		return (AE_NO_NAMESPACE);
	}

	if (!handle) {
		/* Null name handle */

		REPORT_ERROR ("Ns_attach_method: null name handle");
		return (AE_BAD_PARAMETER);
	}


	/* Allocate a method descriptor */

	obj_desc = acpi_cm_create_internal_object (ACPI_TYPE_METHOD);
	if (!obj_desc) {
		/* Method allocation failure  */

		REPORT_ERROR ("Ns_attach_method: allocation failure");
		return (AE_NO_MEMORY);
	}

	/* Init the method info */

	obj_desc->method.pcode      = pcode_addr;
	obj_desc->method.pcode_length = pcode_length;

	/* Update reference count and install */

	acpi_cm_add_reference (obj_desc);

	previous_obj_desc = this_entry->object;
	this_entry->object = obj_desc;


	/*
	 * Delete an existing object.  Don't try to re-use in case it is shared
	 */
	if (previous_obj_desc) {
		acpi_cm_remove_reference (previous_obj_desc);
	}

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_detach_object
 *
 * PARAMETERS:  Object           - An object whose Value will be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete the Value associated with a namespace object.  If the
 *              Value is an allocated object, it is freed.  Otherwise, the
 *              field is simply cleared.
 *
 ***************************************************************************/

void
acpi_ns_detach_object (
	ACPI_HANDLE             object)
{
	ACPI_NAMED_OBJECT       *entry = object;
	ACPI_OBJECT_INTERNAL    *obj_desc;


	obj_desc = entry->object;
	if (!obj_desc) {
		return;
	}

	/* Clear the entry in all cases */

	entry->object = NULL;

	/* Found a valid value */

	/*
	 * Not every value is an object allocated via Acpi_cm_callocate,
	 * - must check
	 */

	if (!acpi_tb_system_table_pointer (obj_desc)) {
		/* Attempt to delete the object (and all subobjects) */

		acpi_cm_remove_reference (obj_desc);
	}

	return;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_attached_object
 *
 * PARAMETERS:  Handle              - Handle of nte to be examined
 *
 * RETURN:      Current value of the object field from nte whose handle is
 *              passed
 *
 ***************************************************************************/

void *
acpi_ns_get_attached_object (
	ACPI_HANDLE             handle)
{

	if (!handle) {
		/* handle invalid */

		REPORT_WARNING ("Ns_get_attached_object: Null handle");
		return (NULL);
	}

	return (((ACPI_NAMED_OBJECT*) handle)->object);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_compare_object
 *
 * PARAMETERS:  Obj_handle          - A namespace entry
 *              Level               - Current nesting level
 *              Obj_desc            - The value to be compared
 *
 * DESCRIPTION: A User_function called by Acpi_ns_walk_namespace(). It performs
 *              a comparison for Acpi_ns_find_attached_object(). The comparison is against
 *              the value in the value field of the Obj_handle (an NTE).
 *              If a match is found, the handle is returned, which aborts
 *              Acpi_ns_walk_namespace.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_compare_object (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *obj_desc,
	void                    **return_value)
{

	if (((ACPI_NAMED_OBJECT*) obj_handle)->object == obj_desc) {
		if (return_value) {
			*return_value = obj_handle;
		}

		 /* Stop the walk */
		return AE_CTRL_TERMINATE;
	}

	/* Not found, continue the walk */
	return AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_find_attached_object
 *
 * PARAMETERS:  *Obj_desc           - Value to be found in ptr_val field.
 *              Start_handle        - Root of subtree to be searched, or
 *                                    NS_ALL to search the entire namespace
 *              Max_depth           - Maximum depth of search.  Use INT_MAX
 *                                    for an effectively unlimited depth.
 *
 * DESCRIPTION: Traverse the name space until finding a name whose Value field
 *              matches the Obj_desc parameter, and return a handle to that
 *              name, or (ACPI_HANDLE)0 if none exists.
 *              if Start_handle is NS_ALL (null) search from the root,
 *              else it is a handle whose children are to be searched.
 *
 ***************************************************************************/

ACPI_HANDLE
acpi_ns_find_attached_object (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	ACPI_HANDLE             start_handle,
	s32                     max_depth)
{
	ACPI_HANDLE             ret_object;
	ACPI_STATUS             status;


	/* Parameter validation */

	if (!obj_desc) {
		return (NULL);
	}

	if (0 == max_depth) {
		return (NULL);
	}

	if (!acpi_gbl_root_object->child_table) {
		/*
		 * If the name space has not been initialized,
		 * there surely are no matching values.
		 */
		return (NULL);
	}

	if (NS_ALL == start_handle) {
		start_handle = acpi_gbl_root_object;
	}

	else {
		/*
		 * If base is not the root and has no children,
		 * there is nothing to search.
		 */
		return (NULL);
	}


	/*
	 * Walk namespace until a match is found.
	 * Either the matching object is returned, or NULL in case
	 * of no match.
	 */
	status = acpi_ns_walk_namespace (ACPI_TYPE_ANY, start_handle,
			   max_depth, NS_WALK_NO_UNLOCK,
			   acpi_ns_compare_object,
			   obj_desc, &ret_object);

	if (ACPI_FAILURE (status)) {
		ret_object = NULL;
	}

	return (ret_object);
}


