
/******************************************************************************
 *
 * Module Name: nsaccess - Top-level functions for accessing ACPI namespace
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
#include "amlcode.h"
#include "interp.h"
#include "namesp.h"
#include "dispatch.h"


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nsaccess");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_root_create_scope
 *
 * PARAMETERS:  Entry               - NTE for which a scope will be created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a scope table for the given name table entry
 *
 * MUTEX:       Expects namespace to be locked
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_root_create_scope (
	ACPI_NAMED_OBJECT       *entry)
{

	/* Allocate a scope table */

	if (entry->child_table) {
		return (AE_EXIST);
	}

	entry->child_table = acpi_ns_allocate_name_table (NS_TABLE_SIZE);
	if (!entry->child_table) {
		/*  root name table allocation failure  */

		REPORT_ERROR ("Root name table allocation failure");
		return (AE_NO_MEMORY);
	}

	/*
	 * Init the scope first entry -- since it is the exemplar of
	 * the scope (Some fields are duplicated to new entries!)
	 */
	acpi_ns_initialize_table (entry->child_table, NULL, entry);
	return (AE_OK);

}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_root_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate and initialize the root name table
 *
 * MUTEX:       Locks namespace for entire execution
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_root_initialize (void)
{
	ACPI_STATUS             status = AE_OK;
	PREDEFINED_NAMES        *init_val = NULL;
	ACPI_NAMED_OBJECT       *new_entry;
	ACPI_OBJECT_INTERNAL    *obj_desc;


	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	/*
	 * Root is initially NULL, so a non-NULL value indicates
	 * that Acpi_ns_root_initialize() has already been called; just return.
	 */

	if (acpi_gbl_root_object->child_table) {
		status = AE_OK;
		goto unlock_and_exit;
	}


	/* Create the root scope */

	status = acpi_ns_root_create_scope (acpi_gbl_root_object);
	if (ACPI_FAILURE (status)) {
		goto unlock_and_exit;
	}

	/* Enter the pre-defined names in the name table */

	for (init_val = acpi_gbl_pre_defined_names; init_val->name; init_val++) {
		status = acpi_ns_lookup (NULL, init_val->name,
				 (OBJECT_TYPE_INTERNAL) init_val->type,
				 IMODE_LOAD_PASS2, NS_NO_UPSEARCH,
				 NULL, &new_entry);

		/*
		 * if name entered successfully
		 * && its entry in Pre_defined_names[] specifies an
		 * initial value
		 */

		if ((status == AE_OK) &&
			new_entry && init_val->val)
		{
			/*
			 * Entry requests an initial value, allocate a
			 * descriptor for it.
			 */

			obj_desc =
				acpi_cm_create_internal_object (
					(OBJECT_TYPE_INTERNAL) init_val->type);

			if (!obj_desc) {
				status = AE_NO_MEMORY;
				goto unlock_and_exit;
			}

			/*
			 * Convert value string from table entry to
			 * internal representation. Only types actually
			 * used for initial values are implemented here.
			 */

			switch (init_val->type)
			{

			case ACPI_TYPE_NUMBER:

				obj_desc->number.value =
					(u32) STRTOUL (init_val->val, NULL, 10);
				break;


			case ACPI_TYPE_STRING:

				obj_desc->string.length =
						  (u16) STRLEN (init_val->val);

				/*
				 * Allocate a buffer for the string.  All
				 * String.Pointers must be allocated buffers!
				 * (makes deletion simpler)
				 */
				obj_desc->string.pointer =
					acpi_cm_allocate ((ACPI_SIZE)
							 (obj_desc->string.length + 1));

				if (!obj_desc->string.pointer) {
					REPORT_ERROR ("Initial value string"
							  "allocation failure");

					acpi_cm_remove_reference (obj_desc);
					status = AE_NO_MEMORY;
					goto unlock_and_exit;
				}

				STRCPY ((char *) obj_desc->string.pointer,
						init_val->val);
				break;


			case ACPI_TYPE_MUTEX:

				obj_desc->mutex.sync_level =
					(u16) STRTOUL (init_val->val, NULL, 10);

				if (STRCMP (init_val->name, "_GL_") == 0) {
					/*
					 * Create a counting semaphore for the
					 * global lock
					 */
					status =
						acpi_os_create_semaphore (ACPI_NO_UNIT_LIMIT,
								 1, &obj_desc->mutex.semaphore);

					if (ACPI_FAILURE (status)) {
						goto unlock_and_exit;
					}
					/*
					 * We just created the mutex for the
					 * global lock, save it
					 */

					acpi_gbl_global_lock_semaphore =
							   obj_desc->mutex.semaphore;
				}

				else {
					/* Create a mutex */

					status = acpi_os_create_semaphore (1, 1,
							   &obj_desc->mutex.semaphore);

					if (ACPI_FAILURE (status)) {
						goto unlock_and_exit;
					}
				}

				/* TBD: [Restructure] These fields may be obsolete */

				obj_desc->mutex.lock_count = 0;
				obj_desc->mutex.thread_id = 0;
				break;


			default:
				REPORT_ERROR ("Unsupported initial type value");
				acpi_cm_remove_reference (obj_desc);
				obj_desc = NULL;
				continue;
			}

			/* Store pointer to value descriptor in nte */

			acpi_ns_attach_object (new_entry, obj_desc,
					   obj_desc->common.type);
		}
	}


unlock_and_exit:
	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_lookup
 *
 * PARAMETERS:  Prefix_scope    - Search scope if name is not fully qualified
 *              Pathname        - Search pathname, in internal format
 *                                (as represented in the AML stream)
 *              Type            - Type associated with name
 *              Interpreter_mode - IMODE_LOAD_PASS2 => add name if not found
 *              Ret_entry       - Where the new entry (NTE) is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find or enter the passed name in the name space.
 *              Log an error if name not found in Exec mode.
 *
 * MUTEX:       Assumes namespace is locked.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_lookup (
	ACPI_GENERIC_STATE      *scope_info,
	char                    *pathname,
	OBJECT_TYPE_INTERNAL    type,
	OPERATING_MODE          interpreter_mode,
	u32                     flags,
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAMED_OBJECT       **ret_entry)
{
	ACPI_STATUS             status;
	ACPI_NAME_TABLE         *prefix_scope;
	ACPI_NAME_TABLE         *table_to_search = NULL;
	ACPI_NAME_TABLE         *scope_to_push = NULL;
	ACPI_NAMED_OBJECT       *this_entry = NULL;
	u32                     num_segments;
	ACPI_NAME               simple_name;
	u8                      null_name_path = FALSE;
	OBJECT_TYPE_INTERNAL    type_to_check_for;
	OBJECT_TYPE_INTERNAL    this_search_type;

	if (!ret_entry) {
		return (AE_BAD_PARAMETER);
	}


	acpi_gbl_ns_lookup_count++;

	*ret_entry = ENTRY_NOT_FOUND;
	if (!acpi_gbl_root_object->child_table) {
		/*
		 * If the name space has not been initialized:
		 * -  In Pass1 of Load mode, we need to initialize it
		 *    before trying to define a name.
		 * -  In Exec mode, there are no names to be found.
		 */

		if (IMODE_LOAD_PASS1 == interpreter_mode) {
			if ((status = acpi_ns_root_initialize ()) != AE_OK) {
				return (status);
			}
		}
		else {
			return (AE_NOT_FOUND);
		}
	}


	/*
	 * Get the prefix scope.
	 * A null scope means use the root scope
	 */

	if ((!scope_info) ||
		(!scope_info->scope.name_table))
	{
		prefix_scope = acpi_gbl_root_object->child_table;
	}
	else {
		prefix_scope = scope_info->scope.name_table;
	}


	/*
	 * This check is explicitly split provide relax the Type_to_check_for
	 * conditions for Bank_field_defn. Originally, both Bank_field_defn and
	 * Def_field_defn caused Type_to_check_for to be set to ACPI_TYPE_REGION,
	 * but the Bank_field_defn may also check for a Field definition as well
	 * as an Operation_region.
	 */

	if (INTERNAL_TYPE_DEF_FIELD_DEFN == type) {
		/* Def_field_defn defines fields in a Region */

		type_to_check_for = ACPI_TYPE_REGION;
	}

	else if (INTERNAL_TYPE_BANK_FIELD_DEFN == type) {
		/* Bank_field_defn defines data fields in a Field Object */

		type_to_check_for = ACPI_TYPE_ANY;
	}

	else {
		type_to_check_for = type;
	}


	/* Examine the name pointer */

	if (!pathname) {
		/*  8-12-98 ASL Grammar Update supports null Name_path  */

		null_name_path = TRUE;
		num_segments = 0;
		this_entry = acpi_gbl_root_object;

	}

	else {
		/*
		 * Valid name pointer (Internal name format)
		 *
		 * Check for prefixes.  As represented in the AML stream, a
		 * Pathname consists of an optional scope prefix followed by
		 * a segment part.
		 *
		 * If present, the scope prefix is either a Root_prefix (in
		 * which case the name is fully qualified), or zero or more
		 * Parent_prefixes (in which case the name's scope is relative
		 * to the current scope).
		 *
		 * The segment part consists of either:
		 *  - A single 4-byte name segment, or
		 *  - A Dual_name_prefix followed by two 4-byte name segments, or
		 *  - A Multi_name_prefix_op, followed by a byte indicating the
		 *    number of segments and the segments themselves.
		 */

		if (*pathname == AML_ROOT_PREFIX) {
			/* Pathname is fully qualified, look in root name table */

			table_to_search = acpi_gbl_root_object->child_table;
			/* point to segment part */
			pathname++;

			/* Direct reference to root, "\" */

			if (!(*pathname)) {
				this_entry = acpi_gbl_root_object;
				goto check_for_new_scope_and_exit;
			}
		}

		else {
			/* Pathname is relative to current scope, start there */

			table_to_search = prefix_scope;

			/*
			 * Handle up-prefix (carat).  More than one prefix
			 * is supported
			 */

			while (*pathname == AML_PARENT_PREFIX) {

				/* Point to segment part or next Parent_prefix */

				pathname++;

				/*  Backup to the parent's scope  */

				table_to_search = table_to_search->parent_table;
				if (!table_to_search) {
					/* Current scope has no parent scope */

					REPORT_ERROR ("Ns_lookup: Too many parent"
							  "prefixes or scope has no parent");


					return (AE_NOT_FOUND);
				}
			}
		}


		/*
		 * Examine the name prefix opcode, if any,
		 * to determine the number of segments
		 */

		if (*pathname == AML_DUAL_NAME_PREFIX) {
			num_segments = 2;
			/* point to first segment */
			pathname++;

		}

		else if (*pathname == AML_MULTI_NAME_PREFIX_OP) {
			num_segments = (s32)* (u8 *) ++pathname;
			/* point to first segment */
			pathname++;

		}

		else {
			/*
			 * No Dual or Multi prefix, hence there is only one
			 * segment and Pathname is already pointing to it.
			 */
			num_segments = 1;

		}

	}


	/*
	 * Search namespace for each segment of the name.
	 * Loop through and verify/add each name segment.
	 */


	while (num_segments-- && table_to_search) {
		/*
		 * Search for the current segment in the table where
		 * it should be.
		 * Type is significant only at the last (topmost) level.
		 */
		this_search_type = ACPI_TYPE_ANY;
		if (!num_segments) {
			this_search_type = type;
		}

		MOVE_UNALIGNED32_TO_32 (&simple_name, pathname);
		status = acpi_ns_search_and_enter (simple_name, walk_state,
				   table_to_search, interpreter_mode,
				   this_search_type, flags,
				   &this_entry);

		if (status != AE_OK) {
			if (status == AE_NOT_FOUND) {
				/* Name not in ACPI namespace  */

				if (IMODE_LOAD_PASS1 == interpreter_mode ||
					IMODE_LOAD_PASS2 == interpreter_mode)
				{
					REPORT_ERROR ("Name table overflow");
				}

			}

			return (status);
		}


		/*
		 * If 1) last segment (Num_segments == 0)
		 *    2) and looking for a specific type
		 *       (Not checking for TYPE_ANY)
		 *    3) which is not a local type (TYPE_DEF_ANY)
		 *    4) which is not a local type (TYPE_SCOPE)
		 *    5) which is not a local type (TYPE_INDEX_FIELD_DEFN)
		 *    6) and type of entry is known (not TYPE_ANY)
		 *    7) and entry does not match request
		 *
		 * Then we have a type mismatch.  Just warn and ignore it.
		 */
		if ((num_segments     == 0)                               &&
			(type_to_check_for != ACPI_TYPE_ANY)                  &&
			(type_to_check_for != INTERNAL_TYPE_DEF_ANY)          &&
			(type_to_check_for != INTERNAL_TYPE_SCOPE)            &&
			(type_to_check_for != INTERNAL_TYPE_INDEX_FIELD_DEFN) &&
			(this_entry->type != ACPI_TYPE_ANY)                   &&
			(this_entry->type != type_to_check_for))
		{
			/* Complain about type mismatch */

			REPORT_WARNING ("Type mismatch");
		}

		/*
		 * If last segment and not looking for a specific type, but type of
		 * found entry is known, use that type to see if it opens a scope.
		 */

		if ((0 == num_segments) && (ACPI_TYPE_ANY == type)) {
			type = this_entry->type;
		}

		if ((num_segments || acpi_ns_opens_scope (type)) &&
			(this_entry->child_table == NULL))
		{
			/*
			 * More segments or the type implies enclosed scope,
			 * and the next scope has not been allocated.
			 */

			if ((IMODE_LOAD_PASS1 == interpreter_mode) ||
				(IMODE_LOAD_PASS2 == interpreter_mode))
			{
				/*
				 * First or second pass load mode
				 * ==> locate the next scope
				 */

				this_entry->child_table =
					acpi_ns_allocate_name_table (NS_TABLE_SIZE);

				if (!this_entry->child_table) {
					return (AE_NO_MEMORY);
				}
			}

			/* Now complain if there is no next scope */

			if (this_entry->child_table == NULL) {
				if (IMODE_LOAD_PASS1 == interpreter_mode ||
					IMODE_LOAD_PASS2 == interpreter_mode)
				{
					REPORT_ERROR ("Name Table allocation failure");
					return (AE_NOT_FOUND);
				}

				return (AE_NOT_FOUND);
			}


			/* Scope table initialization */

			if (IMODE_LOAD_PASS1 == interpreter_mode ||
				IMODE_LOAD_PASS2 == interpreter_mode)
			{
				/* Initialize the new table */

				acpi_ns_initialize_table (this_entry->child_table,
						 table_to_search,
						 this_entry);
			}
		}

		table_to_search = this_entry->child_table;
		/* point to next name segment */
		pathname += ACPI_NAME_SIZE;
	}


	/*
	 * Always check if we need to open a new scope
	 */

check_for_new_scope_and_exit:

	if (!(flags & NS_DONT_OPEN_SCOPE) && (walk_state)) {
		/*
		 * If entry is a type which opens a scope,
		 * push the new scope on the scope stack.
		 */

		if (acpi_ns_opens_scope (type_to_check_for)) {
			/*  8-12-98 ASL Grammar Update supports null Name_path  */

			if (null_name_path) {
				/* TBD: [Investigate] - is this the correct thing to do? */

				scope_to_push = NULL;
			}
			else {
				scope_to_push = this_entry->child_table;
			}

			status = acpi_ds_scope_stack_push (scope_to_push, type,
					   walk_state);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

		}
	}

	*ret_entry = this_entry;
	return (AE_OK);
}

