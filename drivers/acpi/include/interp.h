
/******************************************************************************
 *
 * Name: interp.h - Interpreter subcomponent prototypes and defines
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

#ifndef __INTERP_H__
#define __INTERP_H__


#include "actypes.h"
#include "acobject.h"


#define WALK_OPERANDS       &(walk_state->operands [walk_state->num_operands -1])


/* Interpreter constants */

#define AML_END_OF_BLOCK            -1
#define PUSH_PKG_LENGTH             1
#define DO_NOT_PUSH_PKG_LENGTH      0


#define STACK_TOP                   0
#define STACK_BOTTOM                (u32) -1

/* Constants for global "When_to_parse_methods" */

#define METHOD_PARSE_AT_INIT        0x0
#define METHOD_PARSE_JUST_IN_TIME   0x1
#define METHOD_DELETE_AT_COMPLETION 0x2


ACPI_STATUS
acpi_aml_resolve_operands (
	u16                     opcode,
	ACPI_OBJECT_INTERNAL    **stack_ptr);


/*
 * amxface - External interpreter interfaces
 */

ACPI_STATUS
acpi_aml_load_table (
	ACPI_TABLE_TYPE         table_id);

ACPI_STATUS
acpi_aml_execute_method (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_obj_desc);


/*
 * amcopy - Interpreter object copy support
 */

ACPI_STATUS
acpi_aml_build_copy_internal_package_object (
	ACPI_OBJECT_INTERNAL    *source_obj,
	ACPI_OBJECT_INTERNAL    *dest_obj);


/*
 * amfield - ACPI AML (p-code) execution - field manipulation
 */


ACPI_STATUS
acpi_aml_read_field (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	void                    *buffer,
	u32                     buffer_length,
	u32                     byte_length,
	u32                     datum_length,
	u32                     bit_granularity,
	u32                     byte_granularity);

ACPI_STATUS
acpi_aml_write_field (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	void                    *buffer,
	u32                     buffer_length,
	u32                     byte_length,
	u32                     datum_length,
	u32                     bit_granularity,
	u32                     byte_granularity);

ACPI_STATUS
acpi_aml_setup_field (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	ACPI_OBJECT_INTERNAL    *rgn_desc,
	s32                     field_bit_width);

ACPI_STATUS
acpi_aml_read_field_data (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	u32                     field_byte_offset,
	u32                     field_bit_width,
	u32                     *value);

ACPI_STATUS
acpi_aml_access_named_field (
	s32                     mode,
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     length);

ACPI_STATUS
acpi_aml_set_named_field_value (
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     length);

ACPI_STATUS
acpi_aml_get_named_field_value (
	ACPI_HANDLE             named_field,
	void                    *buffer,
	u32                     length);


/*
 * ammisc - ACPI AML (p-code) execution - specific opcodes
 */

