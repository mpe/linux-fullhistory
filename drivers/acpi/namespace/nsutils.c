
/******************************************************************************
 *
 * Module Name: nsutils - Utilities for accessing ACPI namespace, accessing
 *                          parents and siblings and Scope manipulation
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
#include "namesp.h"
#include "interp.h"
#include "amlcode.h"
#include "tables.h"

#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nsutils");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_valid_root_prefix
 *
 * PARAMETERS:  Prefix          - Character to be checked
 *
 * RETURN:      TRUE if a valid prefix
 *
 * DESCRIPTION: Check if a character is a valid ACPI Root prefix
 *
 ***************************************************************************/

u8
acpi_ns_valid_root_prefix (
	char                    prefix)
{

	return ((u8) (prefix == '\\'));
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_valid_path_separator
 *
 * PARAMETERS:  Sep              - Character to be checked
 *
 * RETURN:      TRUE if a valid path separator
 *
 * DESCRIPTION: Check if a character is a valid ACPI path separator
 *
 ***************************************************************************/

u8
acpi_ns_valid_path_separator (
	char                    sep)
{

	return ((u8) (sep == '.'));
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_type
 *
 * PARAMETERS:  Handle              - Handle of nte to be examined
 *
 * RETURN:      Type field from nte whose handle is passed
 *
 ***************************************************************************/

OBJECT_TYPE_INTERNAL
acpi_ns_get_type (
	ACPI_HANDLE             handle)
{

	if (!handle) {
		/*  Handle invalid  */

		REPORT_WARNING ("Ns_get_type: Null handle");
		return (ACPI_TYPE_ANY);
	}

	return (((ACPI_NAMED_OBJECT*) handle)->type);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_local
 *
 * PARAMETERS:  Type            - A namespace object type
 *
 * RETURN:      LOCAL if names must be found locally in objects of the
 *              passed type, 0 if enclosing scopes should be searched
 *
 ***************************************************************************/

s32
acpi_ns_local (
	OBJECT_TYPE_INTERNAL    type)
{

	if (!acpi_cm_valid_object_type (type)) {
		/*  type code out of range  */

		REPORT_WARNING ("Ns_local: Invalid Object Type");
		return (NSP_NORMAL);
	}

	return ((s32) acpi_gbl_ns_properties[type] & NSP_LOCAL);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_internalize_name
 *
 * PARAMETERS:  *External_name            - External representation of name
 *              **Converted Name        - Where to return the resulting
 *                                        internal represention of the name
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external representation (e.g. "\_PR_.CPU0")
 *              to internal form (e.g. 5c 2f 02 5f 50 52 5f 43 50 55 30)
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_internalize_name (
	char                    *external_name,
	char                    **converted_name)
{
	char                    *result = NULL;
	char                    *internal_name;
	ACPI_SIZE               num_segments;
	u8                      fully_qualified = FALSE;
	u32                     i;


	if ((!external_name)     ||
		(*external_name == 0) ||
		(!converted_name))
	{
		return (AE_BAD_PARAMETER);
	}


	/*
	 * For the internal name, the required length is 4 bytes
	 * per segment, plus 1 each for Root_prefix, Multi_name_prefix_op,
	 * segment count, trailing null (which is not really needed,
	 * but no there's harm in putting it there)
	 *
	 * strlen() + 1 covers the first Name_seg, which has no
	 * path separator
	 */

	if (acpi_ns_valid_root_prefix (external_name[0])) {
		fully_qualified = TRUE;
		external_name++;
	}


	/*
	 * Determine the number of ACPI name "segments" by counting
	 * the number of path separators within the string.  Start
	 * with one segment since the segment count is (# separators)
	 * + 1, and zero separators is ok.
	 */

	num_segments = 1;
	for (i = 0; external_name[i]; i++) {
		if (acpi_ns_valid_path_separator (external_name[i])) {
			num_segments++;
		}
	}


	/* We need a segment to store the internal version of the name */

	internal_name = acpi_cm_callocate ((ACPI_NAME_SIZE * num_segments) + 4);
	if (!internal_name) {
		return (AE_NO_MEMORY);
	}


	/* Setup the correct prefixes, counts, and pointers */

	if (fully_qualified) {
		internal_name[0] = '\\';
		internal_name[1] = AML_MULTI_NAME_PREFIX_OP;
		internal_name[2] = (char) num_segments;
		result = &internal_name[3];
	}
	else {
		internal_name[0] = AML_MULTI_NAME_PREFIX_OP;
		internal_name[1] = (char) num_segments;
		result = &internal_name[2];
	}


	/* Build the name (minus path separators) */

	for (; num_segments; num_segments--) {
		for (i = 0; i < ACPI_NAME_SIZE; i++) {
			if (acpi_ns_valid_path_separator (*external_name) ||
			   (*external_name == 0))
			{
				/*
				 * Pad the segment with underscore(s) if
				 * segment is short
				 */

				result[i] = '_';
			}

			else {
				/* Convert char to uppercase and save it */

				result[i] = (char) TOUPPER (*external_name);
				external_name++;
			}

		}

		/* Now we must have a path separator, or the pathname is bad */

		if (!acpi_ns_valid_path_separator (*external_name) &&
			(*external_name != 0))
		{
			acpi_cm_free (internal_name);
			return (AE_BAD_PARAMETER);
		}

		/* Move on the next segment */

		external_name++;
		result += ACPI_NAME_SIZE;
	}


	/* Return the completed name */

	/* Terminate the string! */
	*result = 0;
	*converted_name = internal_name;



	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_externalize_name
 *
 * PARAMETERS:  *Internal_name         - Internal representation of name
 *              **Converted_name       - Where to return the resulting
 *                                        external representation of name
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert internal name (e.g. 5c 2f 02 5f 50 52 5f 43 50 55 30)
 *              to its external form (e.g. "\_PR_.CPU0")
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ns_externalize_name (
	u32                     internal_name_length,
	char                    *internal_name,
	u32                     *converted_name_length,
	char                    **converted_name)
{
	u32                     prefix_length = 0;
	u32                     names_index = 0;
	u32                     names_count = 0;
	u32                     i = 0;
	u32                     j = 0;

	if (internal_name_length < 0 ||
		!internal_name ||
		!converted_name_length ||
		!converted_name)
	{
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Check for a prefix (one '\' | one or more '^').
	 */
	switch (internal_name[0])
	{
	case '\\':
		prefix_length = 1;
		break;

	case '^':
		for (i = 0; i < internal_name_length; i++) {
			if (internal_name[i] != '^') {
				prefix_length = i + 1;
			}
		}

		if (i == internal_name_length) {
			prefix_length = i;
		}

		break;
	}

	/*
	 * Check for object names.  Note that there could be 0-255 of these
	 * 4-byte elements.
	 */
	if (prefix_length < internal_name_length) {
		switch (internal_name[prefix_length])
		{

		/* <count> 4-byte names */

		case AML_MULTI_NAME_PREFIX_OP:
			names_index = prefix_length + 2;
			names_count = (u32) internal_name[prefix_length + 1];
			break;


		/* two 4-byte names */

		case AML_DUAL_NAME_PREFIX:
			names_index = prefix_length + 1;
			names_count = 2;
			break;


		/* Null_name */

		case 0:
			names_index = 0;
			names_count = 0;
			break;


		/* one 4-byte name */

		default:
			names_index = prefix_length;
			names_count = 1;
			break;
		}
	}

	/*
	 * Calculate the length of Converted_name, which equals the length
	 * of the prefix, length of all object names, length of any required
	 * punctuation ('.') between object names, plus the NULL terminator.
	 */
	*converted_name_length = prefix_length + (4 * names_count) +
			   ((names_count > 0) ? (names_count - 1) : 0) + 1;

	/*
	 * Check to see if we're still in bounds.  If not, there's a problem
	 * with Internal_name (invalid format).
	 */
	if (*converted_name_length > internal_name_length) {
		REPORT_ERROR ("Ns_externalize_name: Invalid internal name.\n");
		return (AE_BAD_PATHNAME);
	}

	/*
	 * Build Converted_name...
	 */

	(*converted_name) = acpi_cm_callocate (*converted_name_length);
	if (!(*converted_name)) {
		return (AE_NO_MEMORY);
	}

	j = 0;

	for (i = 0; i < prefix_length; i++) {
		(*converted_name)[j++] = internal_name[i];
	}

	if (names_count > 0) {
		for (i = 0; i < names_count; i++) {
			if (i > 0) {
				(*converted_name)[j++] = '.';
			}

			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
		}
	}

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_convert_handle_to_entry
 *
 * PARAMETERS:  Handle          - Handle to be converted to an NTE
 *
 * RETURN:      A Name table entry pointer
 *
 * DESCRIPTION: Convert a namespace handle to a real NTE
 *
 ****************************************************************************/

ACPI_NAMED_OBJECT*
acpi_ns_convert_handle_to_entry (
	ACPI_HANDLE             handle)
{

	/*
	 * Simple implementation for now;
	 * TBD: [Future] Real integer handles allow for more verification
	 * and keep all pointers within this subsystem!
	 */

	if (!handle) {
		return NULL;
	}

	if (handle == ACPI_ROOT_OBJECT) {
		return acpi_gbl_root_object;
	}


	/* We can at least attempt to verify the handle */

	if (!VALID_DESCRIPTOR_TYPE (handle, ACPI_DESC_TYPE_NAMED)) {
		return NULL;
	}

	return (ACPI_NAMED_OBJECT*) handle;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_convert_entry_to_handle
 *
 * PARAMETERS:  Nte          - NTE to be converted to a Handle
 *
 * RETURN:      An USER ACPI_HANDLE
 *
 * DESCRIPTION: Convert a real NTE to a namespace handle
 *
 ****************************************************************************/

ACPI_HANDLE
acpi_ns_convert_entry_to_handle(ACPI_NAMED_OBJECT*nte)
{


	/*
	 * Simple implementation for now;
	 * TBD: [Future] Real integer handles allow for more verification
	 * and keep all pointers within this subsystem!
	 */

	return (ACPI_HANDLE) nte;


/* ---------------------------------------------------

	if (!Nte) {
		return NULL;
	}

	if (Nte == Acpi_gbl_Root_object) {
		return ACPI_ROOT_OBJECT;
	}


	return (ACPI_HANDLE) Nte;
------------------------------------------------------*/
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ns_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: free memory allocated for table storage.
 *
 ******************************************************************************/

void
acpi_ns_terminate (void)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_NAMED_OBJECT       *entry;


	entry = acpi_gbl_root_object;

	/*
	 * 1) Free the entire namespace -- all objects, tables, and stacks
	 */
	/*
	 * Delete all objects linked to the root
	 * (additional table descriptors)
	 */

	acpi_ns_delete_namespace_subtree (entry);

	/* Detach any object(s) attached to the root */

	obj_desc = acpi_ns_get_attached_object (entry);
	if (obj_desc) {
		acpi_ns_detach_object (entry);
		acpi_cm_remove_reference (obj_desc);
	}

	acpi_ns_delete_name_table (entry->child_table);
	entry->child_table = NULL;


	REPORT_SUCCESS ("Entire namespace and objects deleted");

	/*
	 * 2) Now we can delete the ACPI tables
	 */

	acpi_tb_delete_acpi_tables ();

	return;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_opens_scope
 *
 * PARAMETERS:  Type        - A valid namespace type
 *
 * RETURN:      NEWSCOPE if the passed type "opens a name scope" according
 *              to the ACPI specification, else 0
 *
 ***************************************************************************/

s32
acpi_ns_opens_scope (
	OBJECT_TYPE_INTERNAL    type)
{

	if (!acpi_cm_valid_object_type (type)) {
		/* type code out of range  */

		REPORT_WARNING ("Ns_opens_scope: Invalid Object Type");
		return (NSP_NORMAL);
	}

	return (((s32) acpi_gbl_ns_properties[type]) & NSP_NEWSCOPE);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_named_object
 *
 * PARAMETERS:  *Pathname   - Name to be found, in external (ASL) format. The
 *                            \ (backslash) and ^ (carat) prefixes, and the
 *                            . (period) to separate segments are supported.
 *              In_scope    - Root of subtree to be searched, or NS_ALL for the
 *                            root of the name space.  If Name is fully
 *                            qualified (first char is '\'), the passed value
 *                            of Scope will not be accessed.
 *              Out_nte     - Where the Nte is returned
 *
 * DESCRIPTION: Look up a name relative to a given scope and return the
 *              corresponding NTE.  NOTE: Scope can be null.
 *
 * MUTEX:       Locks namespace
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_get_named_object (
	char                    *pathname,
	ACPI_NAME_TABLE         *in_scope,
	ACPI_NAMED_OBJECT       **out_nte)
{
	ACPI_GENERIC_STATE      scope_info;
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *obj_entry = NULL;
	char                    *internal_path = NULL;


	scope_info.scope.name_table = in_scope;

	/* Ensure that the namespace has been initialized */

	if (!acpi_gbl_root_object->child_table) {
		return (AE_NO_NAMESPACE);
	}

	if (!pathname) {
		return (AE_BAD_PARAMETER);
	}


	/* Convert path to internal representation */

	status = acpi_ns_internalize_name (pathname, &internal_path);
	if (ACPI_FAILURE (status)) {
		return (status);
	}


	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	/* NS_ALL means start from the root */

	if (NS_ALL == scope_info.scope.name_table) {
		scope_info.scope.name_table = acpi_gbl_root_object->child_table;
	}

	else {
		scope_info.scope.name_table = in_scope;
		if (!scope_info.scope.name_table) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup (&scope_info, internal_path,
			 ACPI_TYPE_ANY, IMODE_EXECUTE,
			 NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			 NULL, &obj_entry);


	/* Return what was wanted - the NTE that matches the name */

	*out_nte = obj_entry;


unlock_and_exit:

	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

	/* Cleanup */

	acpi_cm_free (internal_path);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_find_parent_name
 *
 * PARAMETERS:  *Child_entry            - nte whose name is to be found
 *
 * RETURN:      The ACPI name
 *
 * DESCRIPTION: Search for the given nte in its parent scope and return the
 *              name segment, or "????" if the parent name can't be found
 *              (which "should not happen").
 *
 ***************************************************************************/

ACPI_NAME
acpi_ns_find_parent_name (
	ACPI_NAMED_OBJECT       *child_entry)
{
	ACPI_NAMED_OBJECT       *parent_entry;


	if (child_entry) {
		/* Valid entry.  Get the parent Nte */

		parent_entry = acpi_ns_get_parent_entry (child_entry);
		if (parent_entry) {
			if (parent_entry->name) {
				return (parent_entry->name);
			}
		}

	}


	return (ACPI_UNKNOWN_NAME);
}

/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_exist_downstream_sibling
 *
 * PARAMETERS:  *This_entry         - pointer to first nte to examine
 *
 * RETURN:      TRUE if sibling is found, FALSE otherwise
 *
 * DESCRIPTION: Searches remainder of scope being processed to determine
 *              whether there is a downstream sibling to the current
 *              object.  This function is used to determine what type of
 *              line drawing character to use when displaying namespace
 *              trees.
 *
 ***************************************************************************/

u8
acpi_ns_exist_downstream_sibling (
	ACPI_NAMED_OBJECT       *this_entry)
{

	if (!this_entry) {
		return FALSE;
	}

	if (this_entry->name) {
		return TRUE;
	}


/* TBD: what did this really do?
	if (This_entry->Next_entry) {
		return TRUE;
	}
*/
	return FALSE;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_owner_table
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ***************************************************************************/


ACPI_NAME_TABLE *
acpi_ns_get_owner_table (
	ACPI_NAMED_OBJECT       *this_entry)
{

	/*
	 * Given an entry in the Name_table->Entries field of a name table,
	 * we can create a pointer to the beginning of the table as follows:
	 *
	 * 1) Starting with the the pointer to the entry,
	 * 2) Subtract the entry index * size of each entry to get a
	 *    pointer to Entries[0]
	 * 3) Subtract the size of NAME_TABLE structure to get a pointer
	 *    to the start.
	 *
	 * This saves having to put a pointer in every entry that points
	 * back to the beginning of the table and/or a pointer back to
	 * the parent.
	 */

	return (ACPI_NAME_TABLE *) ((char *) this_entry -
			 (this_entry->this_index *
			 sizeof (ACPI_NAMED_OBJECT)) -
			 (sizeof (ACPI_NAME_TABLE) -
			 sizeof (ACPI_NAMED_OBJECT)));

}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_parent_entry
 *
 * PARAMETERS:  This_entry      - Current table entry
 *
 * RETURN:      Parent entry of the given entry
 *
 * DESCRIPTION: Obtain the parent entry for a given entry in the namespace.
 *
 ***************************************************************************/


ACPI_NAMED_OBJECT *
acpi_ns_get_parent_entry (
	ACPI_NAMED_OBJECT       *this_entry)
{
	ACPI_NAME_TABLE         *name_table;


	name_table = acpi_ns_get_owner_table (this_entry);

	/*
	 * Now that we have a pointer to the name table, we can just pluck
	 * the parent
	 */

	return (name_table->parent_entry);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_next_valid_entry
 *
 * PARAMETERS:  This_entry      - Current table entry
 *
 * RETURN:      Next valid object in the table.  NULL if no more valid
 *              objects
 *
 * DESCRIPTION: Find the next valid object within a name table.
 *
 ***************************************************************************/


ACPI_NAMED_OBJECT *
acpi_ns_get_next_valid_entry (
	ACPI_NAMED_OBJECT       *this_entry)
{
	ACPI_NAME_TABLE         *name_table;
	u32                     index;


	index = this_entry->this_index + 1;
	name_table = acpi_ns_get_owner_table (this_entry);


	while (name_table) {
		if (index >= NS_TABLE_SIZE) {
			/* We are at the end of this table */

			name_table = name_table->next_table;
			index = 0;
			continue;
		}


		/* Is this a valid (occupied) slot? */

		if (name_table->entries[index].name) {
			/* Found a valid entry, all done */

			return (&name_table->entries[index]);
		}

		/* Go to the next slot */

		index++;
	}

	/* No more valid entries in this name table */

	return NULL;
}


