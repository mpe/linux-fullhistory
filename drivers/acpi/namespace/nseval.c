
/******************************************************************************
 *
 * Module Name: nseval - Object evaluation interfaces -- includes control
 *                       method lookup and execution.
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
#include "parser.h"
#include "interp.h"
#include "namesp.h"


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nseval");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_relative
 *
 * PARAMETERS:  Rel_obj_entry       - NTE of the relative containing object
 *              *Pathname           - Name of method to execute, If NULL, the
 *                                    handle is the object to execute
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and execute the requested method using the handle as a
 *              scope
 *
 * MUTEX:       Locks Namespace
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_evaluate_relative (
	ACPI_NAMED_OBJECT       *handle,
	char                    *pathname,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object)
{
	ACPI_NAMED_OBJECT       *rel_obj_entry;
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *obj_entry = NULL;
	char                    *internal_path = NULL;
	ACPI_GENERIC_STATE      scope_info;


	/*
	 * Must have a valid object handle
	 */
	if (!handle) {
		return (AE_BAD_PARAMETER);
	}

	/* Build an internal name string for the method */

	status = acpi_ns_internalize_name (pathname, &internal_path);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Get the prefix handle and NTE */

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	rel_obj_entry = acpi_ns_convert_handle_to_entry (handle);
	if (!rel_obj_entry) {
		acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Lookup the name in the namespace */

	scope_info.scope.name_table = rel_obj_entry->child_table;
	status = acpi_ns_lookup (&scope_info, internal_path, ACPI_TYPE_ANY,
			 IMODE_EXECUTE,
			 NS_NO_UPSEARCH, NULL,
			 &obj_entry);

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

	if (status != AE_OK) {
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt
	 * to evaluate it.
	 */

	status = acpi_ns_evaluate_by_handle (obj_entry, params, return_object);

cleanup:

	/* Cleanup */

	acpi_cm_free (internal_path);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_by_name
 *
 * PARAMETERS:  Pathname            - Fully qualified pathname to the object
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and execute the requested method passing the given
 *              parameters
 *
 * MUTEX:       Locks Namespace
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_evaluate_by_name (
	char                    *pathname,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object)
{
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *obj_entry = NULL;
	char                    *internal_path = NULL;


	/* Build an internal name string for the method */

	if (pathname[0] != '\\' || pathname[1] != '/') {
		status = acpi_ns_internalize_name (pathname, &internal_path);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup (NULL, internal_path, ACPI_TYPE_ANY,
			 IMODE_EXECUTE,
			 NS_NO_UPSEARCH, NULL,
			 &obj_entry);

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

	if (status != AE_OK) {
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt
	 * to evaluate it.
	 */

	status = acpi_ns_evaluate_by_handle (obj_entry, params, return_object);


cleanup:

	/* Cleanup */

	if (internal_path) {
		acpi_cm_free (internal_path);
	}

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_by_handle
 *
 * PARAMETERS:  Obj_entry           - NTE of method to execute
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the requested method passing the given parameters
 *
 * MUTEX:       Locks Namespace
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_evaluate_by_handle (
	ACPI_NAMED_OBJECT       *handle,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object)
{
	ACPI_NAMED_OBJECT       *obj_entry;
	ACPI_STATUS             status;
	ACPI_OBJECT_INTERNAL    *local_return_object;


	/* Check if namespace has been initialized */

	if (!acpi_gbl_root_object->child_table) {
		return (AE_NO_NAMESPACE);
	}

	/* Parameter Validation */

	if (!handle) {
		return (AE_BAD_PARAMETER);
	}

	if (return_object) {
		/* Initialize the return value to an invalid object */

		*return_object = NULL;
	}

	/* Get the prefix handle and NTE */

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	obj_entry = acpi_ns_convert_handle_to_entry (handle);
	if (!obj_entry) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}


	/*
	 * Two major cases here:
	 * 1) The object is an actual control method -- execute it.
	 * 2) The object is not a method -- just return it's current
	 *      value
	 *
	 * In both cases, the namespace is unlocked by the
	 *  Acpi_ns* procedure
	 */

	if (acpi_ns_get_type (obj_entry) == ACPI_TYPE_METHOD) {
		/*
		 * Case 1) We have an actual control method to execute
		 */

		status = acpi_ns_execute_control_method (obj_entry,
				 params,
				 &local_return_object);
	}

	else {
		/*
		 * Case 2) Object is NOT a method, just return its
		 * current value
		 */

		status = acpi_ns_get_object_value (obj_entry,
				   &local_return_object);
	}


	/*
	 * Check if there is a return value on the stack that must
	 * be dealt with
	 */

	if (status == AE_CTRL_RETURN_VALUE) {
		/*
		 * If the Method returned a value and the caller
		 * provided a place to store a returned value, Copy
		 * the returned value to the object descriptor provided
		 * by the caller.
		 */

		if (return_object) {
			/*
			 * Valid return object, copy the pointer to
			 * the returned object
			 */

			*return_object = local_return_object;
		}


		/* Map AE_RETURN_VALUE to AE_OK, we are done with it */

		if (status == AE_CTRL_RETURN_VALUE) {
			status = AE_OK;
		}
	}

	/*
	 * Namespace was unlocked by the handling Acpi_ns* function,
	 * so we just return
	 */

	return (status);


unlock_and_exit:

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_execute_control_method
 *
 * PARAMETERS:  Method_entry        - The Nte of the object/method
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the requested method passing the given parameters
 *
 * MUTEX:       Assumes namespace is locked
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_execute_control_method (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_obj_desc)
{
	ACPI_STATUS             status;
	ACPI_OBJECT_INTERNAL    *obj_desc;


	/* Verify that there is a method associated with this object */

	obj_desc = acpi_ns_get_attached_object ((ACPI_HANDLE) method_entry);
	if (!obj_desc) {
		return (AE_ERROR);
	}

	/*
	 * Valid method, Set the current scope to that of the Method,
	 * and execute it.
	 */


	/*
	 * Unlock the namespace before execution.  This allows namespace access
	 * via the external Acpi* interfaces while a method is being executed.
	 * However, any namespace deletion must acquire both the namespace and
	 * interpter locks to ensure that no thread is using the portion of the
	 * namespace that is being deleted.
	 */

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

	/*
	 * Excecute the method via the interpreter
	 */
	status = acpi_aml_execute_method (method_entry, params, return_obj_desc);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_object_value
 *
 * PARAMETERS:  Object_entry        - The Nte of the object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the current value of the object
 *
 * MUTEX:       Assumes namespace is locked
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_get_object_value (
	ACPI_NAMED_OBJECT       *object_entry,
	ACPI_OBJECT_INTERNAL    **return_obj_desc)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_OBJECT_INTERNAL    *val_desc;


	/*
	 *  We take the value from certain objects directly
	 */

	if ((object_entry->type == ACPI_TYPE_PROCESSOR) ||
		(object_entry->type == ACPI_TYPE_POWER))
	{

		/*
		 *  Create a Reference object to contain the object
		 */
		obj_desc = acpi_cm_create_internal_object (object_entry->type);
		if (!obj_desc) {
		   status = AE_NO_MEMORY;
		   goto unlock_and_exit;
		}

		/*
		 *  Get the attached object
		 */

		val_desc = acpi_ns_get_attached_object (object_entry);
		if (!val_desc) {
			status = AE_NULL_OBJECT;
			goto unlock_and_exit;
		}

		/*
		 *  Just copy from the original to the return object
		 */

		MEMCPY (&obj_desc->common.first_non_common_byte,
				&val_desc->common.first_non_common_byte,
				(sizeof(ACPI_OBJECT_COMMON) -
				sizeof(obj_desc->common.first_non_common_byte)));
	}


	/*
	 * Other objects require a reference object wrapper which we
	 * then attempt to resolve.
	 */
	else {
		/* Create an Reference object to contain the object */

		obj_desc = acpi_cm_create_internal_object (INTERNAL_TYPE_REFERENCE);
		if (!obj_desc) {
		   status = AE_NO_MEMORY;
		   goto unlock_and_exit;
		}

		/* Construct a descriptor pointing to the name */

		obj_desc->reference.op_code = (u8) AML_NAME_OP;
		obj_desc->reference.object = (void *) object_entry;

		/*
		 * Use Acpi_aml_resolve_to_value() to get the associated value.
		 * The call to Acpi_aml_resolve_to_value causes
		 * Obj_desc (allocated above) to always be deleted.
		 */

		status = acpi_aml_resolve_to_value (&obj_desc);
	}

	/*
	 * If Acpi_aml_resolve_to_value() succeeded, the return value was
	 * placed in Obj_desc.
	 */

	if (status == AE_OK) {
		status = AE_CTRL_RETURN_VALUE;

		*return_obj_desc = obj_desc;
	}


unlock_and_exit:

	/* Unlock the namespace */

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}
