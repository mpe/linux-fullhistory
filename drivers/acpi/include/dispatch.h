/******************************************************************************
 *
 * Module Name: dispatch.h
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


#ifndef _DISPATCH_H_
#define _DISPATCH_H_


#define NAMEOF_LOCAL_NTE    "__L0"
#define NAMEOF_ARG_NTE      "__A0"


/* For Acpi_ds_method_data_set_value */

#define MTH_TYPE_LOCAL              0
#define MTH_TYPE_ARG                1


/* Common interfaces */

ACPI_STATUS
acpi_ds_obj_stack_push (
	void                    *object,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_obj_stack_pop (
	u32                     pop_count,
	ACPI_WALK_STATE         *walk_state);

void *
acpi_ds_obj_stack_get_value (
	u32                     index,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_obj_stack_pop_object (
	ACPI_OBJECT_INTERNAL    **object,
	ACPI_WALK_STATE         *walk_state);


/* dsregion - Op region support */

ACPI_STATUS
acpi_ds_get_region_arguments (
	ACPI_OBJECT_INTERNAL    *rgn_desc);


/* dsctrl - Parser/Interpreter interface, control stack routines */

/*
ACPI_CTRL_STATE *
Acpi_ds_create_control_state (void);

void
Acpi_ds_push_control_state (
	ACPI_CTRL_STATE         *Control_state,
	ACPI_WALK_STATE         *Walk_state);

ACPI_CTRL_STATE *
Acpi_ds_pop_control_state (
	ACPI_WALK_STATE         *Walk_state);
*/

ACPI_STATUS
acpi_ds_exec_begin_control_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_exec_end_control_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);


/* dsexec - Parser/Interpreter interface, method execution callbacks */

ACPI_STATUS
acpi_ds_exec_begin_op (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_exec_end_op (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op);


/* dsfield - Parser/Interpreter interface for AML fields */


ACPI_STATUS
acpi_ds_create_field (
	ACPI_GENERIC_OP         *op,
	ACPI_HANDLE             region,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_create_bank_field (
	ACPI_GENERIC_OP         *op,
	ACPI_HANDLE             region,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_create_index_field (
	ACPI_GENERIC_OP         *op,
	ACPI_HANDLE             region,
	ACPI_WALK_STATE         *walk_state);


/* dsload - Parser/Interpreter interface, namespace load callbacks */

ACPI_STATUS
acpi_ds_load1_begin_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_load1_end_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_load2_begin_op (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_load2_end_op (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op);


/* dsmthdat - method data (locals/args) */


ACPI_STATUS
acpi_ds_method_data_delete_all (
	ACPI_WALK_STATE         *walk_state);

u8
acpi_ds_is_method_value (
	ACPI_OBJECT_INTERNAL    *obj_desc);

OBJECT_TYPE_INTERNAL
acpi_ds_method_data_get_type (
	u32                     type,
	u32                     index);

ACPI_STATUS
acpi_ds_method_data_get_value (
	u32                     type,
	u32                     index,
	ACPI_OBJECT_INTERNAL    **obj_desc);

ACPI_STATUS
acpi_ds_method_data_set_value (
	u32                     type,
	u32                     index,
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_ds_method_data_delete_value (
	u32                     type,
	u32                     index);

ACPI_STATUS
acpi_ds_method_data_init_args (
	ACPI_OBJECT_INTERNAL    **params,
	u32                     param_count);

ACPI_NAMED_OBJECT*
acpi_ds_method_data_get_nte (
	u32                     type,
	u32                     index);

ACPI_STATUS
acpi_ds_method_data_init (
	ACPI_WALK_STATE         *walk_state);


/* dsmethod - Parser/Interpreter interface - control method parsing */

ACPI_STATUS
acpi_ds_parse_method (
	ACPI_HANDLE             obj_handle);

ACPI_STATUS
acpi_ds_call_control_method (
	ACPI_WALK_LIST          *walk_list,
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_restart_control_method (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    *return_desc);

ACPI_STATUS
acpi_ds_terminate_control_method (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_begin_method_execution (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    *obj_desc);


/* dsobj - Parser/Interpreter interface - object initialization and conversion */

ACPI_STATUS
acpi_ds_init_one_object (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

ACPI_STATUS
acpi_ds_initialize_objects (
	ACPI_TABLE_DESC         *table_desc,
	ACPI_NAMED_OBJECT       *start_entry);

ACPI_STATUS
acpi_ds_build_internal_package_obj (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	ACPI_OBJECT_INTERNAL    **obj_desc);

ACPI_STATUS
acpi_ds_build_internal_object (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	ACPI_OBJECT_INTERNAL    **obj_desc_ptr);

ACPI_STATUS
acpi_ds_init_object_from_op (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	u16                     opcode,
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_ds_create_named_object (
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAMED_OBJECT       *entry,
	ACPI_GENERIC_OP         *op);


/* dsregn - Parser/Interpreter interface - Op Region parsing */

ACPI_STATUS
acpi_ds_eval_region_operands (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

ACPI_STATUS
acpi_ds_initialize_region (
	ACPI_HANDLE             obj_handle);


/* dsutils - Parser/Interpreter interface utility routines */

void
acpi_ds_delete_result_if_not_used (
	ACPI_GENERIC_OP         *op,
	ACPI_OBJECT_INTERNAL    *result_obj,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_create_operand (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *arg);

ACPI_STATUS
acpi_ds_create_operands (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *first_arg);

ACPI_STATUS
acpi_ds_resolve_operands (
	ACPI_WALK_STATE         *walk_state);

OBJECT_TYPE_INTERNAL
acpi_ds_map_opcode_to_data_type (
	u16                     opcode,
	u32                     *out_flags);

OBJECT_TYPE_INTERNAL
acpi_ds_map_named_opcode_to_data_type (
	u16                     opcode);


/*
 * dswscope - Scope Stack manipulation
 */

ACPI_STATUS
acpi_ds_scope_stack_push (
	ACPI_NAME_TABLE         *new_scope,
	OBJECT_TYPE_INTERNAL    type,
	ACPI_WALK_STATE         *walk_state);


ACPI_STATUS
acpi_ds_scope_stack_pop (
	ACPI_WALK_STATE         *walk_state);

void
acpi_ds_scope_stack_clear (
	ACPI_WALK_STATE         *walk_state);


/* Acpi_dswstate - parser WALK_STATE management routines */

ACPI_WALK_STATE *
acpi_ds_create_walk_state (
	ACPI_OWNER_ID           owner_id,
	ACPI_GENERIC_OP         *origin,
	ACPI_OBJECT_INTERNAL    *mth_desc,
	ACPI_WALK_LIST          *walk_list);

ACPI_STATUS
acpi_ds_obj_stack_delete_all (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_obj_stack_pop_and_delete (
	u32                     pop_count,
	ACPI_WALK_STATE         *walk_state);

void
acpi_ds_delete_walk_state (
	ACPI_WALK_STATE         *walk_state);

ACPI_WALK_STATE *
acpi_ds_pop_walk_state (
	ACPI_WALK_LIST          *walk_list);

ACPI_STATUS
acpi_ds_result_stack_pop (
	ACPI_OBJECT_INTERNAL    **object,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_result_stack_push (
	void                    *object,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ds_result_stack_clear (
	ACPI_WALK_STATE         *walk_state);

ACPI_WALK_STATE *
acpi_ds_get_current_walk_state (
	ACPI_WALK_LIST          *walk_list);

void
acpi_ds_delete_walk_state_cache (
	void);


#endif /* _DISPATCH_H_ */