
/******************************************************************************
 *
 * Module Name: dswload - Dispatcher namespace load callbacks
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
#include "parser.h"
#include "amlcode.h"
#include "dispatch.h"
#include "interp.h"
#include "namesp.h"
#include "events.h"


#define _COMPONENT          DISPATCHER
	 MODULE_NAME         ("dswload");


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_load1_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been reached in the
 *                                walk;  Arguments have not been evaluated yet.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_load1_begin_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_NAMED_OBJECT       *new_entry;
	ACPI_STATUS             status;
	OBJECT_TYPE_INTERNAL    data_type;


	/* We are only interested in opcodes that have an associated name */

	if (!acpi_ps_is_named_op (op->opcode)) {
		return AE_OK;
	}


	/* Check if this object has already been installed in the namespace */

	if (op->acpi_named_object) {
		return AE_OK;
	}

	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

	/* Attempt to type a NAME opcode by examining the argument */

	/* TBD: [Investigate] is this the right place to do this? */

	if (op->opcode == AML_NAME_OP) {
		if (op->value.arg) {

			data_type = acpi_ds_map_opcode_to_data_type ((op->value.arg)->opcode,
					  NULL);
		}
	}


	/*
	 * Enter the named type into the internal namespace.  We enter the name
	 * as we go downward in the parse tree.  Any necessary subobjects that involve
	 * arguments to the opcode must be created as we go back up the parse tree later.
	 */
	status = acpi_ns_lookup (walk_state->scope_info,
			 (char *) &((ACPI_NAMED_OP *)op)->name,
			 data_type, IMODE_LOAD_PASS1,
			 NS_NO_UPSEARCH, walk_state, &(new_entry));

	if (ACPI_SUCCESS (status)) {
		/*
		 * Put the NTE in the "op" object that the parser uses, so we
		 * can get it again quickly when this scope is closed
		 */
		op->acpi_named_object = new_entry;
	}

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_load1_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_load1_end_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	OBJECT_TYPE_INTERNAL    data_type;


	/* We are only interested in opcodes that have an associated name */

	if (!acpi_ps_is_named_op (op->opcode)) {
		return AE_OK;
	}

	/* TBD: [Investigate] can this be removed? */

	if (op->opcode == AML_SCOPE_OP) {
		if (((ACPI_NAMED_OP *)op)->name == -1) {
			return AE_OK;
		}
	}


	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (data_type)) {

		acpi_ds_scope_stack_pop (walk_state);
	}

	return AE_OK;

}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_load2_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been reached in the
 *                                walk;  Arguments have not been evaluated yet.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_load2_begin_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_NAMED_OBJECT       *new_entry;
	ACPI_STATUS             status;
	OBJECT_TYPE_INTERNAL    data_type;
	char                    *buffer_ptr;
	void                    *original = NULL;


	/* We only care about Namespace opcodes here */

	if (!acpi_ps_is_namespace_op (op->opcode) &&
		op->opcode != AML_NAMEPATH_OP)
	{
		return AE_OK;
	}


	/*
	 * Get the name we are going to enter or lookup in the namespace
	 */
	if (op->opcode == AML_NAMEPATH_OP) {
		/* For Namepath op , get the path string */

		buffer_ptr = op->value.string;
		if (!buffer_ptr) {
			/* No name, just exit */

			return AE_OK;
		}
	}

	else {
		/* Get name from the op */

		buffer_ptr = (char *) &((ACPI_NAMED_OP *)op)->name;
	}


	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);


	if (op->opcode == AML_DEF_FIELD_OP      ||
		op->opcode == AML_BANK_FIELD_OP     ||
		op->opcode == AML_INDEX_FIELD_OP)
	{
		new_entry = NULL;
		status = AE_OK;
	}

	else if (op->opcode == AML_NAMEPATH_OP) {
		/*
		 * The Name_path is an object reference to an existing object. Don't enter the
		 * name into the namespace, but look it up for use later
		 */
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr,
				 data_type, IMODE_EXECUTE,
				 NS_SEARCH_PARENT, walk_state,
				 &(new_entry));
	}

	else {
		if (op->acpi_named_object) {
			original = op->acpi_named_object;
			new_entry = op->acpi_named_object;

			if (acpi_ns_opens_scope (data_type)) {
				status = acpi_ds_scope_stack_push (new_entry->child_table,
						   data_type,
						   walk_state);
				if (ACPI_FAILURE (status)) {
					return (status);
				}

			}
			return AE_OK;
		}

		/*
		 * Enter the named type into the internal namespace.  We enter the name
		 * as we go downward in the parse tree.  Any necessary subobjects that involve
		 * arguments to the opcode must be created as we go back up the parse tree later.
		 */
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr,
				 data_type, IMODE_EXECUTE,
				 NS_NO_UPSEARCH, walk_state,
				 &(new_entry));
	}

	if (ACPI_SUCCESS (status)) {
		/*
		 * Put the NTE in the "op" object that the parser uses, so we
		 * can get it again quickly when this scope is closed
		 */
		op->acpi_named_object = new_entry;

	}


	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_load2_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_load2_end_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_STATUS             status = AE_OK;
	OBJECT_TYPE_INTERNAL    data_type;
	ACPI_NAMED_OBJECT       *entry;
	ACPI_GENERIC_OP         *arg;
	ACPI_NAMED_OBJECT       *new_entry;


	if (!acpi_ps_is_namespace_object_op (op->opcode)) {
		return AE_OK;
	}

	if (op->opcode == AML_SCOPE_OP) {
		if (((ACPI_NAMED_OP *)op)->name == -1) {
			return AE_OK;
		}
	}


	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

	/*
	 * Get the NTE/name from the earlier lookup
	 * (It was saved in the *op structure)
	 */
	entry = op->acpi_named_object;

	/*
	 * Put the NTE on the object stack (Contains the ACPI Name of
	 * this object)
	 */

	walk_state->operands[0] = (void *) entry;
	walk_state->num_operands = 1;

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (data_type)) {

		acpi_ds_scope_stack_pop (walk_state);
	}


	/*
	 * Named operations are as follows:
	 *
	 * AML_SCOPE
	 * AML_DEVICE
	 * AML_THERMALZONE
	 * AML_METHOD
	 * AML_POWERRES
	 * AML_PROCESSOR
	 * AML_FIELD
	 * AML_INDEXFIELD
	 * AML_BANKFIELD
	 * AML_NAMEDFIELD
	 * AML_NAME
	 * AML_ALIAS
	 * AML_MUTEX
	 * AML_EVENT
	 * AML_OPREGION
	 * AML_CREATEFIELD
	 * AML_CREATEBITFIELD
	 * AML_CREATEBYTEFIELD
	 * AML_CREATEWORDFIELD
	 * AML_CREATEDWORDFIELD
	 * AML_METHODCALL
	 */


	/* Decode the opcode */

	arg = op->value.arg;

	switch (op->opcode)
	{

	case AML_CREATE_FIELD_OP:
	case AML_BIT_FIELD_OP:
	case AML_BYTE_FIELD_OP:
	case AML_WORD_FIELD_OP:
	case AML_DWORD_FIELD_OP:

		/* Get the Name_string argument */

		if (op->opcode == AML_CREATE_FIELD_OP) {
			arg = acpi_ps_get_arg (op, 3);
		}
		else {
			/* Create Bit/Byte/Word/Dword field */

			arg = acpi_ps_get_arg (op, 2);
		}

		/*
		 * Enter the Name_string into the namespace
		 */

		status = acpi_ns_lookup (walk_state->scope_info,
				 arg->value.string,
				 INTERNAL_TYPE_DEF_ANY,
				 IMODE_LOAD_PASS1,
				 NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
				 walk_state, &(new_entry));

		if (ACPI_SUCCESS (status)) {
			/* We could put the returned object (NTE) on the object stack for later, but
			 * for now, we will put it in the "op" object that the parser uses, so we
			 * can get it again at the end of this scope
			 */
			op->acpi_named_object = new_entry;
		}

		break;


	case AML_METHODCALL_OP:

		/*
		 * Lookup the method name and save the NTE
		 */

		status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
				 ACPI_TYPE_ANY, IMODE_LOAD_PASS2,
				 NS_SEARCH_PARENT | NS_DONT_OPEN_SCOPE,
				 walk_state, &(new_entry));

		if (ACPI_SUCCESS (status)) {

/* has name already been resolved by here ??*/

			/* TBD: [Restructure] Make sure that what we found is indeed a method! */
			/* We didn't search for a method on purpose, to see if the name would resolve! */

			/* We could put the returned object (NTE) on the object stack for later, but
			 * for now, we will put it in the "op" object that the parser uses, so we
			 * can get it again at the end of this scope
			 */
			op->acpi_named_object = new_entry;
		}


		 break;


	case AML_PROCESSOR_OP:

		/* Nothing to do other than enter object into namespace */

		status = acpi_aml_exec_create_processor (op, (ACPI_HANDLE) entry);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		break;


	case AML_POWER_RES_OP:

		/* Nothing to do other than enter object into namespace */

		status = acpi_aml_exec_create_power_resource (op, (ACPI_HANDLE) entry);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		break;


	case AML_THERMAL_ZONE_OP:

		/* Nothing to do other than enter object into namespace */

		break;


	case AML_DEF_FIELD_OP:

		arg = op->value.arg;

		status = acpi_ds_create_field (op,
				  (ACPI_HANDLE) arg->acpi_named_object,
				  walk_state);
		break;


	case AML_INDEX_FIELD_OP:

		arg = op->value.arg;

		status = acpi_ds_create_index_field (op,
				   (ACPI_HANDLE) arg->acpi_named_object,
				   walk_state);
		break;


	case AML_BANK_FIELD_OP:

		arg = op->value.arg;
		status = acpi_ds_create_bank_field (op,
				   (ACPI_HANDLE) arg->acpi_named_object,
				   walk_state);
		break;


	/*
	 * Method_op Pkg_length Names_string Method_flags Term_list
	 */
	case AML_METHOD_OP:

		if (!entry->object) {
			status = acpi_aml_exec_create_method (((ACPI_DEFERRED_OP *) op)->body,
					   ((ACPI_DEFERRED_OP *) op)->body_length,
					   arg->value.integer, (ACPI_HANDLE) entry);
		}

		break;


	case AML_MUTEX_OP:

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_aml_exec_create_mutex (walk_state);
		break;


	case AML_EVENT_OP:

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_aml_exec_create_event (walk_state);
		break;


	case AML_REGION_OP:


		/*
		 * The Op_region is not fully parsed at this time. Only valid argument is the Space_id.
		 * (We must save the address of the AML of the address and length operands)
		 */

		status = acpi_aml_exec_create_region (((ACPI_DEFERRED_OP *) op)->body,
				   ((ACPI_DEFERRED_OP *) op)->body_length,
				   arg->value.integer, walk_state);

		break;


	/* Namespace Modifier Opcodes */

	case AML_ALIAS_OP:

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_aml_exec_create_alias (walk_state);
		break;


	case AML_NAME_OP:

		status = acpi_ds_create_named_object (walk_state, entry, op);

		break;


	case AML_NAMEPATH_OP:

		break;


	default:
		break;
	}


cleanup:
	/* Remove the NTE pushed at the very beginning */
	acpi_ds_obj_stack_pop (1, walk_state);
	return (status);
}

