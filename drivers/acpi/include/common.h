
/******************************************************************************
 *
 * Name: common.h -- prototypes for the common (subsystem-wide) procedures
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

#ifndef _COMMON_H
#define _COMMON_H


#define REF_INCREMENT       (u16) 0
#define REF_DECREMENT       (u16) 1
#define REF_FORCE_DELETE    (u16) 2

/* Acpi_cm_dump_buffer */

#define DB_BYTE_DISPLAY     1
#define DB_WORD_DISPLAY     2
#define DB_DWORD_DISPLAY    4
#define DB_QWORD_DISPLAY    8


/* Global initialization interfaces */

void
acpi_cm_init_globals (
	ACPI_INIT_DATA *init_data);

void
acpi_cm_terminate (
	void);


/*
 * Acpi_cm_init - miscellaneous initialization and shutdown
 */

ACPI_STATUS
acpi_cm_hardware_initialize (
	void);

ACPI_STATUS
acpi_cm_subsystem_shutdown (
	void);

/*
 * Acpi_cm_global - Global data structures and procedures
 */

char *
acpi_cm_get_mutex_name (
	u32                     mutex_id);

char *
acpi_cm_get_type_name (
	u32                     type);

u8
acpi_cm_valid_object_type (
	u32                     type);

ACPI_OWNER_ID
acpi_cm_allocate_owner_id (
	u32                     id_type);


/*
 * Acpi_cm_clib - Local implementations of C library functions
 */

ACPI_SIZE
acpi_cm_strlen (
	const char              *string);

char *
acpi_cm_strcpy (
	char                    *dst_string,
	const char              *src_string);

char *
acpi_cm_strncpy (
	char                    *dst_string,
	const char              *src_string,
	ACPI_SIZE               count);

u32
acpi_cm_strncmp (
	const char              *string1,
	const char              *string2,
	ACPI_SIZE               count);

u32
acpi_cm_strcmp (
	const char              *string1,
	const char              *string2);

char *
acpi_cm_strcat (
	char                    *dst_string,
	const char              *src_string);

char *
acpi_cm_strncat (
	char                    *dst_string,
	const char              *src_string,
	ACPI_SIZE               count);

u32
acpi_cm_strtoul (
	const char              *string,
	char                    **terminator,
	s32                     base);

char *
acpi_cm_strstr (
	char                    *string1,
	char                    *string2);

char *
acpi_cm_strupr (
	char                    *src_string);

void *
acpi_cm_memcpy (
	void                    *dest,
	const void              *src,
	ACPI_SIZE               count);

void *
acpi_cm_memset (
	void                    *dest,
	s32                     value,
	ACPI_SIZE               count);

s32
acpi_cm_to_upper (
	s32                     c);

s32
acpi_cm_to_lower (
	s32                     c);


/*
 * Acpi_cm_copy - Object construction and conversion interfaces
 */

ACPI_STATUS
acpi_cm_build_simple_object(
	ACPI_OBJECT_INTERNAL    *obj,
	ACPI_OBJECT             *user_obj,
	char                    *data_space,
	u32                     *buffer_space_used);

ACPI_STATUS
acpi_cm_build_package_object (
	ACPI_OBJECT_INTERNAL    *obj,
	char                    *buffer,
	u32                     *space_used);

ACPI_STATUS
acpi_cm_build_external_object (
	ACPI_OBJECT_INTERNAL    *obj,
	ACPI_BUFFER             *ret_buffer);

ACPI_STATUS
acpi_cm_build_internal_simple_object(
	ACPI_OBJECT             *user_obj,
	ACPI_OBJECT_INTERNAL    *obj);

ACPI_STATUS
acpi_cm_build_internal_object (
	ACPI_OBJECT             *obj,
	ACPI_OBJECT_INTERNAL    *internal_obj);

ACPI_STATUS
acpi_cm_copy_internal_simple_object (
	ACPI_OBJECT_INTERNAL    *source_obj,
	ACPI_OBJECT_INTERNAL    *dest_obj);

ACPI_STATUS
acpi_cm_build_copy_internal_package_object (
	ACPI_OBJECT_INTERNAL    *source_obj,
	ACPI_OBJECT_INTERNAL    *dest_obj);


/*
 * Acpi_cm_create - Object creation
 */

ACPI_STATUS
acpi_cm_update_object_reference (
	ACPI_OBJECT_INTERNAL    *object,
	u16                     action);

ACPI_OBJECT_INTERNAL *
_cm_create_internal_object (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id,
	OBJECT_TYPE_INTERNAL    type);


