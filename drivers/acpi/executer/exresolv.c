
/******************************************************************************
 *
 * Module Name: exresolv - AML Interpreter object resolution
 *              $Revision: 118 $
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
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exresolv")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_to_value
 *
 * PARAMETERS:  **Stack_ptr         - Points to entry on Obj_stack, which can
 *                                    be either an (acpi_operand_object *)
 *                                    or an acpi_handle.
 *              Walk_state          - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert Reference objects to values
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ex_resolve_to_value", stack_ptr);


	if (!stack_ptr || !*stack_ptr) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null pointer\n"));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/*
	 * The entity pointed to by the Stack_ptr can be either
	 * 1) A valid acpi_operand_object, or
	 * 2) A acpi_namespace_node (Named_obj)
	 */
	if (ACPI_GET_DESCRIPTOR_TYPE (*stack_ptr) == ACPI_DESC_TYPE_OPERAND) {
		status = acpi_ex_resolve_object_to_value (stack_ptr, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Object on the stack may have changed if Acpi_ex_resolve_object_to_value()
	 * was called (i.e., we can't use an _else_ here.)
	 */
	if (ACPI_GET_DESCRIPTOR_TYPE (*stack_ptr) == ACPI_DESC_TYPE_NAMED) {
		status = acpi_ex_resolve_node_to_value (
				  ACPI_CAST_INDIRECT_PTR (acpi_namespace_node, stack_ptr),
				  walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Resolved object %p\n", *stack_ptr));
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_object_to_value
 *
 * PARAMETERS:  Stack_ptr       - Pointer to a stack location that contains a
 *                                ptr to an internal object.
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value from an internal object.  The Reference type
 *              uses the associated AML opcode to determine the value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_object_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *stack_desc;
	void                    *temp_node;
	acpi_operand_object     *obj_desc;
	u16                     opcode;


	ACPI_FUNCTION_TRACE ("Ex_resolve_object_to_value");


	stack_desc = *stack_ptr;

	/* This is an acpi_operand_object  */

	switch (ACPI_GET_OBJECT_TYPE (stack_desc)) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		opcode = stack_desc->reference.opcode;

		switch (opcode) {
		case AML_NAME_OP:

			/*
			 * Convert indirect name ptr to a direct name ptr.
			 * Then, Acpi_ex_resolve_node_to_value can be used to get the value
			 */
			temp_node = stack_desc->reference.object;

			/* Delete the Reference Object */

			acpi_ut_remove_reference (stack_desc);

			/* Put direct name pointer onto stack and exit */

			(*stack_ptr) = temp_node;
			break;


		case AML_LOCAL_OP:
		case AML_ARG_OP:

			/*
			 * Get the local from the method's state info
			 * Note: this increments the local's object reference count
			 */
			status = acpi_ds_method_data_get_value (opcode,
					  stack_desc->reference.offset, walk_state, &obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Now we can delete the original Reference Object and
			 * replace it with the resolve value
			 */
			acpi_ut_remove_reference (stack_desc);
			*stack_ptr = obj_desc;

			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Arg/Local %d] Value_obj is %p\n",
				stack_desc->reference.offset, obj_desc));
			break;


		case AML_INDEX_OP:

			switch (stack_desc->reference.target_type) {
			case ACPI_TYPE_BUFFER_FIELD:

				/* Just return - leave the Reference on the stack */
				break;


			case ACPI_TYPE_PACKAGE:

				obj_desc = *stack_desc->reference.where;
				if (obj_desc) {
					/*
					 * Valid obj descriptor, copy pointer to return value
					 * (i.e., dereference the package index)
					 * Delete the ref object, increment the returned object
					 */
					acpi_ut_remove_reference (stack_desc);
					acpi_ut_add_reference (obj_desc);
					*stack_ptr = obj_desc;
				}
				else {
					/*
					 * A NULL object descriptor means an unitialized element of
					 * the package, can't dereference it
					 */
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Attempt to deref an Index to NULL pkg element Idx=%p\n",
						stack_desc));
					status = AE_AML_UNINITIALIZED_ELEMENT;
				}
				break;


			default:

				/* Invalid reference object */

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Unknown Target_type %X in Index/Reference obj %p\n",
					stack_desc->reference.target_type, stack_desc));
				status = AE_AML_INTERNAL;
				break;
			}
			break;


		case AML_REF_OF_OP:
		case AML_DEBUG_OP:

			/* Just leave the object as-is */

			break;


		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Reference opcode %X in %p\n",
				opcode, stack_desc));
			status = AE_AML_INTERNAL;
			break;
		}
		break;


	case ACPI_TYPE_BUFFER:

		status = acpi_ds_get_buffer_arguments (stack_desc);
		break;


	case ACPI_TYPE_PACKAGE:

		status = acpi_ds_get_package_arguments (stack_desc);
		break;


	/*
	 * These cases may never happen here, but just in case..
	 */
	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Field_read Source_desc=%p Type=%X\n",
			stack_desc, ACPI_GET_OBJECT_TYPE (stack_desc)));

		status = acpi_ex_read_data_from_field (walk_state, stack_desc, &obj_desc);
		*stack_ptr = (void *) obj_desc;
		break;

	default:
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_multiple
 *
 * PARAMETERS:  Walk_state          - Current state (contains AML opcode)
 *              Operand             - Starting point for resolution
 *              Return_type         - Where the object type is returned
 *              Return_desc         - Where the resolved object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the base object and type.  Traverse a reference list if
 *              necessary to get to the base object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_multiple (
	acpi_walk_state         *walk_state,
	acpi_operand_object     *operand,
	acpi_object_type        *return_type,
	acpi_operand_object     **return_desc)
{
	acpi_operand_object     *obj_desc = (void *) operand;
	acpi_namespace_node     *node;
	acpi_object_type        type;


	ACPI_FUNCTION_TRACE ("Acpi_ex_resolve_multiple");


	/*
	 * For reference objects created via the Ref_of or Index operators,
	 * we need to get to the base object (as per the ACPI specification
	 * of the Object_type and Size_of operators). This means traversing
	 * the list of possibly many nested references.
	 */
	while (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_LOCAL_REFERENCE) {
		switch (obj_desc->reference.opcode) {
		case AML_REF_OF_OP:

			/* Dereference the reference pointer */

			node = obj_desc->reference.object;

			/* All "References" point to a NS node */

			if (ACPI_GET_DESCRIPTOR_TYPE (node) != ACPI_DESC_TYPE_NAMED) {
				return_ACPI_STATUS (AE_AML_INTERNAL);
			}

			/* Get the attached object */

			obj_desc = acpi_ns_get_attached_object (node);
			if (!obj_desc) {
				/* No object, use the NS node type */

				type = acpi_ns_get_type (node);
				goto exit;
			}

			/* Check for circular references */

			if (obj_desc == operand) {
				return_ACPI_STATUS (AE_AML_CIRCULAR_REFERENCE);
			}
			break;


		case AML_INDEX_OP:

			/* Get the type of this reference (index into another object) */

			type = obj_desc->reference.target_type;
			if (type != ACPI_TYPE_PACKAGE) {
				goto exit;
			}

			/*
			 * The main object is a package, we want to get the type
			 * of the individual package element that is referenced by
			 * the index.
			 *
			 * This could of course in turn be another reference object.
			 */
			obj_desc = *(obj_desc->reference.where);
			break;


		case AML_INT_NAMEPATH_OP:

			/* Dereference the reference pointer */

			node = obj_desc->reference.node;

			/* All "References" point to a NS node */

			if (ACPI_GET_DESCRIPTOR_TYPE (node) != ACPI_DESC_TYPE_NAMED) {
				return_ACPI_STATUS (AE_AML_INTERNAL);
			}

			/* Get the attached object */

			obj_desc = acpi_ns_get_attached_object (node);
			if (!obj_desc) {
				/* No object, use the NS node type */

				type = acpi_ns_get_type (node);
				goto exit;
			}

			/* Check for circular references */

			if (obj_desc == operand) {
				return_ACPI_STATUS (AE_AML_CIRCULAR_REFERENCE);
			}
			break;


		case AML_DEBUG_OP:

			/* The Debug Object is of type "Debug_object" */

			type = ACPI_TYPE_DEBUG_OBJECT;
			goto exit;


		default:

			ACPI_REPORT_ERROR (("Acpi_ex_resolve_multiple: Unknown Reference subtype %X\n",
				obj_desc->reference.opcode));
			return_ACPI_STATUS (AE_AML_INTERNAL);
		}
	}

	/*
	 * Now we are guaranteed to have an object that has not been created
	 * via the Ref_of or Index operators.
	 */
	type = ACPI_GET_OBJECT_TYPE (obj_desc);


exit:
	/* Convert internal types to external types */

	switch (type) {
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		type = ACPI_TYPE_FIELD_UNIT;
		break;

	case ACPI_TYPE_LOCAL_SCOPE:

		/* Per ACPI Specification, Scope is untyped */

		type = ACPI_TYPE_ANY;
		break;

	default:
		/* No change to Type required */
		break;
	}

	*return_type = type;
	if (return_desc) {
		*return_desc = obj_desc;
	}
	return_ACPI_STATUS (AE_OK);
}


