
/******************************************************************************
 *
 * Name: namesp.h - Namespace subcomponent prototypes and defines
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

#ifndef __NAMESPACE_H__
#define __NAMESPACE_H__

#include "actables.h"


/* To search the entire name space, pass this as Search_base */

#define NS_ALL                  ((ACPI_HANDLE)0)

/*
 * Elements of Acpi_ns_properties are bit significant
 * and should be one-to-one with values of ACPI_OBJECT_TYPE
 */
#define NSP_NORMAL              0
#define NSP_NEWSCOPE            1   /* a definition of this type opens a name scope */
#define NSP_LOCAL               2   /* suppress search of enclosing scopes */


/* Definitions of the predefined namespace names  */

#define ACPI_UNKNOWN_NAME       (u32) 0x3F3F3F3F     /* Unknown name is  "????" */
#define ACPI_ROOT_NAME          (u32) 0x2F202020     /* Root name is     "/   " */
#define ACPI_SYS_BUS_NAME       (u32) 0x5F53425F     /* Sys bus name is  "_SB_" */

#define NS_ROOT_PATH            "/"
#define NS_SYSTEM_BUS           "_SB_"


/* Flags for Acpi_ns_lookup, Acpi_ns_search_and_enter */

#define NS_NO_UPSEARCH          0
#define NS_SEARCH_PARENT        0x01
#define NS_DONT_OPEN_SCOPE      0x02
#define NS_NO_PEER_SEARCH       0x04

#define NS_WALK_UNLOCK          TRUE
#define NS_WALK_NO_UNLOCK       FALSE


ACPI_STATUS
acpi_ns_walk_namespace (
	OBJECT_TYPE_INTERNAL    type,
	ACPI_HANDLE             start_object,
	u32                     max_depth,
	u8                      unlock_before_callback,
	WALK_CALLBACK           user_function,
	void                    *context,
	void                    **return_value);


ACPI_NAMED_OBJECT*
acpi_ns_get_next_object (
	OBJECT_TYPE_INTERNAL    type,
	ACPI_NAMED_OBJECT       *parent,
	ACPI_NAMED_OBJECT       *child);


ACPI_STATUS
acpi_ns_delete_namespace_by_owner (
	u16                     table_id);

void
acpi_ns_free_table_entry (
	ACPI_NAMED_OBJECT       *entry);


/* Namespace loading - nsload */

ACPI_STATUS
acpi_ns_parse_table (
	ACPI_TABLE_DESC         *table_desc,
	ACPI_NAME_TABLE         *scope);

ACPI_STATUS
acpi_ns_load_table (
	ACPI_TABLE_DESC         *table_desc,
	ACPI_NAMED_OBJECT       *entry);

ACPI_STATUS
acpi_ns_load_table_by_type (
	ACPI_TABLE_TYPE         table_type);


/*
 * Top-level namespace access - nsaccess
 */


ACPI_STATUS
acpi_ns_root_initialize (
	void);

ACPI_STATUS
acpi_ns_lookup (
	ACPI_GENERIC_STATE      *scope_info,
	char                    *name,
	OBJECT_TYPE_INTERNAL    type,
	OPERATING_MODE          interpreter_mode,
	u32                     flags,
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAMED_OBJECT       **ret_entry);


/*
 * Table allocation/deallocation - nsalloc
 */

ACPI_NAME_TABLE *
acpi_ns_allocate_name_table (
	u32                     num_entries);

ACPI_STATUS
acpi_ns_delete_namespace_subtree (
	ACPI_NAMED_OBJECT       *parent_handle);

void
acpi_ns_detach_object (
	ACPI_HANDLE             object);

void
acpi_ns_delete_name_table (
	ACPI_NAME_TABLE         *name_table);


/*
 * Namespace modification - nsmodify
 */

ACPI_STATUS
acpi_ns_unload_namespace (
	ACPI_HANDLE             handle);

ACPI_STATUS
acpi_ns_delete_subtree (
	ACPI_HANDLE             start_handle);


/*
 * Namespace dump/print utilities - nsdump
 */

void
acpi_ns_dump_tables (
	ACPI_HANDLE             search_base,
	s32                     max_depth);

void
acpi_ns_dump_entry (
	ACPI_HANDLE             handle,
	u32                     debug_level);

ACPI_STATUS
acpi_ns_dump_pathname (
	ACPI_HANDLE             handle,
	char                    *msg,
	u32                     level,
	u32                     component);

void
acpi_ns_dump_root_devices (
	void);

void
acpi_ns_dump_objects (
	OBJECT_TYPE_INTERNAL    type,
	u32                     max_depth,
	u32                     ownder_id,
	ACPI_HANDLE             start_handle);


/*
 * Namespace evaluation functions - nseval
 */

ACPI_STATUS
acpi_ns_evaluate_by_handle (
	ACPI_NAMED_OBJECT       *object_nte,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object);

ACPI_STATUS
acpi_ns_evaluate_by_name (
	char                    *pathname,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object);