/*
 * Acpi_cm_debug - Debug interfaces
 */

s32
get_debug_level (
	void);

void
set_debug_level (
	s32                     level);

void
function_trace (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name);

void
function_trace_ptr (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	void                    *pointer);

void
function_trace_u32 (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	u32                     integer);

void
function_trace_str (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	char                    *string);

void
function_exit (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name);

void
function_status_exit (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	ACPI_STATUS             status);

void
function_value_exit (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	NATIVE_UINT             value);

void
function_ptr_exit (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             function_name,
	char                    *ptr);

void
debug_print_prefix (
	ACPI_STRING             module_name,
	s32                     line_number);

void
debug_print (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	s32                     print_level,
	char                    *format, ...);

void
debug_print_raw (
	char                    *format, ...);

void
_report_info (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             message);

void
_report_error (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             message);

void
_report_warning (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             message);

void
_report_success (
	ACPI_STRING             module_name,
	s32                     line_number,
	s32                     component_id,
	ACPI_STRING             message);

void
acpi_cm_dump_buffer (
	char                    *buffer,
	u32                     count,
	u32                     display,
	s32                     component_id);


/*
 * Acpi_cm_delete - Object deletion
 */

void
acpi_cm_delete_internal_obj (
	ACPI_OBJECT_INTERNAL    *object);

void
acpi_cm_delete_internal_package_object (
	ACPI_OBJECT_INTERNAL    *object);

void
acpi_cm_delete_internal_simple_object (
	ACPI_OBJECT_INTERNAL    *object);

ACPI_STATUS
acpi_cm_delete_internal_object_list (
	ACPI_OBJECT_INTERNAL    **obj_list);


/*
 * Acpi_cm_eval - object evaluation
 */

/* Method name strings */

#define METHOD_NAME__HID        "_HID"
#define METHOD_NAME__UID        "_UID"
#define METHOD_NAME__ADR        "_ADR"
#define METHOD_NAME__STA        "_STA"
#define METHOD_NAME__REG        "_REG"
#define METHOD_NAME__SEG        "_SEG"
#define METHOD_NAME__BBN        "_BBN"


ACPI_STATUS
acpi_cm_evaluate_numeric_object (
	char                    *method_name,
	ACPI_NAMED_OBJECT       *acpi_device,
	u32                     *address);

ACPI_STATUS
acpi_cm_execute_HID (
	ACPI_NAMED_OBJECT       *acpi_device,
	DEVICE_ID               *hid);

ACPI_STATUS
acpi_cm_execute_STA (
	ACPI_NAMED_OBJECT       *acpi_device,
	u32                     *status_flags);

ACPI_STATUS
acpi_cm_execute_UID (
	ACPI_NAMED_OBJECT       *acpi_device,
	DEVICE_ID               *uid);


/*
 * Acpi_cm_error - exception interfaces
 */

char *
acpi_cm_format_exception (
	ACPI_STATUS             status);


/*
 * Acpi_cm_mutex - mutual exclusion interfaces
 */

ACPI_STATUS
acpi_cm_mutex_initialize (
	void);

void
acpi_cm_mutex_terminate (
	void);

ACPI_STATUS
acpi_cm_create_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_cm_delete_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_cm_acquire_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_cm_release_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);


/*
 * Acpi_cm_object - internal object create/delete/cache routines
 */

#define acpi_cm_create_internal_object(t) _cm_create_internal_object(_THIS_MODULE,__LINE__,_COMPONENT,t)
#define acpi_cm_allocate_object_desc()  _cm_allocate_object_desc(_THIS_MODULE,__LINE__,_COMPONENT)

void *
_cm_allocate_object_desc (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id);

void
acpi_cm_delete_object_desc (
	ACPI_OBJECT_INTERNAL    *object);

u8
acpi_cm_valid_internal_object (
	void                    *object);


/*
 * Acpi_cm_ref_cnt - Object reference count management
 */

void
acpi_cm_add_reference (
	ACPI_OBJECT_INTERNAL    *object);

void
acpi_cm_remove_reference (
	ACPI_OBJECT_INTERNAL    *object);

/*
 * Acpi_cm_size - Object size routines
 */

ACPI_STATUS
acpi_cm_get_simple_object_size (
	ACPI_OBJECT_INTERNAL    *obj,
	u32                     *obj_length);

ACPI_STATUS
acpi_cm_get_package_object_size (
	ACPI_OBJECT_INTERNAL    *obj,
	u32                     *obj_length);

ACPI_STATUS
acpi_cm_get_object_size(
	ACPI_OBJECT_INTERNAL    *obj,
	u32                     *obj_length);


