/******************************************************************************
 *
 * Module Name: psparse - Parser top level AML parse routines
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


/*
 * Parse the AML and build an operation tree as most interpreters,
 * like Perl, do.  Parsing is done by hand rather than with a YACC
 * generated parser to tightly constrain stack and dynamic memory
 * usage.  At the same time, parsing is kept flexible and the code
 * fairly compact by parsing based on a list of AML opcode
 * templates in Acpi_gbl_Aml_op_info[]
 */

#include "acpi.h"
#include "parser.h"
#include "dispatch.h"
#include "amlcode.h"
#include "namesp.h"
#include "debugger.h"

#define _COMPONENT          PARSER
	 MODULE_NAME         ("psparse");


u32                         acpi_gbl_depth = 0;
extern u32                  acpi_gbl_scope_depth;


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

ACPI_STATUS
acpi_ps_delete_completed_op (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op)
{

	acpi_ps_free_op (op);
	return AE_OK;
}


#ifndef PARSER_ONLY
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_delete_parse_tree
 *
 * PARAMETERS:  Root            - Root of tree (or subtree) to delete
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a portion of or an entire parse tree.
 *
 ******************************************************************************/

void
acpi_ps_delete_parse_tree (
	ACPI_GENERIC_OP         *root)
{
	ACPI_GENERIC_OP         *op;
	ACPI_WALK_STATE         walk_state;


	walk_state.origin = root;
	op = root;

	/* TBD: [Restructure] hack for root case */

	if (op == acpi_gbl_parsed_namespace_root) {
		op = acpi_ps_get_child (op);
	}

	/* Save root until last, so that we know when the tree has been walked */

	walk_state.next_op = op;
	walk_state.next_op_info = NEXT_OP_DOWNWARD;

	while (walk_state.next_op) {
		acpi_ps_get_next_walk_op (&walk_state, walk_state.next_op,
				 acpi_ps_delete_completed_op);
	}
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_peek_opcode
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get next AML opcode (without incrementing AML pointer)
 *
 ******************************************************************************/

u32
acpi_ps_get_opcode_size (
	u32                     opcode)
{

	/* Extended (2-byte) opcode if > 255 */

	if (opcode > 0x00FF) {
		return 2;
	}

	/* Otherwise, just a single byte opcode */

	return 1;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_peek_opcode
 *
 * PARAMETERS:  Parser_state        - A parser state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get next AML opcode (without incrementing AML pointer)
 *
 ******************************************************************************/

u16
acpi_ps_peek_opcode (
	ACPI_PARSE_STATE        *parser_state)
{
	u8                      *aml;
	u16                     opcode;


	aml = parser_state->aml;
	opcode = (u16) GET8 (aml);

	aml++;


	/*
	 * Original code special cased LNOTEQUAL, LLESSEQUAL, LGREATEREQUAL.
	 * These opcodes are no longer recognized. Instead, they are broken into
	 * two opcodes.
	 *
	 *
	 *    if (Opcode == AML_EXTOP
	 *       || (Opcode == AML_LNOT
	 *          && (GET8 (Acpi_aml) == AML_LEQUAL
	 *               || GET8 (Acpi_aml) == AML_LGREATER
	 *               || GET8 (Acpi_aml) == AML_LLESS)))
	 *
	 *     extended Opcode, !=, <=, or >=
	 */

	if (opcode == AML_EXTOP) {
		/* Extended opcode */

		opcode = (u16) ((opcode << 8) | GET8 (aml));
		aml++;
	}

	/* don't convert bare name to a namepath */

	return opcode;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_create_state
 *
 * PARAMETERS:  Acpi_aml            - Acpi_aml code pointer
 *              Acpi_aml_size       - Length of AML code
 *
 * RETURN:      A new parser state object
 *
 * DESCRIPTION: Create and initialize a new parser state object
 *
 ******************************************************************************/

ACPI_PARSE_STATE *
acpi_ps_create_state (
	u8                      *aml,
	s32                     aml_size)
{
	ACPI_PARSE_STATE        *parser_state;


	parser_state = acpi_cm_callocate (sizeof (ACPI_PARSE_STATE));
	if (!parser_state) {
		return (NULL);
	}

	parser_state->aml      = aml;
	parser_state->aml_end  = aml + aml_size;
	parser_state->pkg_end  = parser_state->aml_end;
	parser_state->aml_start = aml;


	return (parser_state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_find_object
 *
 * PARAMETERS:  Opcode          - Current opcode
 *              Parser_state    - Current state
 *              Walk_state      - Current state
 *              *Op             - Where found/new op is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find a named object.  Two versions - one to search the parse
 *              tree (for parser-only applications such as acpidump), another
 *              to search the ACPI internal namespace (the parse tree may no
 *              longer exist)
 *
 ******************************************************************************/

#ifdef PARSER_ONLY

ACPI_STATUS
acpi_ps_find_object (
	u16                     opcode,
	ACPI_PARSE_STATE        *parser_state,
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         **op)
{
	char                    *path;


	/* Find the name in the parse tree */

	path = acpi_ps_get_next_namestring (parser_state);

	*op = acpi_ps_find (acpi_ps_get_parent_scope (parser_state),
			  path, opcode, 1);

	if (!(*op)) {
		return AE_NOT_FOUND;
	}

	return AE_OK;
}
#else

ACPI_STATUS
acpi_ps_find_object (
	u16                     opcode,
	ACPI_PARSE_STATE        *parser_state,
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         **out_op)
{
	char                    *path;
	ACPI_GENERIC_OP         *op;
	OBJECT_TYPE_INTERNAL    data_type;
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *entry = NULL;


	/*
	 * The full parse tree has already been deleted -- therefore, we are parsing
	 * a control method.  We can lookup the name in the namespace instead of
	 * the parse tree!
	 */


	path = acpi_ps_get_next_namestring (parser_state);

	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (opcode);

	/*
	 * Enter the object into the namespace
	 * LOAD_PASS1 means Create if not found
	 */

	status = acpi_ns_lookup (walk_state->scope_info, path, data_type,
			 IMODE_LOAD_PASS1,
			 NS_NO_UPSEARCH, walk_state, &entry);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Create a new op */

	op = acpi_ps_alloc_op (opcode);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Initialize */

	((ACPI_NAMED_OP *)op)->name = entry->name;
	op->acpi_named_object = entry;


	acpi_ps_append_arg (acpi_ps_get_parent_scope (parser_state), op);

	*out_op = op;


	return (AE_OK);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_parse_loop
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse AML (pointed to by the current parser state) and return
 *              a tree of ops.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_parse_loop (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_WALK_STATE         *walk_state,
	u32                     parse_flags)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_GENERIC_OP         *op = NULL;     /* current op */
	ACPI_OP_INFO            *op_info;
	ACPI_GENERIC_OP         *arg = NULL;
	ACPI_DEFERRED_OP        *deferred_op;
	u32                     arg_count;      /* push for fixed or var args */
	u32                     arg_types = 0;
	ACPI_PTRDIFF            aml_offset;
	u16                     opcode;
	ACPI_GENERIC_OP         pre_op;


#ifndef PARSER_ONLY
	OBJECT_TYPE_INTERNAL    data_type;
#endif


	/*
	 * Iterative parsing loop, while there is more aml to process:
	 */
	while (parser_state->aml < parser_state->aml_end) {
		if (!op) {
			/* Get the next opcode from the AML stream */

			aml_offset  = parser_state->aml - parser_state->aml_start;
			opcode      = acpi_ps_peek_opcode (parser_state);
			op_info     = acpi_ps_get_opcode_info (opcode);

			/*
			 * First cut to determine what we have found:
			 * 1) A valid AML opcode
			 * 2) A name string
			 * 3) An unknown/invalid opcode
			 */

			if (op_info) {
				/* Found opcode info, this is a normal opcode */

				parser_state->aml += acpi_ps_get_opcode_size (opcode);
				arg_types = op_info->parse_args;
			}

			else if (acpi_ps_is_prefix_char (opcode) ||
					acpi_ps_is_leading_char (opcode))
			{
				/*
				 * Starts with a valid prefix or ASCII char, this is a name
				 * string.  Convert the bare name string to a namepath.
				 */

				opcode = AML_NAMEPATH_OP;
				arg_types = ARGP_NAMESTRING;
			}

			else {
				/* The opcode is unrecognized.  Just skip unknown opcodes */

				parser_state->aml += acpi_ps_get_opcode_size (opcode);
				continue;
			}


			/* Create Op structure and append to parent's argument list */

			if (acpi_ps_is_named_op (opcode)) {
				pre_op.value.arg = NULL;
				pre_op.opcode = opcode;

				while (GET_CURRENT_ARG_TYPE (arg_types) != ARGP_NAME) {
					arg = acpi_ps_get_next_arg (parser_state,
							 GET_CURRENT_ARG_TYPE (arg_types),
							 &arg_count);
					acpi_ps_append_arg (&pre_op, arg);
					INCREMENT_ARG_LIST (arg_types);
				}


				/* We know that this arg is a name, move to next arg */

				INCREMENT_ARG_LIST (arg_types);

				status = acpi_ps_find_object (opcode, parser_state, walk_state, &op);
				if (ACPI_FAILURE (status)) {
					return (AE_NOT_FOUND);
				}

				acpi_ps_append_arg (op, pre_op.value.arg);
				acpi_gbl_depth++;


				if (op->opcode == AML_REGION_OP) {
					deferred_op = acpi_ps_to_deferred_op (op);
					if (deferred_op) {
						/*
						 * Skip parsing of control method or opregion body,
						 * because we don't have enough info in the first pass
						 * to parse them correctly.
						 *
						 * Backup to beginning of Op_region declaration (2 for
						 * Opcode, 4 for name)
						 *
						 * Body_length is unknown until we parse the body
						 */

						deferred_op->body       = parser_state->aml - 6;
						deferred_op->body_length = 0;
					}
				}
			}

			else {
				/* Not a named opcode, just allocate Op and append to parent */

				op = acpi_ps_alloc_op (opcode);
				if (!op) {
					return (AE_NO_MEMORY);
				}

				acpi_ps_append_arg (acpi_ps_get_parent_scope (parser_state), op);
			}

			op->aml_offset = aml_offset;

		}


		arg_count = 0;
		if (arg_types)  /* Are there any arguments that must be processed? */ {
			/* get arguments */

			switch (op->opcode)
			{
			case AML_BYTE_OP:       /* AML_BYTEDATA_ARG */
			case AML_WORD_OP:       /* AML_WORDDATA_ARG */
			case AML_DWORD_OP:      /* AML_DWORDATA_ARG */
			case AML_STRING_OP:     /* AML_ASCIICHARLIST_ARG */

				/* fill in constant or string argument directly */

				acpi_ps_get_next_simple_arg (parser_state,
						 GET_CURRENT_ARG_TYPE (arg_types), op);
				break;

			case AML_NAMEPATH_OP:   /* AML_NAMESTRING_ARG */

				acpi_ps_get_next_namepath (parser_state, op, &arg_count, 1);
				arg_types = 0;
				break;


			default:

				/* Op is not a constant or string, append each argument */

				while (GET_CURRENT_ARG_TYPE (arg_types) && !arg_count) {
					aml_offset = parser_state->aml - parser_state->aml_start;
					arg = acpi_ps_get_next_arg (parser_state,
							 GET_CURRENT_ARG_TYPE (arg_types),
							 &arg_count);

					if (arg) {
						arg->aml_offset = aml_offset;
					}

					acpi_ps_append_arg (op, arg);
					INCREMENT_ARG_LIST (arg_types);
				}


				/* For a method, save the length and address of the body */

				if (op->opcode == AML_METHOD_OP) {
					deferred_op = acpi_ps_to_deferred_op (op);
					if (deferred_op) {
						/*
						 * Skip parsing of control method or opregion body,
						 * because we don't have enough info in the first pass
						 * to parse them correctly.
						 */

						deferred_op->body       = parser_state->aml;
						deferred_op->body_length = parser_state->pkg_end -
								  parser_state->aml;

						/*
						 * Skip body of method.  For Op_regions, we must continue
						 * parsing because the opregion is not a standalone
						 * package (We don't know where the end is).
						 */
						parser_state->aml       = parser_state->pkg_end;
						arg_count               = 0;
					}
				}

				break;
			}
		}

		if (!arg_count) {
			/* completed Op, prepare for next */

			if (acpi_ps_is_named_op (op->opcode)) {
				if (acpi_gbl_depth) {
					acpi_gbl_depth--;
				}

				if (op->opcode == AML_REGION_OP) {
					deferred_op = acpi_ps_to_deferred_op (op);
					if (deferred_op) {
						/*
						 * Skip parsing of control method or opregion body,
						 * because we don't have enough info in the first pass
						 * to parse them correctly.
						 *
						 * Completed parsing an Op_region declaration, we now
						 * know the length.
						 */

						deferred_op->body_length = parser_state->aml -
								  deferred_op->body;
					}
				}


#ifndef PARSER_ONLY
				data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

				if (op->opcode == AML_NAME_OP) {
					if (op->value.arg) {
						data_type = acpi_ds_map_opcode_to_data_type (
								  (op->value.arg)->opcode, NULL);
						((ACPI_NAMED_OBJECT*)op->acpi_named_object)->type =
								  (u8) data_type;
					}
				}

				/* Pop the scope stack */

				if (acpi_ns_opens_scope (data_type)) {

					acpi_ds_scope_stack_pop (walk_state);
				}
#endif
			}


			parser_state->scope->arg_count--;


			/* Delete op if asked to */

#ifndef PARSER_ONLY
			if (parse_flags & PARSE_DELETE_TREE) {
				acpi_ps_delete_parse_tree (op);
			}
#endif


			if (acpi_ps_has_completed_scope (parser_state)) {
				acpi_ps_pop_scope (parser_state, &op, &arg_types);
			}

			else {
				op = NULL;
			}
		}

		else {
			/* complex argument, push Op and prepare for argument */

			acpi_ps_push_scope (parser_state, op, arg_types, arg_count);
			op = NULL;
		}

	} /* while Parser_state->Aml */


	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_parse_aml
 *
 * PARAMETERS:  Start_scope     - The starting point of the parse.  Becomes the
 *                                root of the parsed op tree.
 *              Aml             - Pointer to the raw AML code to parse
 *              Aml_size        - Length of the AML to parse
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse raw AML and return a tree of ops
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ps_parse_aml (
	ACPI_GENERIC_OP         *start_scope,
	u8                      *aml,
	u32                     aml_size,
	u32                     parse_flags)
{
	ACPI_STATUS             status;
	ACPI_PARSE_STATE        *parser_state;
	ACPI_WALK_STATE         *walk_state;
	ACPI_WALK_LIST          walk_list;
	ACPI_NAMED_OBJECT       *entry = NULL;


	/* Initialize parser state and scope */

	parser_state = acpi_ps_create_state (aml, aml_size);
	if (!parser_state) {
		return (AE_NO_MEMORY);
	}

	acpi_ps_init_scope (parser_state, start_scope);


	/* Initialize a new walk list */

	walk_list.walk_state = NULL;

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT, NULL, NULL, &walk_list);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}


	/* Setup the current scope */

	entry = parser_state->start_op->acpi_named_object;
	if (entry) {
		/* Push start scope on scope stack and make it current  */

		status = acpi_ds_scope_stack_push (entry->child_table, entry->type,
				   walk_state);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

	}


	/* Create the parse tree */

	status = acpi_ps_parse_loop (parser_state, walk_state, parse_flags);


cleanup:

	/* Cleanup */

	acpi_ds_delete_walk_state (walk_state);
	acpi_ps_cleanup_scope (parser_state);
	acpi_cm_free (parser_state);


	return (status);
}


