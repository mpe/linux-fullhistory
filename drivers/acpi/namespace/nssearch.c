
/******************************************************************************
 *
 * Module Name: nssearch - Namespace search
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


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nssearch");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_one_scope
 *
 * PARAMETERS:  *Entry_name         - Ascii ACPI name to search for
 *              *Name_table         - Starting table where search will begin
 *              Type                - Object type to match
 *              **Ret_entry         - Where the matched NTE is returned
 *              *Ret_info           - Where info about the search is returned
 *
 * RETURN:      Status and return information via NS_SEARCH_DATA
 *
 * DESCRIPTION: Search a single namespace table.  Performs a simple search,
 *              does not add entries or search parents.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_search_one_scope (
	u32                     entry_name,
	ACPI_NAME_TABLE         *name_table,
	OBJECT_TYPE_INTERNAL    type,
	ACPI_NAMED_OBJECT       **ret_entry,
	NS_SEARCH_DATA          *ret_info)
{
	u32                     position;
	ACPI_NAME_TABLE         *this_table;
	ACPI_NAME_TABLE         *previous_table = name_table;
	ACPI_NAMED_OBJECT       *entries;
	u8                      table_full = TRUE;
	ACPI_NAME_TABLE         *table_with_empty_slots = NULL;
	u32                     empty_slot_position = 0;



	/*
	 * Name tables are built (and subsequently dumped) in the
	 * order in which the names are encountered during the namespace load;
	 *
	 * All namespace searching will be linear;  If a table overflows an
	 * additional segment will be allocated and added (chained).
	 *
	 * Start linear search at top of table
	 */
	position = 0;
	this_table = name_table;
	entries = this_table->entries;


	/* Init return data */

	if (ret_info) {
		ret_info->name_table = this_table;
	}


	/*
	 * Search entire name table, including all linked appendages
	 */

	while (this_table) {
		/*
		 * Search for name in table, starting at Position.  Stop
		 * searching upon examining all entries in the table.
		 *
		 */

		entries = this_table->entries;
		while (position < NS_TABLE_SIZE) {
			/* Check for a valid entry */

			if (!entries[position].name) {
				if (table_full) {
					/*
					 * There is room in the table for more
					 * entries, if necessary
					 */

					table_full = FALSE;
					table_with_empty_slots = this_table;
					empty_slot_position = position;
				}
			}

			/* Search for name in table */

			else if (entries[position].name == entry_name) {
				/*
				 * Found matching entry.  Capture type if
				 * appropriate before returning the entry.
				 */

				/*
				 * The Def_field_defn and Bank_field_defn cases
				 * are actually looking up the Region in which
				 * the field will be defined
				 */

				if ((INTERNAL_TYPE_DEF_FIELD_DEFN == type) ||
					(INTERNAL_TYPE_BANK_FIELD_DEFN == type))
				{
					type = ACPI_TYPE_REGION;
				}

				/*
				 * Scope, Def_any, and Index_field_defn are bogus
				 * "types" which do not actually have anything
				 * to do with the type of the name being looked
				 * up.  For any other value of Type, if the type
				 * stored in the entry is Any (i.e. unknown),
				 * save the actual type.
				 */

				if (type != INTERNAL_TYPE_SCOPE &&
					type != INTERNAL_TYPE_DEF_ANY &&
					type != INTERNAL_TYPE_INDEX_FIELD_DEFN &&
					entries[position].type == ACPI_TYPE_ANY)
				{
					entries[position].type = (u8) type;
				}

				*ret_entry = &entries[position];
				return (AE_OK);
			}


			/* Didn't match name, move on to the next entry */

			position++;
		}


		/*
		 * Just examined last slot in this table, move on
		 *  to next appendate.
		 * All appendages, even to the root NT, contain
		 *  NS_TABLE_SIZE entries.
		 */

		previous_table = this_table;
		this_table = this_table->next_table;

		position = 0;
	}


	/* Searched entire table, not found */


	if (ret_info) {
		/*
		 * Save info on if/where a slot is available
		 * (name was not found)
		 */

		ret_info->table_full = table_full;
		if (table_full) {
			ret_info->name_table = previous_table;
		}

		else {
			ret_info->position  = empty_slot_position;
			ret_info->name_table = table_with_empty_slots;
		}
	}

	return (AE_NOT_FOUND);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_parent_tree
 *
 * PARAMETERS:  *Entry_name         - Ascii ACPI name to search for
 *              *Name_table         - Starting table where search will begin
 *              Type                - Object type to match
 *              **Ret_entry         - Where the matched NTE is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called when a name has not been found in the current namespace
 *              table.  Before adding it or giving up, ACPI scope rules require
 *              searching enclosing scopes in cases identified by Acpi_ns_local().
 *
 *              "A name is located by finding the matching name in the current
 *              name space, and then in the parent name space. If the parent
 *              name space does not contain the name, the search continues
 *              recursively until either the name is found or the name space
 *              does not have a parent (the root of the name space).  This
 *              indicates that the name is not found" (From ACPI Specification,
 *              section 5.3)
 *
 ***************************************************************************/


