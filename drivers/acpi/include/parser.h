/******************************************************************************
 *
 * Module Name: parser.h - AML Parser subcomponent prototypes and defines
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


#ifndef _PARSER_H_
#define _PARSER_H_


#define OP_HAS_RETURN_VALUE         1

/* variable # arguments */

#define ACPI_VAR_ARGS               ACPI_UINT32_MAX

/* maximum virtual address */

#define ACPI_MAX_AML                ((u8 *)(~0UL))


#define PARSE_DELETE_TREE           1


/* psapi - Parser external interfaces */

ACPI_STATUS
acpi_psx_load_table (
	u8                      *pcode_addr,
	s32                     pcode_length);

ACPI_STATUS
acpi_psx_execute (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_obj_desc);


u8
acpi_ps_is_namespace_object_op (
	u16                     opcode);
u8
acpi_ps_is_namespace_op (
	u16                     opcode);


/******************************************************************************
 *
 * Parser interfaces
 *
 *****************************************************************************/


/* psargs - Parse AML opcode arguments */

u8 *
acpi_ps_get_next_package_end (
	ACPI_PARSE_STATE        *parser_state);

char *
acpi_ps_get_next_namestring (
	ACPI_PARSE_STATE        *parser_state);

void
acpi_ps_get_next_simple_arg (
	ACPI_PARSE_STATE        *parser_state,
	s32                     arg_type,       /* type of argument */
	ACPI_GENERIC_OP         *arg);           /* (OUT) argument data */

void
acpi_ps_get_next_namepath (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         *arg,
	u32                     *arg_count,
	u8                      method_call);

ACPI_GENERIC_OP *
acpi_ps_get_next_field (
	ACPI_PARSE_STATE        *parser_state);

ACPI_GENERIC_OP *
acpi_ps_get_next_arg (
	ACPI_PARSE_STATE        *parser_state,
	s32                     arg_type,
	u32                     *arg_count);


/* psopcode - AML Opcode information */

ACPI_OP_INFO *
acpi_ps_get_opcode_info (
	u16                     opcode);

char *
acpi_ps_get_opcode_name (
	u16                     opcode);


/* psparse - top level parsing routines */

void
acpi_ps_delete_parse_tree (
	ACPI_GENERIC_OP         *root);

ACPI_STATUS
acpi_ps_parse_loop (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_WALK_STATE         *walk_state,
	u32                     parse_flags);


ACPI_STATUS
acpi_ps_parse_aml (
	ACPI_GENERIC_OP         *start_scope,
	u8                      *aml,
	u32                     acpi_aml_size,
	u32                     parse_flags);

ACPI_STATUS
acpi_ps_parse_table (
	u8                      *aml,
	s32                     aml_size,
	INTERPRETER_CALLBACK    descending_callback,
	INTERPRETER_CALLBACK    ascending_callback,
	ACPI_GENERIC_OP         **root_object);

u16
acpi_ps_peek_opcode (
	ACPI_PARSE_STATE        *state);


/* psscope - Scope stack management routines */


ACPI_STATUS
acpi_ps_init_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         *root);

ACPI_GENERIC_OP *
acpi_ps_get_parent_scope (
	ACPI_PARSE_STATE        *state);

u8
acpi_ps_has_completed_scope (
	ACPI_PARSE_STATE        *parser_state);

void
acpi_ps_pop_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         **op,
	u32                     *arg_list);

ACPI_STATUS
acpi_ps_push_scope (
	ACPI_PARSE_STATE        *parser_state,
	ACPI_GENERIC_OP         *op,
	u32                     remaining_args,
	u32                     arg_count);

void
acpi_ps_cleanup_scope (
	ACPI_PARSE_STATE        *state);


/* pstree - parse tree manipulation routines */

void
acpi_ps_append_arg(
	ACPI_GENERIC_OP         *op,
	ACPI_GENERIC_OP         *arg);

ACPI_GENERIC_OP*
acpi_ps_find (
	ACPI_GENERIC_OP         *scope,
	char                    *path,
	u16                     opcode,
	u32                     create);

ACPI_GENERIC_OP *
acpi_ps_get_arg(
	ACPI_GENERIC_OP         *op,
	u32                      argn);

ACPI_GENERIC_OP *
acpi_ps_get_child (
	ACPI_GENERIC_OP         *op);

ACPI_GENERIC_OP *
acpi_ps_get_depth_next (
	ACPI_GENERIC_OP         *origin,
	ACPI_GENERIC_OP         *op);


/* pswalk - parse tree walk routines */

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
	INTERPRETER_CALLBACK    ascending_callback);

ACPI_STATUS
acpi_ps_get_next_walk_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	INTERPRETER_CALLBACK    ascending_callback);


/* psutils - parser utilities */

void
acpi_ps_init_op (
	ACPI_GENERIC_OP         *op,
	u16                     opcode);

ACPI_GENERIC_OP *
acpi_ps_alloc_op (
	u16                     opcode);

void
acpi_ps_free_op (
	ACPI_GENERIC_OP         *op);

void
acpi_ps_delete_parse_cache (
	void);

u8
acpi_ps_is_leading_char (
	s32                     c);

u8
acpi_ps_is_prefix_char (
	s32                     c);

u8
acpi_ps_is_named_op (
	u16                     opcode);

u8
acpi_ps_is_named_object_op (
	u16                     opcode);

u8
acpi_ps_is_deferred_op (
	u16                     opcode);

u8
acpi_ps_is_bytelist_op(
	u16                     opcode);

u8
acpi_ps_is_field_op(
	u16                     opcode);

u8
acpi_ps_is_create_field_op (
	u16                     opcode);

ACPI_NAMED_OP*
acpi_ps_to_named_op(
	ACPI_GENERIC_OP         *op);

ACPI_DEFERRED_OP *
acpi_ps_to_deferred_op (
	ACPI_GENERIC_OP         *op);

ACPI_BYTELIST_OP*
acpi_ps_to_bytelist_op(
	ACPI_GENERIC_OP         *op);

u32
acpi_ps_get_name(
	ACPI_GENERIC_OP         *op);

void
acpi_ps_set_name(
	ACPI_GENERIC_OP         *op,
	u32                     name);


/* psdump - display parser tree */

s32
acpi_ps_sprint_path (
	char                    *buffer_start,
	u32                     buffer_size,
	ACPI_GENERIC_OP         *op);

s32
acpi_ps_sprint_op (
	char                    *buffer_start,
	u32                     buffer_size,
	ACPI_GENERIC_OP         *op);

void
acpi_ps_show (
	ACPI_GENERIC_OP         *op);


#endif /* _PARSER_H_ */