ACPI_STATUS
acpi_ns_evaluate_relative (
	ACPI_NAMED_OBJECT       *object_nte,
	char                    *pathname,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_object);

ACPI_STATUS
acpi_ns_execute_control_method (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_obj_desc);

ACPI_STATUS
acpi_ns_get_object_value (
	ACPI_NAMED_OBJECT       *object_entry,
	ACPI_OBJECT_INTERNAL    **return_obj_desc);


/*
 * Parent/Child/Peer utility functions - nsfamily
 */

ACPI_NAME
acpi_ns_find_parent_name (
	ACPI_NAMED_OBJECT       *entry_to_search);

u8
acpi_ns_exist_downstream_sibling (
	ACPI_NAMED_OBJECT       *this_entry);


/*
 * Scope manipulation - nsscope
 */

s32
acpi_ns_opens_scope (
	OBJECT_TYPE_INTERNAL    type);

char *
acpi_ns_name_of_scope (
	ACPI_NAME_TABLE         *scope);

char *
acpi_ns_name_of_current_scope (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ns_handle_to_pathname (
	ACPI_HANDLE             obj_handle,
	u32                     *buf_size,
	char                    *user_buffer);

u8
acpi_ns_pattern_match (
	ACPI_NAMED_OBJECT       *obj_entry,
	char                    *search_for);

ACPI_STATUS
acpi_ns_name_compare (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

void
acpi_ns_low_find_names (
	ACPI_NAMED_OBJECT       *this_entry,
	char                    *search_for,
	s32                     *count,
	ACPI_HANDLE             list[],
	s32                     max_depth);

ACPI_HANDLE *
acpi_ns_find_names (
	char                    *search_for,
	ACPI_HANDLE             search_base,
	s32                     max_depth);

ACPI_STATUS
acpi_ns_get_named_object (
	char                    *pathname,
	ACPI_NAME_TABLE         *in_scope,
	ACPI_NAMED_OBJECT       **out_nte);

/*
 * Object management for NTEs - nsobject
 */

ACPI_STATUS
acpi_ns_attach_method (
	ACPI_HANDLE             obj_handle,
	u8                      *pcode_addr,
	u32                     pcode_length);

ACPI_STATUS
acpi_ns_attach_object (
	ACPI_HANDLE             obj_handle,
	ACPI_HANDLE             value,
	OBJECT_TYPE_INTERNAL    type);


void *
acpi_ns_compare_value (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *obj_desc);

ACPI_HANDLE
acpi_ns_find_attached_object (
	ACPI_OBJECT_INTERNAL    *obj_desc,
	ACPI_HANDLE             search_base,
	s32                     max_depth);


/*
 * Namespace searching and entry - nssearch
 */

ACPI_STATUS
acpi_ns_search_and_enter (
	u32                     entry_name,
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAME_TABLE         *name_table,
	OPERATING_MODE          interpreter_mode,
	OBJECT_TYPE_INTERNAL    type,
	u32                     flags,
	ACPI_NAMED_OBJECT       **ret_entry);

void
acpi_ns_initialize_table (
	ACPI_NAME_TABLE         *new_table,
	ACPI_NAME_TABLE         *parent_scope,
	ACPI_NAMED_OBJECT       *parent_entry);

ACPI_STATUS
acpi_ns_search_one_scope (
	u32                     entry_name,
	ACPI_NAME_TABLE         *name_table,
	OBJECT_TYPE_INTERNAL    type,
	ACPI_NAMED_OBJECT       **ret_entry,
	NS_SEARCH_DATA          *ret_info);


/*
 * Utility functions - nsutils
 */

u8
acpi_ns_valid_root_prefix (
	char                    prefix);

u8
acpi_ns_valid_path_separator (
	char                    sep);

OBJECT_TYPE_INTERNAL
acpi_ns_get_type (
	ACPI_HANDLE             obj_handle);

void *
acpi_ns_get_attached_object (
	ACPI_HANDLE             obj_handle);

s32
acpi_ns_local (
	OBJECT_TYPE_INTERNAL    type);

ACPI_STATUS
acpi_ns_internalize_name (
	char                    *dotted_name,
	char                    **converted_name);

ACPI_STATUS
acpi_ns_externalize_name (
	u32                     internal_name_length,
	char                    *internal_name,
	u32                     *converted_name_length,
	char                    **converted_name);

s32
is_ns_object (
	ACPI_OBJECT_INTERNAL    *p_oD);

s32
acpi_ns_mark_nS(
	void);

ACPI_NAMED_OBJECT*
acpi_ns_convert_handle_to_entry (
	ACPI_HANDLE             handle);

ACPI_HANDLE
acpi_ns_convert_entry_to_handle(
	ACPI_NAMED_OBJECT*nte);

void
acpi_ns_terminate (
	void);

ACPI_NAMED_OBJECT *
acpi_ns_get_parent_entry (
	ACPI_NAMED_OBJECT       *this_entry);


ACPI_NAMED_OBJECT *
acpi_ns_get_next_valid_entry (
	ACPI_NAMED_OBJECT       *this_entry);


#endif /* __NAMESPACE_H__ */