ACPI_STATUS
acpi_ns_search_parent_tree (
	u32                     entry_name,
	ACPI_NAME_TABLE         *name_table,
	OBJECT_TYPE_INTERNAL    type,
	ACPI_NAMED_OBJECT       **ret_entry)
{
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *parent_entry;
	ACPI_NAMED_OBJECT       *entries;


	entries = name_table->entries;

	/*
	 * If no parent or type is "local", we won't be searching the
	 * parent tree.
	 */

	if (!acpi_ns_local (type) &&
		name_table->parent_entry)
	{
		parent_entry = name_table->parent_entry;
		/*
		 * Search parents until found or we have backed up to
		 * the root
		 */

		while (parent_entry) {
			/* Search parent scope */
			/* TBD: [Investigate] Why ACPI_TYPE_ANY? */

			status = acpi_ns_search_one_scope (entry_name,
					   parent_entry->child_table,
					   ACPI_TYPE_ANY,
					   ret_entry, NULL);

			if (status == AE_OK) {
				return (status);
			}

			/*
			 * Not found here, go up another level
			 * (until we reach the root)
			 */

			parent_entry = acpi_ns_get_parent_entry (parent_entry);
		}

		/* Not found in parent tree */
	}


	return (AE_NOT_FOUND);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_create_and_link_new_table
 *
 * PARAMETERS:  *Name_table         - The table that is to be "extended" by
 *                                    the creation of an appendage table.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate a new namespace table, initialize it, and link it
 *              into the parent table.
 *
 *              NOTE: We are in the first or second pass load mode, want to
 *              add a new table entry, and the current table is full.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_create_and_link_new_table (
	ACPI_NAME_TABLE         *name_table)
{
	ACPI_NAME_TABLE         *new_table;
	ACPI_NAMED_OBJECT       *parent_entry;
	ACPI_STATUS             status = AE_OK;


	/* Sanity check on the data structure */

	if (name_table->next_table) {
		/* We should never get here (an appendage already allocated) */

		return (AE_AML_INTERNAL);
	}


	/*
	 * We can use the parent entries from the current table
	 * Since the parent information remains the same.
	 */
	parent_entry = name_table->parent_entry;


	/* Allocate and chain an appendage to the filled table */

	new_table = acpi_ns_allocate_name_table (NS_TABLE_SIZE);
	if (!new_table) {
		REPORT_ERROR ("Name Table appendage allocation failure");
		return (AE_NO_MEMORY);
	}

	/*
	 * Allocation successful. Init the new table.
	 */
	name_table->next_table = new_table;
	acpi_ns_initialize_table (new_table, parent_entry->child_table,
			 parent_entry);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_initialize_table
 *
 * PARAMETERS:  New_table           - The new table to be initialized
 *              Parent_table        - The parent (owner) scope
 *              Parent_entry        - The NTE for the parent
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a new namespace table.  Simple, but called
 *              from several places -- code should be kept in one place.
 *
 ***************************************************************************/

void
acpi_ns_initialize_table (
	ACPI_NAME_TABLE         *new_table,
	ACPI_NAME_TABLE         *parent_table,
	ACPI_NAMED_OBJECT       *parent_entry)
{
	u8                     i;


	new_table->parent_entry = parent_entry;
	new_table->parent_table = parent_table;


	/* Init each named object entry in the table */

	for (i = 0; i < NS_TABLE_SIZE; i++) {
		new_table->entries[i].this_index = i;
		new_table->entries[i].data_type = ACPI_DESC_TYPE_NAMED;
	}

}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_initialize_entry
 *
 * PARAMETERS:  Name_table      - The containing table for the new NTE
 *              Position        - Position (index) of the new NTE in the table
 *              Entry_name      - ACPI name of the new entry
 *              Type            - ACPI object type of the new entry
 *              Previous_entry  - Link back to the previous entry (can span
 *                                multiple tables)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a new entry within a namespace table.
 *
 ***************************************************************************/

void
acpi_ns_initialize_entry (
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAME_TABLE         *name_table,
	u32                     position,
	u32                     entry_name,
	OBJECT_TYPE_INTERNAL    type)
{
	ACPI_NAMED_OBJECT       *new_entry;
	u16                     owner_id = TABLE_ID_DSDT;
	ACPI_NAMED_OBJECT       *entries;


	/*
	 * Get the owner ID from the Walk state
	 * The owner ID is used to track table deletion and
	 * deletion of objects created by methods
	 */
	if (walk_state) {
		owner_id = walk_state->owner_id;
	}

	/* The new entry is given by two parameters */

	entries = name_table->entries;
	new_entry = &entries[position];

	/* Init the new entry */

	new_entry->data_type     = ACPI_DESC_TYPE_NAMED;
	new_entry->name          = entry_name;
	new_entry->owner_id      = owner_id;
	new_entry->reference_count = 1;


	/*
	 * If adding a name with unknown type, or having to
	 * add the region in order to define fields in it, we
	 * have a forward reference.
	 */

	if ((ACPI_TYPE_ANY == type) ||
		(INTERNAL_TYPE_DEF_FIELD_DEFN == type) ||
		(INTERNAL_TYPE_BANK_FIELD_DEFN == type))
	{
		/*
		 * We don't want to abort here, however!
		 * We will fill in the actual type when the
		 * real definition is found later.
		 */

	}

	/*
	 * The Def_field_defn and Bank_field_defn cases are actually
	 * looking up the Region in which the field will be defined
	 */

	if ((INTERNAL_TYPE_DEF_FIELD_DEFN == type) ||
		(INTERNAL_TYPE_BANK_FIELD_DEFN == type))
	{
		type = ACPI_TYPE_REGION;
	}

	/*
	 * Scope, Def_any, and Index_field_defn are bogus "types" which do
	 * not actually have anything to do with the type of the name
	 * being looked up.  Save any other value of Type as the type of
	 * the entry.
	 */

	if ((type != INTERNAL_TYPE_SCOPE) &&
		(type != INTERNAL_TYPE_DEF_ANY) &&
		(type != INTERNAL_TYPE_INDEX_FIELD_DEFN))
	{
		new_entry->type = (u8) type;
	}

	return;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_and_enter
 *
 * PARAMETERS:  Entry_name          - Ascii ACPI name to search for (4 chars)
 *              *Name_table         - Starting table where search will begin
 *              Interpreter_mode    - Add names only in MODE_Load_pass_x. Otherwise,
 *                                    search only.
 *              Type                - Object type to match
 *              **Ret_entry         - Where the matched NTE is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search for a name segment in a single name table,
 *              optionally adding it if it is not found.  If the passed
 *              Type is not Any and the type previously stored in the
 *              entry was Any (i.e. unknown), update the stored type.
 *
 *              In IMODE_EXECUTE, search only.
 *              In other modes, search and add if not found.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_search_and_enter (
	u32                     entry_name,
	ACPI_WALK_STATE         *walk_state,
	ACPI_NAME_TABLE         *name_table,
	OPERATING_MODE          interpreter_mode,
	OBJECT_TYPE_INTERNAL    type,
	u32                     flags,
	ACPI_NAMED_OBJECT       **ret_entry)
{
	u32                     position;       /* position in table */
	ACPI_STATUS             status;
	NS_SEARCH_DATA          search_info;
	ACPI_NAMED_OBJECT       *entry;
	ACPI_NAMED_OBJECT       *entries;


	/* Parameter validation */

	if (!name_table || !entry_name || !ret_entry) {
		REPORT_ERROR ("Ns_search_and_enter: bad parameter");
		return (AE_BAD_PARAMETER);
	}


	/* Name must consist of printable characters */

	if (!acpi_cm_valid_acpi_name (entry_name)) {
		return (AE_BAD_CHARACTER);
	}


	/* Try to find the name in the table specified by the caller */

	*ret_entry = ENTRY_NOT_FOUND;
	status = acpi_ns_search_one_scope (entry_name, name_table,
			   type, ret_entry, &search_info);
	if (status != AE_NOT_FOUND) {
		/*
		 * Either found it or there was an error
		 * -- finished either way
		 */

		return (status);
	}


	/*
	 * Not found in the table.  If we are NOT performing the
	 * first pass (name entry) of loading the namespace, search
	 * the parent tree (all the way to the root if necessary.)
	 * We don't want to perform the parent search when the
	 * namespace is actually being loaded.  We want to perform
	 * the search when namespace references are being resolved
	 * (load pass 2) and during the execution phase.
	 */

	if ((interpreter_mode != IMODE_LOAD_PASS1) &&
		(flags & NS_SEARCH_PARENT))
	{
		/*
		 * Not found in table - search parent tree according
		 * to ACPI specification
		 */

		status = acpi_ns_search_parent_tree (entry_name, name_table,
				 type, ret_entry);

		if (status == AE_OK) {
			return (status);
		}
	}


	/*
	 * In execute mode, just search, never add names.  Exit now.
	 */

	if (interpreter_mode == IMODE_EXECUTE) {
		return (AE_NOT_FOUND);
	}


	/*
	 * Extract the pertinent info from the search result struct.
	 * Name_table and position might now point to an appendage
	 */
	name_table = search_info.name_table;
	position = search_info.position;


	/*
	 * This block handles the case where the existing table is full.
	 * we must allocate a new table before we can initialize a new entry
	 */

	if (search_info.table_full) {
		status = acpi_ns_create_and_link_new_table (name_table);
		if (status != AE_OK) {
			return (status);
		}

		/* Point to the first slot in the new table */

		name_table = name_table->next_table;
		position = 0;
	}


	/*
	 * There is room in the table (or we have just allocated a new one.)
	 * Initialize the new entry
	 */

	acpi_ns_initialize_entry (walk_state, name_table, position,
			 entry_name, type);


	entries     = name_table->entries;
	*ret_entry  = &entries[position];
	entry       = &entries[position];

	/*
	 * Increment the reference count(s) of all parents up to
	 * the root!
	 */

	while (acpi_ns_get_parent_entry (entry)) {
		entry = acpi_ns_get_parent_entry (entry);
		entry->reference_count++;
	}


	return (AE_OK);
}

