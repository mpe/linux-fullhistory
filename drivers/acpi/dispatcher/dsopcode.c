/******************************************************************************
 *
 * Module Name: dsopcode - Dispatcher Op Region support and handling of
 *                         "control" opcodes
 *              $Revision: 17 $
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
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acevents.h"
#include "actables.h"

#define _COMPONENT          DISPATCHER
	 MODULE_NAME         ("dsopcode")


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_region_arguments
 *
 * PARAMETERS:  Rgn_desc        - A valid region object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get region address and length.  This implements the late
 *              evaluation of these region attributes.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_get_region_arguments (
	ACPI_OPERAND_OBJECT     *rgn_desc)
{
	ACPI_OPERAND_OBJECT     *method_desc;
	ACPI_NAMESPACE_NODE     *node;
	ACPI_PARSE_OBJECT       *op;
	ACPI_PARSE_OBJECT       *region_op;
	ACPI_STATUS             status;
	ACPI_TABLE_DESC         *table_desc;


	if (rgn_desc->region.flags & AOPOBJ_DATA_VALID) {
		return (AE_OK);
	}


	method_desc = rgn_desc->region.method;
	node = rgn_desc->region.node;


	/*
	 * Allocate a new parser op to be the root of the parsed
	 * Op_region tree
	 */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Save the Node for use in Acpi_ps_parse_aml */

	op->node = acpi_ns_get_parent_object (node);

	/* Get a handle to the parent ACPI table */

	status = acpi_tb_handle_to_object (node->owner_id, &table_desc);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Parse the entire Op_region declaration, creating a parse tree */

	status = acpi_ps_parse_aml (op, method_desc->method.pcode,
			  method_desc->method.pcode_length, 0,
			  NULL, NULL, NULL, acpi_ds_load1_begin_op, acpi_ds_load1_end_op);

	if (ACPI_FAILURE (status)) {
		acpi_ps_delete_parse_tree (op);
		return (status);
	}


	/* Get and init the actual Region_op created above */

/*    Region_op = Op->Value.Arg;
	Op->Node = Node;*/


	region_op = op->value.arg;
	region_op->node = node;
	acpi_ps_delete_parse_tree (op);

	/* Acpi_evaluate the address and length arguments for the Op_region */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	op->node = acpi_ns_get_parent_object (node);

	status = acpi_ps_parse_aml (op, method_desc->method.pcode,
			  method_desc->method.pcode_length,
			  ACPI_PARSE_EXECUTE | ACPI_PARSE_DELETE_TREE,
			  NULL /*Method_desc*/, NULL, NULL,
			  acpi_ds_exec_begin_op, acpi_ds_exec_end_op);
