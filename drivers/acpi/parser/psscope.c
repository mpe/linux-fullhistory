/******************************************************************************
 *
 * Module Name: psscope - Parser scope stack management routines
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

#define _COMPONENT          PARSER
	 MODULE_NAME         ("psscope");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_parent_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Pointer to an Op object
 *
 * DESCRIPTION: Get parent of current op being parsed
 *
 ******************************************************************************/

ACPI_GENERIC_OP *
acpi_ps_get_parent_scope (
	ACPI_PARSE_STATE        *parser_state)
{
	return parser_state->scope->op;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_has_completed_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Boolean, TRUE = scope completed.
 *
 * DESCRIPTION: Is parsing of current argument complete?  Determined by
 *              1) AML pointer is at or beyond the end of the scope
 *              2) The scope argument count has reached zero.
 *
 ******************************************************************************/

u8
acpi_ps_has_completed_scope (
	ACPI_PARSE_STATE        *parser_state)
{
	return (u8) ((parser_state->aml >= parser_state->scope->arg_end ||
			   !parser_state->scope->arg_count));
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_init_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Root                - the root object of this new scope
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate and init a new scope object
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_init_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         *root)
{
	ACPI_PARSE_SCOPE        *scope;


	scope = acpi_cm_callocate (sizeof (ACPI_PARSE_SCOPE));
	if (!scope) {
		return AE_NO_MEMORY;
	}

	scope->op               = root;
	scope->arg_count        = ACPI_VAR_ARGS;
	scope->arg_end          = parser_state->aml_end;
	scope->pkg_end          = parser_state->aml_end;
	parser_state->scope     = scope;
	parser_state->start_op  = root;

	return AE_OK;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_push_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Op                  - Current op to be pushed
 *              Next_arg            - Next op argument (to be pushed)
 *              Arg_count           - Fixed or variable number of args
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push current op to begin parsing its argument
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_push_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         *op,
	u32                     remaining_args,
	u32                     arg_count)
{
	ACPI_PARSE_SCOPE        *scope = parser_state->scope_avail;


	if (scope) {
		/* grabbed scope from available list */

		parser_state->scope_avail = scope->parent;
	}

	else {
		/* allocate scope from the heap */

		scope = (ACPI_PARSE_SCOPE*) acpi_cm_allocate (sizeof (ACPI_PARSE_SCOPE));
		if (!scope) {
			return (AE_NO_MEMORY);
		}
	}

	/* Always zero out the scope before init */

	MEMSET (scope, 0, sizeof (*scope));

	scope->op           = op;
	scope->arg_list     = remaining_args;
	scope->arg_count    = arg_count;
	scope->pkg_end      = parser_state->pkg_end;
	scope->parent       = parser_state->scope;
	parser_state->scope = scope;

	if (arg_count == ACPI_VAR_ARGS) {
		/* multiple arguments */

		scope->arg_end = parser_state->pkg_end;
	}

	else {
		/* single argument */

		scope->arg_end = ACPI_MAX_AML;
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_pop_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Op                  - Where the popped op is returned
 *              Next_arg            - Where the popped "next argument" is
 *                                    returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return to parsing a previous op
 *
 ******************************************************************************/

void
acpi_ps_pop_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         **op,
	u32                     *arg_list)
{
	ACPI_PARSE_SCOPE        *scope = parser_state->scope;


	if (scope->parent) {
		/* return to parsing previous op */

		*op                     = scope->op;
		*arg_list               = scope->arg_list;
		parser_state->pkg_end   = scope->pkg_end;
		parser_state->scope     = scope->parent;

		/* add scope to available list */

		scope->parent           = parser_state->scope_avail;
		parser_state->scope_avail = scope;
	}

	else {
		/* empty parse stack, prepare to fetch next opcode */

		*op                     = NULL;
		*arg_list               = 0;
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_cleanup_scope
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Destroy available list, remaining stack levels, and return
 *              root scope
 *
 ******************************************************************************/

void
acpi_ps_cleanup_scope (
	ACPI_PARSE_STATE        *parser_state)
{
	ACPI_PARSE_SCOPE        *scope;


	if (!parser_state) {
		return;
	}

	/* destroy available list */

	while (parser_state->scope_avail) {
		scope = parser_state->scope_avail;
		parser_state->scope_avail = scope->parent;
		acpi_cm_free (scope);
	}

	/* destroy scope stack */

	while (parser_state->scope) {
		scope = parser_state->scope;
		parser_state->scope = scope->parent;
		acpi_cm_free (scope);
	}

	return;
}

