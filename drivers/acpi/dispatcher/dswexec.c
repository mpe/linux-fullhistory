/******************************************************************************
 *
 * Module Name: dswexec - Dispatcher method execution callbacks;
 *                          Dispatch to interpreter.
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
#include "debugger.h"


#define _COMPONENT          DISPATCHER
	 MODULE_NAME         ("dswexec");


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been reached in the
 *                                walk;  Arguments have not been evaluated yet.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the execution of control
 *              methods.  This is where most operators and operands are
 *              dispatched to the interpreter.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_exec_begin_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_OP_INFO            *op_info;
	ACPI_STATUS             status = AE_OK;


	if (op == walk_state->origin) {
		return (AE_OK);
	}

	/*
	 * If the previous opcode was a conditional, this opcode
	 * must be the beginning of the associated predicate.
	 * Save this knowledge in the current scope descriptor
	 */

	if ((walk_state->control_state) &&
		(walk_state->control_state->common.state ==
			CONTROL_CONDITIONAL_EXECUTING))
	{
		walk_state->control_state->common.state = CONTROL_PREDICATE_EXECUTING;

		/* Save start of predicate */

		walk_state->control_state->control.predicate_op = op;
	}


	op_info = acpi_ps_get_opcode_info (op->opcode);

	/* We want to send namepaths to the load code */

	if (op->opcode == AML_NAMEPATH_OP) {
		op_info->flags = OPTYPE_NAMED_OBJECT;
	}


	/*
	 * Handle the opcode based upon the opcode type
	 */

	switch (op_info->flags & OP_INFO_TYPE)
	{
	case OPTYPE_CONTROL:

		status = acpi_ds_exec_begin_control_op (walk_state, op);
		break;


	case OPTYPE_NAMED_OBJECT:

		if (walk_state->origin->opcode == AML_METHOD_OP) {
			/*
			 * Found a named object declaration during method
			 * execution;  we must enter this object into the
			 * namespace.  The created object is temporary and
			 * will be deleted upon completion of the execution
			 * of this method.
			 */

			status = acpi_ds_load2_begin_op (walk_state, op);
		}
		break;


	default:
		break;
	}

	/* Nothing to do here during method execution */

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the execution of control
 *              methods.  The only thing we really need to do here is to
 *              notice the beginning of IF, ELSE, and WHILE blocks.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_exec_end_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op)
{
	ACPI_STATUS             status = AE_OK;
	u16                     opcode;
	u8                      optype;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_GENERIC_OP         *next_op;
	ACPI_NAMED_OBJECT       *entry;
	ACPI_GENERIC_OP         *first_arg;
	ACPI_OBJECT_INTERNAL    *result_obj = NULL;
	ACPI_OP_INFO            *op_info;
	u32                     operand_index;


	opcode = (u16) op->opcode;


	op_info = acpi_ps_get_opcode_info (op->opcode);
	if (!op_info) {
		return (AE_NOT_IMPLEMENTED);
	}

	optype = (u8) (op_info->flags & OP_INFO_TYPE);
	first_arg = op->value.arg;

	/* Init the walk state */

	walk_state->num_operands = 0;
	walk_state->return_desc = NULL;


	/* Call debugger for single step support (DEBUG build only) */


	/* Decode the opcode */

	switch (optype)
	{
	case OPTYPE_UNDEFINED:

		return (AE_NOT_IMPLEMENTED);
		break;


	case OPTYPE_BOGUS:
		break;

	case OPTYPE_CONSTANT:           /* argument type only */
	case OPTYPE_LITERAL:            /* argument type only */
	case OPTYPE_DATA_TERM:          /* argument type only */
	case OPTYPE_LOCAL_VARIABLE:     /* argument type only */
	case OPTYPE_METHOD_ARGUMENT:    /* argument type only */
		break;


	/* most operators with arguments */

	case OPTYPE_MONADIC1:
	case OPTYPE_DYADIC1:
	case OPTYPE_MONADIC2:
	case OPTYPE_MONADIC2_r:
	case OPTYPE_DYADIC2:
	case OPTYPE_DYADIC2_r:
	case OPTYPE_DYADIC2_s:
	case OPTYPE_RECONFIGURATION:
	case OPTYPE_INDEX:
	case OPTYPE_MATCH:
	case OPTYPE_CREATE_FIELD:
	case OPTYPE_FATAL:

		status = acpi_ds_create_operands (walk_state, first_arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		operand_index = walk_state->num_operands - 1;

		switch (optype)
		{

		case OPTYPE_MONADIC1:

			/* 1 Operand, 0 External_result, 0 Internal_result */

			status = acpi_aml_exec_monadic1 (opcode, walk_state);
			break;


		case OPTYPE_MONADIC2:

			/* 1 Operand, 0 External_result, 1 Internal_result */

			status = acpi_aml_exec_monadic2 (opcode, walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_MONADIC2_r:

			/* 1 Operand, 1 External_result, 1 Internal_result */

			status = acpi_aml_exec_monadic2_r (opcode, walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_DYADIC1:

			/* 2 Operands, 0 External_result, 0 Internal_result */

			status = acpi_aml_exec_dyadic1 (opcode, walk_state);

			break;


		case OPTYPE_DYADIC2:

			/* 2 Operands, 0 External_result, 1 Internal_result */

			status = acpi_aml_exec_dyadic2 (opcode, walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_DYADIC2_r:

			/* 2 Operands, 1 or 2 External_results, 1 Internal_result */


			/* NEW INTERFACE:
			 * Pass in Walk_state, keep result obj but let interpreter
			 * push the result
			 */

			status = acpi_aml_exec_dyadic2_r (opcode, walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_DYADIC2_s:  /* Synchronization Operator */

			/* 2 Operands, 0 External_result, 1 Internal_result */

			status = acpi_aml_exec_dyadic2_s (opcode, walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_RECONFIGURATION:

			/* 1 or 2 operands, 0 Internal Result */

			status = acpi_aml_exec_reconfiguration (opcode, walk_state);
			break;


		case OPTYPE_CREATE_FIELD:

			/* 3 or 4 Operands, 0 External_result, 0 Internal_result */

			status = acpi_aml_exec_create_field (opcode, walk_state);
			break;


		case OPTYPE_FATAL:

			/* 3 Operands, 0 External_result, 0 Internal_result */

			status = acpi_aml_exec_fatal (walk_state);
			break;


		case OPTYPE_INDEX:  /* Type 2 opcode with 3 operands */

			/* 3 Operands, 1 External_result, 1 Internal_result */

			status = acpi_aml_exec_index (walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;


		case OPTYPE_MATCH:  /* Type 2 opcode with 6 operands */

			/* 6 Operands, 0 External_result, 1 Internal_result */

			status = acpi_aml_exec_match (walk_state, &result_obj);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ds_result_stack_push (result_obj, walk_state);
			}

			break;
		}

		break;


	case OPTYPE_CONTROL:    /* Type 1 opcode, IF/ELSE/WHILE/NOOP */

		/* 1 Operand, 0 External_result, 0 Internal_result */

		status = acpi_ds_exec_end_control_op (walk_state, op);

		break;


	case OPTYPE_METHOD_CALL:

		/*
		 * (AML_METHODCALL) Op->Value->Arg->Acpi_named_object contains
		 * the method NTE pointer
		 */
		/* Next_op points to the op that holds the method name */
		next_op = first_arg;
		entry = next_op->acpi_named_object;

		/* Next_op points to first argument op */
		next_op = next_op->next;


		/*
		 * Get the method's arguments and put them on the operand stack
		 */

		status = acpi_ds_create_operands (walk_state, next_op);
		if (ACPI_FAILURE (status)) {
			break;
		}

		/*
		 * Since the operands will be passed to another
		 * control method, we must resolve all local
		 * references here (Local variables, arguments
		 * to *this* method, etc.)
		 */

		status = acpi_ds_resolve_operands (walk_state);
		if (ACPI_FAILURE (status)) {
			break;
		}

		/* Open new scope on the scope stack */
/*
		Status = Acpi_ns_scope_stack_push_entry (Entry);
		if (ACPI_FAILURE (Status)) {
			break;
		}
*/

		/* Tell the walk loop to preempt this running method and
		execute the new method */

		status = AE_CTRL_PENDING;

		/* Return now; we don't want to disturb anything,
		especially the operand count! */

		return (status);
		break;


	case OPTYPE_NAMED_OBJECT:


		if ((walk_state->origin->opcode == AML_METHOD_OP) &&
			(walk_state->origin != op))
		{
			status = acpi_ds_load2_end_op (walk_state, op);
			if (ACPI_FAILURE (status)) {
				break;
			}
		}

		switch (op->opcode)
		{
		case AML_REGION_OP:

			status = acpi_ds_eval_region_operands (walk_state, op);

			break;


		case AML_METHOD_OP:

			break;


		case AML_ALIAS_OP:

			/* Alias creation was already handled by call
			to psxload above */
			break;


		default:
			/* Nothing needs to be done */

			status = AE_OK;
			break;
		}

		break;

	default:

		status = AE_NOT_IMPLEMENTED;
		break;
	}


	/*
	 * Check if we just completed the evaluation of a
	 * conditional predicate
	 */

	if ((walk_state->control_state) &&
		(walk_state->control_state->common.state ==
			CONTROL_PREDICATE_EXECUTING) &&
		(walk_state->control_state->control.predicate_op == op))
	{
		/* Completed the predicate, the result must be a number */

		walk_state->control_state->common.state = 0;

		if (result_obj) {
			status = acpi_ds_result_stack_pop (&obj_desc, walk_state);
			if (ACPI_FAILURE (status)) {
				goto cleanup;
			}
		}

		else {
			status = acpi_ds_create_operand (walk_state, op);
			if (ACPI_FAILURE (status)) {
				goto cleanup;
			}

			status = acpi_aml_resolve_to_value (&walk_state->operands [0]);
			if (ACPI_FAILURE (status)) {
				goto cleanup;
			}

			obj_desc = walk_state->operands [0];
		}

		if (!obj_desc) {
			status = AE_AML_NO_OPERAND;
			goto cleanup;
		}

		if (obj_desc->common.type != ACPI_TYPE_NUMBER) {
			status = AE_AML_OPERAND_TYPE;
			goto cleanup;
		}
		/* Save the result of the predicate evaluation on
		the control stack */

		if (obj_desc->number.value) {
			walk_state->control_state->common.value = TRUE;
		}
		else {
			/* Predicate is FALSE, we will just toss the
			rest of the package */

			walk_state->control_state->common.value = FALSE;
			status = AE_CTRL_FALSE;
		}

		 /* Break to debugger to display result */

		/* Delete the predicate result object (we know that
		we don't need it anymore) and cleanup the stack */

		acpi_cm_remove_reference (obj_desc);
		result_obj = NULL;

		walk_state->control_state->common.state = CONTROL_NORMAL;
	}


cleanup:

	if (result_obj) {
		/* Break to debugger to display result */

		/*
		 * Delete the result op if and only if:
		 * Parent will not use the result -- such as any
		 * non-nested type2 op in a method (parent will be method)
		 */
		acpi_ds_delete_result_if_not_used (op, result_obj, walk_state);
	}

	/* Always clear the object stack */

	/* TBD: [Investigate] Clear stack of return value,
	but don't delete it */
	walk_state->num_operands = 0;

	return (status);
}


