
/******************************************************************************
 *
 * Name: debugger.h - ACPI/AML debugger
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

#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__


#define DB_MAX_ARGS             8  /* Must be max method args + 1 */

#define DB_COMMAND_PROMPT      '-'
#define DB_EXECUTE_PROMPT      '%'


extern int                      optind;
extern char                     *optarg;
extern u8                       *aml_ptr;
extern u32                      acpi_aml_length;

extern u8                       opt_tables;
extern u8                       opt_disasm;
extern u8                       opt_stats;
extern u8                       opt_parse_jit;
extern u8                       opt_verbose;


extern char                     *args[DB_MAX_ARGS];
extern char                     line_buf[80];
extern char                     scope_buf[40];
extern char                     debug_filename[40];
extern u8                       output_to_file;
extern char                     *buffer;
extern char                     *filename;
extern char                     *INDENT_STRING;
extern u32                      acpi_gbl_method_breakpoint;
extern u8                       acpi_gbl_db_output_flags;
extern u32                      acpi_gbl_db_debug_level;
extern u32                      acpi_gbl_db_console_debug_level;

extern u32                      num_names;
extern u32                      num_methods;
extern u32                      num_regions;
extern u32                      num_packages;
extern u32                      num_aliases;
extern u32                      num_devices;
extern u32                      num_field_defs;
extern u32                      num_thermal_zones;
extern u32                      num_named_objects;
extern u32                      num_grammar_elements;
extern u32                      num_method_elements ;
extern u32                      num_mutexes;
extern u32                      num_power_resources;
extern u32                      num_bank_fields ;
extern u32                      num_index_fields;
extern u32                      num_events;

extern u32                      size_of_parse_tree;
extern u32                      size_of_method_trees;
extern u32                      size_of_nTes;
extern u32                      size_of_acpi_objects;


#define BUFFER_SIZE             4196

#define DB_REDIRECTABLE_OUTPUT  0x01
#define DB_CONSOLE_OUTPUT       0x02
#define DB_DUPLICATE_OUTPUT     0x03


typedef struct command_info
{
	char                    *name;          /* Command Name */
	char                    min_args;       /* Minimum arguments required */

} COMMAND_INFO;


typedef struct argument_info
{
	char                    *name;          /* Argument Name */

} ARGUMENT_INFO;


#define PARAM_LIST(pl)                  pl

#define DBTEST_OUTPUT_LEVEL(lvl)        if (opt_verbose)

#define VERBOSE_PRINT(fp)               DBTEST_OUTPUT_LEVEL(lvl) {\
			  acpi_os_printf PARAM_LIST(fp);}

#define EX_NO_SINGLE_STEP       1
#define EX_SINGLE_STEP          2


/* Prototypes */


/*
 * dbapi - external debugger interfaces
 */

int
acpi_db_initialize (
	void);

ACPI_STATUS
acpi_db_single_step (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op,
	u8                      op_type);


/*
 * dbcmds - debug commands and output routines
 */


void
acpi_db_display_table_info (
	char                    *table_arg);

void
acpi_db_unload_acpi_table (
	char                    *table_arg,
	char                    *instance_arg);

void
acpi_db_set_method_breakpoint (
	char                    *location,
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

void
acpi_db_set_method_call_breakpoint (
	ACPI_GENERIC_OP         *op);

void
acpi_db_disassemble_aml (
	char                    *statements,
	ACPI_GENERIC_OP         *op);

void
acpi_db_dump_namespace (
	char                    *start_arg,
	char                    *depth_arg);

void
acpi_db_dump_namespace_by_owner (
	char                    *owner_arg,
	char                    *depth_arg);

void
acpi_db_send_notify (
	char                    *name,
	u32                     value);

void
acpi_db_set_method_data (
	char                    *type_arg,
	char                    *index_arg,
	char                    *value_arg);

ACPI_STATUS
acpi_db_display_objects (
	char                    *obj_type_arg,
	char                    *display_count_arg);

ACPI_STATUS
acpi_db_find_name_in_namespace (
	char                    *name_arg);

void
acpi_db_set_scope (
	char                    *name);

void
acpi_db_find_references (
	char                    *object_arg);


/*
 * dbdisasm - AML disassembler
 */

void
acpi_db_display_op (
	ACPI_GENERIC_OP         *origin,
	u32                     num_opcodes);

void
acpi_db_display_namestring (
	char                    *name);

void
acpi_db_display_path (
	ACPI_GENERIC_OP         *op);

void
acpi_db_display_opcode (
	ACPI_GENERIC_OP         *op);


/*
 * dbdisply - debug display commands
 */


void
acpi_db_display_method_info (
	ACPI_GENERIC_OP         *op);

void
acpi_db_decode_and_display_object (
	char                    *target,
	char                    *output_type);

void
acpi_db_display_result_object (
	ACPI_OBJECT_INTERNAL    *ret_desc);

ACPI_STATUS
acpi_db_display_all_methods (
	char                    *display_count_arg);

void
acpi_db_display_internal_object (
	ACPI_OBJECT_INTERNAL    *obj_desc);

void
acpi_db_display_arguments (
	void);

void
acpi_db_display_locals (
	void);

void
acpi_db_display_results (
	void);

void
acpi_db_display_calling_tree (
	void);

void
acpi_db_display_argument_object (
	ACPI_OBJECT_INTERNAL    *obj_desc);


/*
 * dbexec - debugger control method execution
 */

void
acpi_db_execute (
	char                    *name,
	char                    **args,
	u32                     flags);

void
acpi_db_create_execution_threads (
	char                    *num_threads_arg,
	char                    *num_loops_arg,
	char                    *method_name_arg);


/*
 * dbfileio - Debugger file I/O commands
 */

OBJECT_TYPE_INTERNAL
acpi_db_match_argument (
	char                    *user_argument,
	ARGUMENT_INFO           *arguments);


void
acpi_db_close_debug_file (
	void);

void
acpi_db_open_debug_file (
	char                    *name);

ACPI_STATUS
acpi_db_load_acpi_table (
	char                    *filename);


/*
 * dbhistry - debugger HISTORY command
 */

void
acpi_db_add_to_history (
	char                    *command_line);

void
acpi_db_display_history (void);

char *
acpi_db_get_from_history (
	char                    *command_num_arg);


/*
 * dbinput - user front-end to the AML debugger
 */

ACPI_STATUS
acpi_db_command_dispatch (
	char                    *input_buffer,
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *op);

void
acpi_db_execute_thread (
	void                    *context);

ACPI_STATUS
acpi_db_user_commands (
	char                    prompt,
	ACPI_GENERIC_OP         *op);


/*
 * dbstats - Generation and display of ACPI table statistics
 */

void
acpi_db_generate_statistics (
	ACPI_GENERIC_OP         *root,
	u8                      is_method);


ACPI_STATUS
acpi_db_display_statistics (
	char                    *type_arg);


/*
 * dbutils - AML debugger utilities
 */

void
acpi_db_set_output_destination (
	s32                     where);

void
acpi_db_dump_buffer (
	u32                     address);

void
acpi_db_dump_object (
	ACPI_OBJECT             *obj_desc,
	u32                     level);

void
acpi_db_prep_namestring (
	char                    *name);


ACPI_STATUS
acpi_db_second_pass_parse (
	ACPI_GENERIC_OP         *root);

ACPI_NAMED_OBJECT*
acpi_db_local_ns_lookup (
	char                    *name);


#endif  /* __DEBUGGER_H__ */