ACPI_STATUS
acpi_aml_exec_create_field (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_reconfiguration (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_fatal (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_index (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);

ACPI_STATUS
acpi_aml_exec_match (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);

ACPI_STATUS
acpi_aml_exec_create_mutex (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_create_processor (
	ACPI_GENERIC_OP         *op,
	ACPI_HANDLE             processor_nTE);

ACPI_STATUS
acpi_aml_exec_create_power_resource (
	ACPI_GENERIC_OP         *op,
	ACPI_HANDLE             processor_nTE);

ACPI_STATUS
acpi_aml_exec_create_region (
	u8                      *aml_ptr,
	u32                     acpi_aml_length,
	u32                     region_space,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_create_event (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_create_alias (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_create_method (
	u8                      *aml_ptr,
	u32                     acpi_aml_length,
	u32                     method_flags,
	ACPI_HANDLE             method);


/*
 * amprep - ACPI AML (p-code) execution - prep utilities
 */

ACPI_STATUS
acpi_aml_prep_def_field_value (
	ACPI_NAMED_OBJECT       *this_entry,
	ACPI_HANDLE             region,
	u8                      field_flags,
	u8                      field_attribute,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_aml_prep_bank_field_value (
	ACPI_NAMED_OBJECT       *this_entry,
	ACPI_HANDLE             region,
	ACPI_HANDLE             bank_reg,
	u32                     bank_val,
	u8                      field_flags,
	u8                      field_attribute,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_aml_prep_index_field_value (
	ACPI_NAMED_OBJECT       *this_entry,
	ACPI_HANDLE             index_reg,
	ACPI_HANDLE             data_reg,
	u8                      field_flags,
	u8                      field_attribute,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_aml_prep_operands (
	char                    *types,
	ACPI_OBJECT_INTERNAL    **stack_ptr);


/*
 * iepstack - package stack utilities
 */

/*
u32
Acpi_aml_pkg_stack_level (
	 void);

void
Acpi_aml_clear_pkg_stack (
	void);

ACPI_STATUS
Acpi_aml_pkg_push_length (
	u32                     Length,
	OPERATING_MODE          Load_exec_mode);

ACPI_STATUS
Acpi_aml_pkg_push_exec_length (
	u32                     Length);

ACPI_STATUS
Acpi_aml_pkg_push_exec (
	u8                      *Code,
	u32                     Len);

ACPI_STATUS
Acpi_aml_pkg_pop_length (
	s32                     No_err_under,
	OPERATING_MODE          Load_exec_mode);

ACPI_STATUS
Acpi_aml_pkg_pop_exec_length (
	void);

ACPI_STATUS
Acpi_aml_pkg_pop_exec (
	void);

*/

/*
 * amsystem - Interface to OS services
 */

u16
acpi_aml_system_thread_id (
	void);

ACPI_STATUS
acpi_aml_system_do_notify_op (
	ACPI_OBJECT_INTERNAL    *value,
	ACPI_OBJECT_INTERNAL    *obj_desc);

void
acpi_aml_system_do_suspend(
	u32                     time);

void
acpi_aml_system_do_stall (
	u32                     time);

ACPI_STATUS
acpi_aml_system_acquire_mutex(
	ACPI_OBJECT_INTERNAL    *time,
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_aml_system_release_mutex(
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_aml_system_signal_event(
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_aml_system_wait_event(
	ACPI_OBJECT_INTERNAL    *time,
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_aml_system_reset_event(
	ACPI_OBJECT_INTERNAL    *obj_desc);

ACPI_STATUS
acpi_aml_system_wait_semaphore (
	ACPI_HANDLE             semaphore,
	u32                     timeout);


/*
 * ammonadic - ACPI AML (p-code) execution, monadic operators
 */

ACPI_STATUS
acpi_aml_exec_monadic1 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_monadic2 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);

ACPI_STATUS
acpi_aml_exec_monadic2_r (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);


/*
 * amdyadic - ACPI AML (p-code) execution, dyadic operators
 */

ACPI_STATUS
acpi_aml_exec_dyadic1 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_aml_exec_dyadic2 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);

ACPI_STATUS
acpi_aml_exec_dyadic2_r (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);

ACPI_STATUS
acpi_aml_exec_dyadic2_s (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OBJECT_INTERNAL    **return_desc);


/*
 * amresolv  - Object resolution and get value functions
 */

ACPI_STATUS
acpi_aml_resolve_to_value (
	ACPI_OBJECT_INTERNAL    **stack_ptr);

ACPI_STATUS
acpi_aml_resolve_entry_to_value (
	ACPI_NAMED_OBJECT       **stack_ptr);

ACPI_STATUS
acpi_aml_resolve_object_to_value (
	ACPI_OBJECT_INTERNAL    **stack_ptr);

ACPI_STATUS
acpi_aml_get_field_unit_value (
	ACPI_OBJECT_INTERNAL    *field_desc,
	ACPI_OBJECT_INTERNAL    *result_desc);


/*
 * amcode - Scanner AML code manipulation routines
 */

s32
acpi_aml_avail (
	ACPI_SIZE               n);

s32
acpi_aml_peek (
	void);

s32
acpi_aml_get_pcode_byte (
	u8                      *pcode);

u16
acpi_aml_peek_op (
	void);

u8 *
acpi_aml_consume_bytes (
	ACPI_SIZE               bytes);

ACPI_SIZE
acpi_aml_consume_stream_bytes (
	ACPI_SIZE               bytes_to_get,
	u8                      *aml_buffer);

void
acpi_aml_consume_package (
	OPERATING_MODE          load_exec_mode);

void
acpi_aml_set_pcode_input (
	u8                      *base,
	u32                     length);

ACPI_STATUS
acpi_aml_set_method (
	void                    *object);

ACPI_STATUS
acpi_aml_prep_exec (
	u8                      *pcode,
	u32                     pcode_length);

ACPI_HANDLE
acpi_aml_get_pcode_handle (
	void);

void
acpi_aml_get_current_location (
	ACPI_OBJECT_INTERNAL    *method_desc);

void
acpi_aml_set_current_location (
	ACPI_OBJECT_INTERNAL    *method_desc);


/*
 * amdump - Scanner debug output routines
 */

void
acpi_aml_show_hex_value (
	s32                     byte_count,
	u8                      *aml_ptr,
	s32                     lead_space);

void
acpi_aml_dump_buffer (
	ACPI_SIZE               length);


ACPI_STATUS
acpi_aml_dump_operand (
	ACPI_OBJECT_INTERNAL    *entry_desc);

void
acpi_aml_dump_operands (
	ACPI_OBJECT_INTERNAL    **operands,
	OPERATING_MODE          interpreter_mode,
	char                    *ident,
	s32                     num_levels,
	char                    *note,
	char                    *module_name,
	s32                     line_number);

void
acpi_aml_dump_object_descriptor (
	ACPI_OBJECT_INTERNAL    *object,
	u32                     flags);


void
acpi_aml_dump_acpi_named_object (
	ACPI_NAMED_OBJECT       *entry,
	u32                     flags);


/*
 * amnames - interpreter/scanner name load/execute
 */

char *
acpi_aml_allocate_name_string (
	u32                     prefix_count,
	u32                     num_name_segs);

s32
acpi_aml_good_char (
	s32                     character);

ACPI_STATUS
acpi_aml_exec_name_segment (
	u8                      **in_aml_address,
	char                    *name_string);

ACPI_STATUS
acpi_aml_get_name_string (
	OBJECT_TYPE_INTERNAL    data_type,
	u8                      *in_aml_address,
	char                    **out_name_string,
	u32                     *out_name_length);

u32
acpi_aml_decode_package_length (
	u32                     last_pkg_len);


ACPI_STATUS
acpi_aml_do_name (
	ACPI_OBJECT_TYPE        data_type,
	OPERATING_MODE          load_exec_mode);


/*
 * amstore - Object store support
 */

ACPI_STATUS
acpi_aml_exec_store (
	ACPI_OBJECT_INTERNAL    *op1,
	ACPI_OBJECT_INTERNAL    *res);

ACPI_STATUS
acpi_aml_store_object_to_object (
	ACPI_OBJECT_INTERNAL    *val_desc,
	ACPI_OBJECT_INTERNAL    *dest_desc);

ACPI_STATUS
acpi_aml_store_object_to_nte (
	ACPI_OBJECT_INTERNAL    *val_desc,
	ACPI_NAMED_OBJECT       *entry);


/*
 * amutils - interpreter/scanner utilities
 */

void
acpi_aml_enter_interpreter (
	void);

void
acpi_aml_exit_interpreter (
	void);

u8
acpi_aml_validate_object_type (
	ACPI_OBJECT_TYPE        type);

u8
acpi_aml_acquire_global_lock (
	u32                     rule);

ACPI_STATUS
acpi_aml_release_global_lock (
	u8                      locked);

void
acpi_aml_append_operand_diag(
	char                    *name,
	s32                     line,
	u16                     op_code,
	ACPI_OBJECT_INTERNAL    **operands,
	s32                     Noperands);

u32
acpi_aml_buf_seq (
	void);

s32
acpi_aml_digits_needed (
	s32                     value,
	s32                     base);

ACPI_STATUS
acpi_aml_eisa_id_to_string (
	u32                     numeric_id,
	char                    *out_string);


/*
 * amregion - default Op_region handlers
 */

ACPI_STATUS
acpi_aml_system_memory_space_handler (
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value,
	void                    *context);

ACPI_STATUS
acpi_aml_system_io_space_handler (
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value,
	void                    *context);

ACPI_STATUS
acpi_aml_pci_config_space_handler (
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value,
	void                    *context);

ACPI_STATUS
acpi_aml_embedded_controller_space_handler (
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value,
	void                    *context);

ACPI_STATUS
acpi_aml_sm_bus_space_handler (
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value,
	void                    *context);


#endif /* __INTERP_H__ */
