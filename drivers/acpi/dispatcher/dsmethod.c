
/******************************************************************************
 *
 * Module Name: dsmethod - Parser/Interpreter interface - control method parsing
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
#include "tables.h"
#include "debugger.h"


#define _COMPONENT          DISPATCHER
	 MODULE_NAME         ("dsmethod");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_parse_method
 *
 * PARAMETERS:  Obj_handle      - NTE of the method
 *              Level           - Current nesting level
 *              Context         - Points to a method counter
 *              Return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Call the parser and parse the AML that is
 *              associated with the method.
 *
 * MUTEX:       Assumes parser is locked
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_parse_method (
	ACPI_HANDLE             obj_handle)
{
	ACPI_STATUS             status;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_GENERIC_OP         *op;
	ACPI_NAMED_OBJECT       *entry;
	ACPI_OWNER_ID           owner_id;


	/* Parameter Validation */

	if (!obj_handle) {
		return (AE_NULL_ENTRY);
	}


	/* Extract the method object from the method NTE */

	entry = (ACPI_NAMED_OBJECT*) obj_handle;
	obj_desc = entry->object;
	if (!obj_desc) {
		return (AE_NULL_OBJECT);
	}

	 /* Create a mutex for the method if there is a concurrency limit */

	if ((obj_desc->method.concurrency != INFINITE_CONCURRENCY) &&
		(!obj_desc->method.semaphore))
	{
		status = acpi_os_create_semaphore (1,
				   obj_desc->method.concurrency,
				   &obj_desc->method.semaphore);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}

	/*
	 * Allocate a new parser op to be the root of the parsed
	 * method tree
	 */

	op = acpi_ps_alloc_op (AML_METHOD_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Init new op with the method name and pointer back to the NTE */

	acpi_ps_set_name (op, entry->name);
	op->acpi_named_object = entry;


	/*
	 * Parse the method, creating a parse tree.
	 *
	 * The parse also includes a first pass load of the
	 * namespace where newly declared named objects are
	 * added into the namespace.  Actual evaluation of
	 * the named objects (what would be called a "second
	 * pass") happens during the actual execution of the
	 * method so that operands to the named objects can
	 * take on dynamic run-time values.
	 */

	status = acpi_ps_parse_aml (op, obj_desc->method.pcode,
			  obj_desc->method.pcode_length, 0);

	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Get a new Owner_id for objects created by this method */

	owner_id = acpi_cm_allocate_owner_id (OWNER_TYPE_METHOD);

	/* Install the parsed tree in the method object */

	obj_desc->method.parser_op = op;
	obj_desc->method.owning_id = owner_id;

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_begin_method_execution
 *
 * PARAMETERS:  Method_entry        - NTE of the method
 *              Obj_desc            - The method object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare a method for execution.  Parses the method if necessary,
 *              increments the thread count, and waits at the method semaphore
 *              for clearance to execute.
 *
 * MUTEX:       Locks/unlocks parser.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_begin_method_execution (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    *obj_desc)
{
	ACPI_STATUS             status = AE_OK;


	if (!method_entry) {
		return (AE_NULL_ENTRY);
	}

	obj_desc = acpi_ns_get_attached_object (method_entry);
	if (!obj_desc) {
		return (AE_NULL_OBJECT);
	}

	/*
	 * Lock the parser while we check for and possibly parse the
	 * control method
	 */

	acpi_cm_acquire_mutex (ACPI_MTX_PARSER);


	/* If method is not parsed at this time, we must parse it first */

	if (!obj_desc->method.parser_op) {

		status = acpi_ds_parse_method (method_entry);
		if (ACPI_FAILURE (status)) {
			acpi_cm_release_mutex (ACPI_MTX_PARSER);
			return (status);
		}
	}


	/*
	 * Increment the method parse tree thread count since there
	 * is one additional thread executing in it.  If configured
	 * for deletion-on-exit, the parse tree will be deleted when
	 * the last thread completes execution of the method
	 */

	((ACPI_DEFERRED_OP *) obj_desc->method.parser_op)->thread_count++;

	/*
	 * Parsing is complete, we can unlock the parser.  Parse tree
	 * cannot be deleted at least until this thread completes.
	 */

	acpi_cm_release_mutex (ACPI_MTX_PARSER);

	/*
	 * If there is a concurrency limit on this method, we need to
	 * obtain a unit from the method semaphore.  This releases the
	 * interpreter if we block
	 */

	if (obj_desc->method.semaphore) {
		status = acpi_aml_system_wait_semaphore (obj_desc->method.semaphore,
				 WAIT_FOREVER);
	}


	return (status);

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_call_control_method
 *
 * PARAMETERS:  Walk_state          - Current state of the walk
 *              Op                  - Current Op to be walked
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfer execution to a called control method
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_call_control_method (
	ACPI_WALK_LIST          *walk_list,
	ACPI_WALK_STATE         *this_walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_STATUS             status;
	ACPI_DEFERRED_OP        *method;
	ACPI_NAMED_OBJECT       *method_entry;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_WALK_STATE         *next_walk_state;
	u32                     i;


	/*
	 * Prev_op points to the METHOD_CALL Op.
	 * Get the NTE entry (in the METHOD_CALL->NAME Op) and the
	 * corresponding METHOD Op
	 */

	method_entry = (this_walk_state->prev_op->value.arg)->acpi_named_object;
	if (!method_entry) {
		return (AE_NULL_ENTRY);
	}

	obj_desc = acpi_ns_get_attached_object (method_entry);
	if (!obj_desc) {
		return (AE_NULL_OBJECT);
	}

	/* Parse method if necessary, wait on concurrency semaphore */

	status = acpi_ds_begin_method_execution (method_entry, obj_desc);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Save the (current) Op for when this walk is restarted */

	this_walk_state->method_call_op = this_walk_state->prev_op;
	this_walk_state->prev_op    = op;
	method                      = obj_desc->method.parser_op;

	/* Create a new state for the preempting walk */

	next_walk_state = acpi_ds_create_walk_state (obj_desc->method.owning_id,
			  (ACPI_GENERIC_OP *) method,
			  obj_desc, walk_list);
	if (!next_walk_state) {
		return (AE_NO_MEMORY);
	}

	/* The Next_op of the Next_walk will be the beginning of the method */

	next_walk_state->next_op = (ACPI_GENERIC_OP *) method;

	/* Open a new scope */

	status = acpi_ds_scope_stack_push (method_entry->child_table,
			   ACPI_TYPE_METHOD, next_walk_state);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}


	/*
	 * Initialize the arguments for the method.  The resolved
	 * arguments were put on the previous walk state's operand
	 * stack.  Operands on the previous walk state stack always
	 * start at index 0.
	 */

	status = acpi_ds_method_data_init_args (&this_walk_state->operands[0],
			 this_walk_state->num_operands);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/*
	 * Delete the operands on the previous walkstate operand stack
	 * (they were copied to new objects)
	 */

	for (i = 0; i < obj_desc->method.param_count; i++) {
		acpi_cm_remove_reference (this_walk_state->operands [i]);
	}

	/* Clear the operand stack */

	this_walk_state->num_operands = 0;


	return (AE_OK);


	/* On error, we must delete the new walk state */

cleanup:
	acpi_ds_terminate_control_method (next_walk_state);
	acpi_ds_delete_walk_state (next_walk_state);
	return (status);

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_restart_control_method
 *
 * PARAMETERS:  Walk_state          - State of the method when it was preempted
 *              Op                  - Pointer to new current op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Restart a method that was preempted
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_restart_control_method (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    *return_desc)
{
	ACPI_STATUS             status;


	if (return_desc) {
		/*
		 * Get the return value (if any) from the previous method.
		 * NULL if no return value
		 */

		status = acpi_ds_result_stack_push (return_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			acpi_cm_remove_reference (return_desc);
			return (status);
		}

		/*
		 * Delete the return value if it will not be used by the
		 * calling method
		 */

		acpi_ds_delete_result_if_not_used (walk_state->method_call_op,
				   return_desc, walk_state);
	}


	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_terminate_control_method
 *
 * PARAMETERS:  Walk_state          - State of the method
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Terminate a control method.  Delete everything that the method
 *              created, delete all locals and arguments, and delete the parse
 *              tree if requested.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_terminate_control_method (
	ACPI_WALK_STATE         *walk_state)
{
	ACPI_STATUS             status;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_DEFERRED_OP        *op;
	ACPI_NAMED_OBJECT       *method_entry;


	/* The method object should be stored in the walk state */

	obj_desc = walk_state->method_desc;
	if (!obj_desc) {
		return (AE_OK);
	}

	/* Delete all arguments and locals */

	acpi_ds_method_data_delete_all (walk_state);

	/*
	 * Lock the parser while we terminate this method.
	 * If this is the last thread executing the method,
	 * we have additional cleanup to perform
	 */

	acpi_cm_acquire_mutex (ACPI_MTX_PARSER);

	/*
	 * The root of the method parse tree should be stored
	 * in the method object
	 */

	op = obj_desc->method.parser_op;
	if (!op) {
		goto unlock_and_exit;
	}

	/* Signal completion of the execution of this method if necessary */

	if (walk_state->method_desc->method.semaphore) {
		status = acpi_os_signal_semaphore (
			walk_state->method_desc->method.semaphore, 1);
	}

	/* Decrement the thread count on the method parse tree */

	op->thread_count--;
	if (!op->thread_count) {
		/*
		 * There are no more threads executing this method.  Perform
		 * additional cleanup.
		 *
		 * The method NTE is stored in the method Op
		 */
		method_entry = op->acpi_named_object;

		/*
		 * Delete any namespace entries created immediately underneath
		 * the method
		 */
		acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);
		if (method_entry->child_table) {
			acpi_ns_delete_namespace_subtree (method_entry);
		}

		/*
		 * Delete any namespace entries created anywhere else within
		 * the namespace
		 */

		acpi_ns_delete_namespace_by_owner (
				 walk_state->method_desc->method.owning_id);

		acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

		/*
		 * Delete the method's parse tree if asked to
		 */
		if (acpi_gbl_when_to_parse_methods & METHOD_DELETE_AT_COMPLETION) {
			acpi_ps_delete_parse_tree (
					walk_state->method_desc->method.parser_op);

			walk_state->method_desc->method.parser_op = NULL;
		}
	}

unlock_and_exit:

	acpi_cm_release_mutex (ACPI_MTX_PARSER);
	return (AE_OK);
}


