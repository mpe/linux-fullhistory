/******************************************************************************
 *
 * Module Name: pswalk - Parser routines to walk parsed op tree(s)
 *              $Revision: 58 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
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
#include "acparser.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_PARSER
	 MODULE_NAME         ("pswalk")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_walk_op
 *
 * PARAMETERS:  Walk_state          - Current state of the walk
 *              Op                  - Current Op to be walked
 *              Ascending_callback  - Procedure called when Op is complete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the next Op in a walk of the parse tree.
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_walk_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_parse_upwards      ascending_callback)
{
	acpi_parse_object       *next;
	acpi_parse_object       *parent;
	acpi_parse_object       *grand_parent;
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ps_get_next_walk_op", op);


	/* Check for a argument only if we are descending in the tree */

	if (walk_state->next_op_info != NEXT_OP_UPWARD) {
		/* Look for an argument or child of the current op */

		next = acpi_ps_get_arg (op, 0);
		if (next) {
			/* Still going downward in tree (Op is not completed yet) */

			walk_state->prev_op     = op;
			walk_state->next_op     = next;
			walk_state->next_op_info = NEXT_OP_DOWNWARD;

			return_ACPI_STATUS (AE_OK);
		}


		/*
		 * No more children, this Op is complete.  Save Next and Parent
		 * in case the Op object gets deleted by the callback routine
		 */
		next    = op->next;
		parent  = op->parent;

		walk_state->op    = op;
		walk_state->op_info = acpi_ps_get_opcode_info (op->opcode);
		walk_state->opcode = op->opcode;

		status = ascending_callback (walk_state);

		/*
		 * If we are back to the starting point, the walk is complete.
		 */
		if (op == walk_state->origin) {
			/* Reached the point of origin, the walk is complete */

			walk_state->prev_op     = op;
			walk_state->next_op     = NULL;

			return_ACPI_STATUS (status);
		}

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

			return_ACPI_STATUS (status);
		}

		/*
		 * Drop into the loop below because we are moving upwards in
		 * the tree
		 */
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

		walk_state->op    = parent;
		walk_state->op_info = acpi_ps_get_opcode_info (parent->opcode);
		walk_state->opcode = parent->opcode;

		status = ascending_callback (walk_state);

		/*
		 * If we are back to the starting point, the walk is complete.
		 */
		if (parent == walk_state->origin) {
			/* Reached the point of origin, the walk is complete */

			walk_state->prev_op     = parent;
			walk_state->next_op     = NULL;

			return_ACPI_STATUS (status);
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

			return_ACPI_STATUS (status);
		}

		/* No siblings, no errors, just move up one more level in the tree */

		op                  = parent;
		parent              = grand_parent;
		walk_state->prev_op = op;
	}


	/* Got all the way to the top of the tree, we must be done! */
	/* However, the code should have terminated in the loop above */

	walk_state->next_op     = NULL;

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_delete_completed_op
 *
 * PARAMETERS:  State           - Walk state
 *              Op              - Completed op
 *
 * RETURN:      AE_OK
 *
 * DESCRIPTION: Callback function for Acpi_ps_get_next_walk_op(). Used during
 *              Acpi_ps_delete_parse tree to delete Op objects when all sub-objects
 *              have been visited (and deleted.)
 *
 ******************************************************************************/

static acpi_status
acpi_ps_delete_completed_op (
	acpi_walk_state         *walk_state)
{

	acpi_ps_free_op (walk_state->op);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_delete_parse_tree
 *
 * PARAMETERS:  Subtree_root        - Root of tree (or subtree) to delete
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a portion of or an entire parse tree.
 *
 ******************************************************************************/

void
acpi_ps_delete_parse_tree (
	acpi_parse_object       *subtree_root)
{
	acpi_walk_state         *walk_state;
	acpi_walk_list          walk_list;


	FUNCTION_TRACE_PTR ("Ps_delete_parse_tree", subtree_root);


	if (!subtree_root) {
		return_VOID;
	}

	/* Create and initialize a new walk list */

	walk_list.walk_state = NULL;
	walk_list.acquired_mutex_list.prev = NULL;
	walk_list.acquired_mutex_list.next = NULL;

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT, NULL, NULL, &walk_list);
	if (!walk_state) {
		return_VOID;
	}

	walk_state->parse_flags         = 0;
	walk_state->descending_callback = NULL;
	walk_state->ascending_callback  = NULL;


	walk_state->origin = subtree_root;
	walk_state->next_op = subtree_root;


	/* Head downward in the tree */

	walk_state->next_op_info = NEXT_OP_DOWNWARD;

	/* Visit all nodes in the subtree */

	while (walk_state->next_op) {
		acpi_ps_get_next_walk_op (walk_state, walk_state->next_op,
				 acpi_ps_delete_completed_op);
	}

	/* We are done with this walk */

	acpi_ex_release_all_mutexes ((acpi_operand_object *) &walk_list.acquired_mutex_list);
	acpi_ds_delete_walk_state (walk_state);

	return_VOID;
}


