/******************************************************************************
 *
 * Module Name: pswalk - Parser routines to walk parsed op tree(s)
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
#include "dispatch.h"
#include "namesp.h"
#include "interp.h"

#define _COMPONENT          PARSER
	 MODULE_NAME         ("pswalk");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_walk_op
 *
 * PARAMETERS:  Walk_state          - Current state of the walk
 *              Op                  - Current Op to be walked
 *              Ascending_callback  - Procedure called when Op is complete
 *              Prev_op             - Where the previous Op is stored
 *              Next_op             - Where the next Op in the walk is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the next Op in a walk of the parse tree.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_get_next_walk_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	INTERPRETER_CALLBACK    ascending_callback)
{
	ACPI_GENERIC_OP         *next;
	ACPI_GENERIC_OP         *parent;
	ACPI_GENERIC_OP         *grand_parent;
	ACPI_STATUS             status;


	/* Check for a argument only if we are descending in the tree */

	if (walk_state->next_op_info != NEXT_OP_UPWARD) {
		/* Look for an argument or child of the current op */

		next = acpi_ps_get_arg (op, 0);
		if (next) {
			/* Still going downward in tree (Op is not completed yet) */

			walk_state->prev_op     = op;
			walk_state->next_op     = next;
			walk_state->next_op_info = NEXT_OP_DOWNWARD;

			return (AE_OK);
		}


		/*
		 * No more children, this Op is complete.  Save Next and Parent
		 * in case the Op object gets deleted by the callback routine
		 */

		next    = op->next;
		parent  = op->parent;

		status = ascending_callback (walk_state, op);

		switch (status)
		{
		case AE_CTRL_TERMINATE:

			/*
			 * A control method was terminated via a RETURN statement.
			 * The walk of this method is complete.
			 */
			walk_state->prev_op     = walk_state->origin;
			walk_state->next_op     = NULL;

			return (AE_OK);
			break;


		case AE_CTRL_FALSE:

			/*
			 * Either an IF/WHILE Predicate was false or we encountered a BREAK
			 * opcode.  In both cases, we do not execute the rest of the
			 * package;  We simply close out the parent (finishing the walk of
			 * this branch of the tree) and continue execution at the parent
			 * level.
			 */

			next        = parent->next;
			status      = AE_OK;

			/*
			 * If there is a sibling to the parent, we must close out the
			 * parent now, because we are going to continue to go downward (to
			 * the sibling) in the parse tree.
			 */
			if (next) {
				status = ascending_callback (walk_state, parent);

				/* The parent sibling will be next */

				walk_state->prev_op     = op;
				walk_state->next_op     = next;
				walk_state->next_op_info = NEXT_OP_DOWNWARD;

				/* Continue downward */

				return (AE_OK);
			}

			/*
			 * Drop into the loop below because we are moving upwards in
			 * the tree
			 */

			break;


		default:
			/*
			 * Check for a sibling to the current op.  A sibling means
			 * we are still going "downward" in the tree.
			 */

			if (next) {
				/* There is a sibling, it will be next */

				walk_state->prev_op     = op;
				walk_state->next_op     = next;
				walk_state->next_op_info = NEXT_OP_DOWNWARD;

				/* Continue downward */

				return (status);
			}

			/*
			 * No sibling, but check status.
			 * Abort on error from callback routine
			 */
			if (status != AE_OK) {
				/* Next op will be the parent */

				walk_state->prev_op     = op;
				walk_state->next_op     = parent;
				walk_state->next_op_info = NEXT_OP_UPWARD;

				return (status);
			}

			/*
			 * Drop into the loop below because we are moving upwards in
			 * the tree
			 */

			break;
		}
	}

	else {
		/*
		 * We are resuming a walk, and we were (are) going upward in the tree.
		 * So, we want to drop into the parent loop below.
		 */

		parent = op;
	}


	/*
	 * Look for a sibling of the current Op's parent
	 * Continue moving up the tree until we find a node that has not been
	 * visited, or we get back to where we started.
	 */
	while (parent) {
		/* We are moving up the tree, therefore this parent Op is complete */

		grand_parent = parent->parent;
		next        = parent->next;

		status = ascending_callback (walk_state, parent);


		switch (status)
		{
		case AE_CTRL_FALSE:

			/*
			 * Either an IF/WHILE Predicate was false or we encountered a
			 * BREAK opcode.  In both cases, we do not execute the rest of the
			 * package;  We simply close out the parent (finishing the walk of
			 * this branch of the tree) and continue execution at the parent
			 * level.
			 */

			parent      = grand_parent;
			next        = grand_parent->next;
			grand_parent = grand_parent->parent;

			status = ascending_callback (walk_state, parent);

			/* Now continue to the next node in the tree */

			break;


		case AE_CTRL_TRUE:

			/*
			 * Predicate of a WHILE was true and the loop just completed an
			 * execution.  Go back to the start of the loop and reevaluate the
			 * predicate.
			 */

			op = walk_state->control_state->control.predicate_op;

			walk_state->control_state->common.state = CONTROL_PREDICATE_EXECUTING;

			/*
			 * Acpi_evaluate the predicate again (next)
			 * Because we will traverse WHILE tree again
			 */

			walk_state->prev_op     = op->parent;
			walk_state->next_op     = op;
			walk_state->next_op_info = NEXT_OP_DOWNWARD;

			return (AE_OK);
			break;


		case AE_CTRL_TERMINATE:

			/*
			 * A control method was terminated via a RETURN statement.
			 * The walk of this method is complete.
			 */
			walk_state->prev_op     = walk_state->origin;
			walk_state->next_op     = NULL;

			return (AE_OK);
			break;
		}


		/*
		 * If we are back to the starting point, the walk is complete.
		 */
		if (parent == walk_state->origin) {
			/* Reached the point of origin, the walk is complete */

			walk_state->prev_op     = parent;
			walk_state->next_op     = NULL;

			return (status);
		}


		/*
		 * If there is a sibling to this parent (it is not the starting point
		 * Op), then we will visit it.
		 */
		if (next) {
			/* found sibling of parent */

			walk_state->prev_op     = parent;
			walk_state->next_op     = next;
			walk_state->next_op_info = NEXT_OP_DOWNWARD;

			return (status);
		}

		/*
		 * No sibling, check for an error from closing the parent
		 * (Also, AE_PENDING if a method call was encountered)
		 */
		if (status != AE_OK) {
			walk_state->prev_op     = parent;
			walk_state->next_op     = grand_parent;
			walk_state->next_op_info = NEXT_OP_UPWARD;

			return (status);
		}

		/* No siblings, no errors, just move up one more level in the tree */

		op                  = parent;
		parent              = grand_parent;
		walk_state->prev_op = op;
	}


	/* Got all the way to the top of the tree, we must be done! */
	/* However, the code should have terminated in the loop above */

	walk_state->next_op     = NULL;

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_walk_loop
 *
 * PARAMETERS:  Walk_list           - State of the walk
 *              Start_op            - Starting Op of the subtree to be walked
 *              Descending_callback - Procedure called when a new Op is
 *                                    encountered
 *              Ascending_callback  - Procedure called when Op is complete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform a walk of the parsed AML tree.  Begins and terminates at
 *              the Start_op.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_walk_loop (
	ACPI_WALK_LIST          *walk_list,
	ACPI_GENERIC_OP         *start_op,
	INTERPRETER_CALLBACK    descending_callback,
	INTERPRETER_CALLBACK    ascending_callback)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_WALK_STATE         *walk_state;
	ACPI_GENERIC_OP         *op = start_op;


	walk_state = acpi_ds_get_current_walk_state (walk_list);


	/* Walk entire subtree, visiting all nodes depth-first */

	while (op) {
		if (walk_state->next_op_info != NEXT_OP_UPWARD) {
			status = descending_callback (walk_state, op);
		}

		/*
		 * A TRUE exception means that an ELSE was detected, but the IF
		 * predicate evaluated TRUE.
		 */
		if (status == AE_CTRL_TRUE) {
			/*
			 * Ignore the entire ELSE block by moving on to the the next opcode.
			 * And we do that by simply going up in the tree (either to the next
			 * sibling or to the parent) from here.
			 */

			walk_state->next_op_info = NEXT_OP_UPWARD;
		}

		/* Get the next node (op) in the depth-first walk */

		status = acpi_ps_get_next_walk_op (walk_state, op, ascending_callback);

		/*
		 * A PENDING exception means that a control method invocation has been
		 * detected
		 */

		if (status == AE_CTRL_PENDING) {
			/* Transfer control to the called control method */

			status = acpi_ds_call_control_method (walk_list, walk_state, op);

			/*
			 * If the transfer to the new method method call worked, a new walk
			 * state was created -- get it
			 */

			walk_state = acpi_ds_get_current_walk_state (walk_list);
		}

		/* Abort the walk on any exception */

		if (ACPI_FAILURE (status)) {
			return (status);
		}

		op = walk_state->next_op;
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_walk_parsed_aml
 *
 * PARAMETERS:  Start_op            - Starting Op of the subtree to be walked
 *              End_op              - Where to terminate the walk
 *              Descending_callback - Procedure called when a new Op is
 *                                    encountered
 *              Ascending_callback  - Procedure called when Op is complete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Top level interface to walk the parsed AML tree.  Handles
 *              preemption of executing control methods.
 *
 *              NOTE: The End_op is usually only different from the Start_op if
 *              we don't want to visit the Start_op during the tree descent.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_walk_parsed_aml (
	ACPI_GENERIC_OP         *start_op,
	ACPI_GENERIC_OP         *end_op,
	ACPI_OBJECT_INTERNAL    *mth_desc,
	ACPI_NAME_TABLE         *start_scope,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **caller_return_desc,
	ACPI_OWNER_ID           owner_id,
	INTERPRETER_CALLBACK    descending_callback,
	INTERPRETER_CALLBACK    ascending_callback)
{
	ACPI_GENERIC_OP         *op;
	ACPI_WALK_STATE         *walk_state;
	ACPI_OBJECT_INTERNAL    *return_desc;
	ACPI_STATUS             status;
	ACPI_WALK_LIST          walk_list;
	ACPI_WALK_LIST          *prev_walk_list;


	/* Parameter Validation */

	if (!start_op || !end_op) {
		return AE_BAD_PARAMETER;
	}

	/* Initialize a new walk list */

	walk_list.walk_state = NULL;

	walk_state = acpi_ds_create_walk_state (owner_id, end_op, mth_desc, &walk_list);
	if (!walk_state) {
		return (AE_NO_MEMORY);
	}

	/* TBD: [Restructure] TEMP until we pass Walk_state to the interpreter
	 */
	prev_walk_list = acpi_gbl_current_walk_list;
	acpi_gbl_current_walk_list = &walk_list;

	if (start_scope) {
		/* Push start scope on scope stack and make it current  */

		status = acpi_ds_scope_stack_push (start_scope, ACPI_TYPE_METHOD, walk_state);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

	}

	if (mth_desc) {
		/* Init arguments if this is a control method */
		/* TBD: [Restructure] add walkstate as a param */

		acpi_ds_method_data_init_args (params, MTH_NUM_ARGS);
	}

	op = start_op;
	status = AE_OK;


	/*
	 * Execute the walk loop as long as there is a valid Walk State.  This
	 * handles nested control method invocations without recursion.
	 */

	while (walk_state) {
		if (status == AE_OK) {
			status = acpi_ps_walk_loop (&walk_list, op, descending_callback,
					 ascending_callback);
		}

		/* We are done with this walk, move on to the parent if any */

		BREAKPOINT3;

		walk_state = acpi_ds_pop_walk_state (&walk_list);

		/* Extract return value before we delete Walk_state */

		return_desc = walk_state->return_desc;

		/* Reset the current scope to the beginning of scope stack */

		acpi_ds_scope_stack_clear (walk_state);

		/*
		 * If we just returned from the execution of a control method,
		 * there's lots of cleanup to do
		 */

		if (walk_state->method_desc &&
			walk_state->method_desc->method.parser_op)
		{
			acpi_ds_terminate_control_method (walk_state);
		}

		 /* Delete this walk state and all linked control states */

		acpi_ds_delete_walk_state (walk_state);

	   /* Check if we have restarted a preempted walk */

		walk_state = acpi_ds_get_current_walk_state (&walk_list);
		if (walk_state &&
			status == AE_OK)
		{
			/* There is another walk state, restart it */

			/*
			 * If the method returned value is not used by the parent,
			 * The object is deleted
			 */

			acpi_ds_restart_control_method (walk_state, return_desc);

			/* Get the next Op to process */

			op = walk_state->next_op;
		}

		/*
		 * Just completed a 1st-level method, save the final internal return
		 * value (if any)
		 */

		else if (caller_return_desc) {
			*caller_return_desc = return_desc; /* NULL if no return value */
		}

		else if (return_desc) {
			/* Caller doesn't want it, must delete it */

			acpi_cm_remove_reference (return_desc);
		}
	}


	acpi_gbl_current_walk_list = prev_walk_list;

	return (status);
}