/*
	Acpi_ps_walk_parsed_aml (Region_op, Region_op, NULL, NULL, NULL,
			 NULL, Table_desc->Table_id,
			 Acpi_ds_exec_begin_op, Acpi_ds_exec_end_op);
*/
	/* All done with the parse tree, delete it */

	acpi_ps_delete_parse_tree (op);

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_initialize_region
 *
 * PARAMETERS:  Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_initialize_region (
	ACPI_HANDLE             obj_handle)
{
	ACPI_OPERAND_OBJECT     *obj_desc;
	ACPI_STATUS             status;


	obj_desc = acpi_ns_get_attached_object (obj_handle);

	/* Namespace is NOT locked */

	status = acpi_ev_initialize_region (obj_desc, FALSE);

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_eval_region_operands
 *
 * PARAMETERS:  Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get region address and length
 *              Called from Acpi_ds_exec_end_op during Op_region parse tree walk
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_eval_region_operands (
	ACPI_WALK_STATE         *walk_state,
	ACPI_PARSE_OBJECT       *op)
{
	ACPI_STATUS             status;
	ACPI_OPERAND_OBJECT     *obj_desc;
	ACPI_OPERAND_OBJECT     *region_desc;
	ACPI_NAMESPACE_NODE     *node;
	ACPI_PARSE_OBJECT       *next_op;


	/*
	 * This is where we evaluate the address and length fields of the Op_region declaration
	 */

	node =  op->node;

	/* Next_op points to the op that holds the Space_iD */
	next_op = op->value.arg;

	/* Next_op points to address op */
	next_op = next_op->next;

	/* Acpi_evaluate/create the address and length operands */

	status = acpi_ds_create_operands (walk_state, next_op);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	region_desc = acpi_ns_get_attached_object (node);
	if (!region_desc) {
		return (AE_NOT_EXIST);
	}

	/* Get the length and save it */

	/* Top of stack */
	obj_desc = walk_state->operands[walk_state->num_operands - 1];

	region_desc->region.length = obj_desc->number.value;
	acpi_cm_remove_reference (obj_desc);

	/* Get the address and save it */

	/* Top of stack - 1 */
	obj_desc = walk_state->operands[walk_state->num_operands - 2];

	region_desc->region.address = obj_desc->number.value;
	acpi_cm_remove_reference (obj_desc);


	/* Now the address and length are valid for this opregion */

	region_desc->region.flags |= AOPOBJ_DATA_VALID;

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_begin_control_op
 *
 * PARAMETERS:  Walk_list       - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_exec_begin_control_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_PARSE_OBJECT       *op)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_GENERIC_STATE      *control_state;


	switch (op->opcode)
	{
	case AML_IF_OP:
	case AML_WHILE_OP:

		/*
		 * IF/WHILE: Create a new control state to manage these
		 * constructs. We need to manage these as a stack, in order
		 * to handle nesting.
		 */

		control_state = acpi_cm_create_control_state ();
		if (!control_state) {
			status = AE_NO_MEMORY;
			break;
		}

		acpi_cm_push_generic_state (&walk_state->control_state, control_state);

		/*
		 * Save a pointer to the predicate for multiple executions
		 * of a loop
		 */
		walk_state->control_state->control.aml_predicate_start =
				 walk_state->parser_state->aml - 1;
				 /*Acpi_ps_pkg_length_encoding_size (GET8 (Walk_state->Parser_state->Aml));*/
		break;


	case AML_ELSE_OP:

		/* Predicate is in the state object */
		/* If predicate is true, the IF was executed, ignore ELSE part */

		if (walk_state->last_predicate) {
			status = AE_CTRL_TRUE;
		}

		break;


	case AML_RETURN_OP:

		break;


	default:
		break;
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_end_control_op
 *
 * PARAMETERS:  Walk_list       - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ds_exec_end_control_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_PARSE_OBJECT       *op)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_GENERIC_STATE      *control_state;


	switch (op->opcode)
	{
	case AML_IF_OP:

		/*
		 * Save the result of the predicate in case there is an
		 * ELSE to come
		 */

		walk_state->last_predicate =
				(u8) walk_state->control_state->common.value;

		/*
		 * Pop the control state that was created at the start
		 * of the IF and free it
		 */

		control_state =
				acpi_cm_pop_generic_state (&walk_state->control_state);

		acpi_cm_delete_generic_state (control_state);

		break;


	case AML_ELSE_OP:

		break;


	case AML_WHILE_OP:

		if (walk_state->control_state->common.value) {
			/* Predicate was true, go back and evaluate it again! */

			status = AE_CTRL_PENDING;
		}

/*        else {*/
			/* Pop this control state and free it */

			control_state =
					acpi_cm_pop_generic_state (&walk_state->control_state);

			walk_state->aml_last_while = control_state->control.aml_predicate_start;
			acpi_cm_delete_generic_state (control_state);
/*        }*/

		break;


	case AML_RETURN_OP:


		/*
		 * One optional operand -- the return value
		 * It can be either an immediate operand or a result that
		 * has been bubbled up the tree
		 */
		if (op->value.arg) {
			/* Return statement has an immediate operand */

			status = acpi_ds_create_operands (walk_state, op->value.arg);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			/*
			 * If value being returned is a Reference (such as
			 * an arg or local), resolve it now because it may
			 * cease to exist at the end of the method.
			 */

			status = acpi_aml_resolve_to_value (&walk_state->operands [0], walk_state);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			/*
			 * Get the return value and save as the last result
			 * value.  This is the only place where Walk_state->Return_desc
			 * is set to anything other than zero!
			 */

			walk_state->return_desc = walk_state->operands[0];
		}

		else if (walk_state->num_results > 0) {
			/*
			 * The return value has come from a previous calculation.
			 *
			 * If value being returned is a Reference (such as
			 * an arg or local), resolve it now because it may
			 * cease to exist at the end of the method.
			 */

			status = acpi_aml_resolve_to_value (&walk_state->results [0], walk_state);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			walk_state->return_desc = walk_state->results [0];
		}

		else {
			/* No return operand */

			if (walk_state->num_operands) {
				acpi_cm_remove_reference (walk_state->operands [0]);
			}

			walk_state->operands [0]    = NULL;
			walk_state->num_operands    = 0;
			walk_state->return_desc     = NULL;
		}


		/* End the control method execution right now */
		status = AE_CTRL_TERMINATE;
		break;


	case AML_NOOP_OP:

		/* Just do nothing! */
		break;


	case AML_BREAK_POINT_OP:

		/* Call up to the OS dependent layer to handle this */

		acpi_os_breakpoint (NULL);

		/* If it returns, we are done! */

		break;


	case AML_BREAK_OP:

		/*
		 * As per the ACPI specification:
		 *      "The break operation causes the current package
		 *          execution to complete"
		 *      "Break -- Stop executing the current code package
		 *          at this point"
		 *
		 * Returning AE_FALSE here will cause termination of
		 * the current package, and execution will continue one
		 * level up, starting with the completion of the parent Op.
		 */

		status = AE_CTRL_FALSE;
		break;


	default:

		status = AE_AML_BAD_OPCODE;
		break;
	}


	return (status);
}