/*
 * Acpi_cm_state - Generic state creation/cache routines
 */

void
acpi_cm_push_generic_state (
	ACPI_GENERIC_STATE      **list_head,
	ACPI_GENERIC_STATE      *state);

ACPI_GENERIC_STATE *
acpi_cm_pop_generic_state (
	ACPI_GENERIC_STATE      **list_head);


ACPI_GENERIC_STATE *
acpi_cm_create_generic_state (
	void);

ACPI_GENERIC_STATE *
acpi_cm_create_update_state (
	ACPI_OBJECT_INTERNAL    *object,
	u16                     action);

ACPI_STATUS
acpi_cm_create_update_state_and_push (
	ACPI_OBJECT_INTERNAL    *object,
	u16                     action,
	ACPI_GENERIC_STATE      **state_list);

ACPI_GENERIC_STATE *
acpi_cm_create_control_state (
	void);

void
acpi_cm_delete_generic_state (
	ACPI_GENERIC_STATE      *state);

void
acpi_cm_delete_generic_state_cache (
	void);

void
acpi_cm_delete_object_cache (
	void);

/*
 * Acpi_cmutils
 */

u8
acpi_cm_valid_acpi_name (
	u32                     name);

u8
acpi_cm_valid_acpi_character (
	char                    character);


/*
 * Memory allocation functions and related macros.
 * Macros that expand to include filename and line number
 */

void *
_cm_allocate (
	u32                     size,
	u32                     component,
	ACPI_STRING             module,
	s32                     line);

void *
_cm_callocate (
	u32                     size,
	u32                     component,
	ACPI_STRING             module,
	s32                     line);

void
_cm_free (
	void                    *address,
	u32                     component,
	ACPI_STRING             module,
	s32                     line);

void
acpi_cm_init_static_object (
	ACPI_OBJECT_INTERNAL    *obj_desc);

#define acpi_cm_allocate(a)             _cm_allocate(a,_COMPONENT,_THIS_MODULE,__LINE__)
#define acpi_cm_callocate(a)            _cm_callocate(a, _COMPONENT,_THIS_MODULE,__LINE__)
#define acpi_cm_free(a)                 _cm_free(a,_COMPONENT,_THIS_MODULE,__LINE__)

#ifndef ACPI_DEBUG

#define acpi_cm_add_element_to_alloc_list(a,b,c,d,e,f)
#define acpi_cm_delete_element_from_alloc_list(a,b,c,d)
#define acpi_cm_dump_current_allocations(a,b)
#define acpi_cm_dump_allocation_info()

#define DECREMENT_OBJECT_METRICS(a)
#define INCREMENT_OBJECT_METRICS(a)
#define INITIALIZE_ALLOCATION_METRICS()

#else

#define INITIALIZE_ALLOCATION_METRICS() \
	acpi_gbl_current_object_count = 0; \
	acpi_gbl_current_object_size = 0; \
	acpi_gbl_running_object_count = 0; \
	acpi_gbl_running_object_size = 0; \
	acpi_gbl_max_concurrent_object_count = 0; \
	acpi_gbl_max_concurrent_object_size = 0; \
	acpi_gbl_current_alloc_size = 0; \
	acpi_gbl_current_alloc_count = 0; \
	acpi_gbl_running_alloc_size = 0; \
	acpi_gbl_running_alloc_count = 0; \
	acpi_gbl_max_concurrent_alloc_size = 0; \
	acpi_gbl_max_concurrent_alloc_count = 0

#define DECREMENT_OBJECT_METRICS(a) \
	acpi_gbl_current_object_count--; \
	acpi_gbl_current_object_size -= a

#define INCREMENT_OBJECT_METRICS(a) \
	acpi_gbl_current_object_count++; \
	acpi_gbl_running_object_count++; \
	if (acpi_gbl_max_concurrent_object_count < acpi_gbl_current_object_count) \
	{ \
		acpi_gbl_max_concurrent_object_count = acpi_gbl_current_object_count; \
	} \
	acpi_gbl_running_object_size += a; \
	acpi_gbl_current_object_size += a; \
	if (acpi_gbl_max_concurrent_object_size < acpi_gbl_current_object_size) \
	{ \
		acpi_gbl_max_concurrent_object_size = acpi_gbl_current_object_size; \
	}


void
acpi_cm_dump_allocation_info (
	void);

void
acpi_cm_dump_current_allocations (
	u32                     component,
	ACPI_STRING             module);

#endif


#endif /* _COMMON_H */
