
/******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
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
	 MODULE_NAME         ("nsnames");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_name_of_scope
 *
 * PARAMETERS:  Scope           - Scope whose name is needed
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the scope, in Label format (all segments strung together
 *              with no separators)
 *
 * DESCRIPTION: Used via Acpi_ns_name_of_current_scope() and Acpi_ns_last_fQN()
 *              for label generation in the interpreter, and for debug
 *              printing in Acpi_ns_search_table().
 *
 ***************************************************************************/

char *
acpi_ns_name_of_scope (
	ACPI_NAME_TABLE         *scope)
{
	char                    *name_buffer;
	ACPI_SIZE               size;
	ACPI_NAME               name;
	ACPI_NAMED_OBJECT       *entry_to_search;
	ACPI_NAMED_OBJECT       *parent_entry;


	if (!acpi_gbl_root_object->child_table || !scope) {
		/*
		 * If the name space has not been initialized,
		 * this function should not have been called.
		 */
		return (NULL);
	}

	entry_to_search = scope->entries;


	/* Calculate required buffer size based on depth below root NT */

	size = 1;
	parent_entry = entry_to_search;
	while (parent_entry) {
		parent_entry = acpi_ns_get_parent_entry (parent_entry);
		if (parent_entry) {
			size += ACPI_NAME_SIZE;
		}
	}


	/* Allocate the buffer */

	name_buffer = acpi_cm_callocate (size + 1);
	if (!name_buffer) {
		REPORT_ERROR ("Ns_name_of_scope: allocation failure");
		return (NULL);
	}


	/* Store terminator byte, then build name backwards */

	name_buffer[size] = '\0';
	while ((size > ACPI_NAME_SIZE) &&
		acpi_ns_get_parent_entry (entry_to_search))
	{
		size -= ACPI_NAME_SIZE;
		name = acpi_ns_find_parent_name (entry_to_search);

		/* Put the name into the buffer */

		MOVE_UNALIGNED32_TO_32 ((name_buffer + size), &name);
		entry_to_search = acpi_ns_get_parent_entry (entry_to_search);
	}

	name_buffer[--size] = AML_ROOT_PREFIX;


	return (name_buffer);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_name_of_current_scope
 *
 * PARAMETERS:  none
 *
 * RETURN:      pointer to storage containing the name of the current scope
 *
 ***************************************************************************/

char *
acpi_ns_name_of_current_scope (
	ACPI_WALK_STATE         *walk_state)
{
	char                    *scope_name;


	if (walk_state && walk_state->scope_info) {
		scope_name =
			acpi_ns_name_of_scope (walk_state->scope_info->scope.name_table);

		return (scope_name);
	}

	REPORT_ERROR ("Current scope pointer is invalid");

	return (NULL);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_handle_to_pathname
 *
 * PARAMETERS:  Target_handle           - Handle of nte whose name is to be found
 *              Buf_size                - Size of the buffer provided
 *              User_buffer             - Where the pathname is returned
 *
 * RETURN:      Status, Buffer is filled with pathname if status == AE_OK
 *
 * DESCRIPTION: Build and return a full namespace pathname
 *
 * MUTEX:       Locks Namespace
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_handle_to_pathname (
	ACPI_HANDLE             target_handle,
	u32                     *buf_size,
	char                    *user_buffer)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_NAMED_OBJECT       *entry_to_search = NULL;
	ACPI_NAMED_OBJECT       *temp = NULL;
	ACPI_SIZE               path_length = 0;
	ACPI_SIZE               size;
	u32                     user_buf_size;
	ACPI_NAME               name;
	u8                      namespace_was_locked;


	if (!acpi_gbl_root_object->child_table || !target_handle) {
		/*
		 * If the name space has not been initialized,
		 * this function should not have been called.
		 */

		return (AE_NO_NAMESPACE);
	}

	namespace_was_locked = acpi_gbl_acpi_mutex_info[ACPI_MTX_NAMESPACE].locked;
	if (!namespace_was_locked) {
		acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);
	}

	entry_to_search = acpi_ns_convert_handle_to_entry (target_handle);
	if (!entry_to_search) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Compute length of pathname as 5 * number of name segments.
	 * Go back up the parent tree to the root
	 */
	for (size = 0, temp = entry_to_search;
		  acpi_ns_get_parent_entry (temp);
		  temp = acpi_ns_get_parent_entry (temp))
	{
		size += PATH_SEGMENT_LENGTH;
	}

	/* Set return length to the required path length */

	path_length = size + 1;
	user_buf_size = *buf_size;
	*buf_size = path_length;

	/* Check if the user buffer is sufficiently large */

	if (path_length > user_buf_size) {
		status = AE_BUFFER_OVERFLOW;
		goto unlock_and_exit;
	}

	/* Store null terminator */

	user_buffer[size] = 0;
	size -= ACPI_NAME_SIZE;

	/* Put the original ACPI name at the end of the path */

	MOVE_UNALIGNED32_TO_32 ((user_buffer + size),
			 &entry_to_search->name);

	user_buffer[--size] = PATH_SEPARATOR;

	/* Build name backwards, putting "." between segments */

	while ((size > ACPI_NAME_SIZE) && entry_to_search) {
		size -= ACPI_NAME_SIZE;
		name = acpi_ns_find_parent_name (entry_to_search);
		MOVE_UNALIGNED32_TO_32 ((user_buffer + size), &name);

		user_buffer[--size] = PATH_SEPARATOR;
		entry_to_search = acpi_ns_get_parent_entry (entry_to_search);
	}

	/*
	 * Overlay the "." preceding the first segment with
	 * the root name "\"
	 */

	user_buffer[size] = '\\';


unlock_and_exit:

	if (!namespace_was_locked) {
		acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	}

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_pattern_match
 *
 * PARAMETERS:  Obj_entry       - A namespace entry
 *              Search_for      - Wildcard pattern string
 *
 * DESCRIPTION: Matches a namespace name against a wildcard pattern.  Only
 *              a very simple pattern - 4 chars, either a valid char or a "?"
 *              to match any.
 *
 ***************************************************************************/

u8
acpi_ns_pattern_match (
	ACPI_NAMED_OBJECT       *obj_entry,
	char                    *search_for)
{
	s32                     i;


	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (search_for[i] != '?' &&
			search_for[i] != ((char *) &obj_entry->name)[i])
		{
			/* No match */

			return FALSE;
		}
	}

	/* name matches pattern */

	return TRUE;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_name_compare
 *
 * PARAMETERS:  Obj_handle      - A namespace entry
 *              Level           - Current nesting level
 *              Context         - A FIND_CONTEXT structure
 *
 * DESCRIPTION: A User_function called by Acpi_ns_walk_namespace(). It performs
 *              a pattern match for Acpi_ns_low_find_names(), and updates the list
 *              and count as required.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_name_compare (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	FIND_CONTEXT            *find = context;


	/* Match, yes or no? */

	if (acpi_ns_pattern_match ((ACPI_NAMED_OBJECT*) obj_handle,
			 find->search_for))
	{
		/* Name matches pattern */

		if (find->list) {
			find->list[*(find->count)] = obj_handle;
		}

		++*(find->count);
	}

	 /* Don't terminate the walk */
	return AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_low_find_names
 *
 * PARAMETERS:  *This_entry         - Table to be searched
 *              *Search_for         - Pattern to be found.
 *                                    4 bytes, ? matches any character.
 *              *Count              - Output count of matches found.
 *                                    Outermost caller should preset to 0
 *              List[]              - Output array of handles.  If
 *                                    null, only the count is obtained.
 *              Max_depth           - Maximum depth of search.  Use
 *                                    INT_MAX for an effectively
 *                                    unlimited depth.
 *
 * DESCRIPTION: Low-level find name.
 *              Traverse the name space finding names which match a search
 *              pattern, and return an array of handles in List[].
 *
 ***************************************************************************/

void
acpi_ns_low_find_names (
	ACPI_NAMED_OBJECT       *this_entry,
	char                    *search_for,
	s32                     *count,
	ACPI_HANDLE             list[],
	s32                     max_depth)
{
	FIND_CONTEXT            find;


	if (0 == max_depth || !this_entry || !search_for || !count) {
		/*
		 * Zero requested depth, nothing to search,
		 * nothing to search for, or count pointer bad
		 */

		return;
	}

	/* Init the context structure used by compare routine */

	find.list       = list;
	find.count      = count;
	find.search_for = search_for;

	/* Walk the namespace and find all matches */

	acpi_ns_walk_namespace (ACPI_TYPE_ANY, (ACPI_HANDLE) this_entry,
			   max_depth, NS_WALK_NO_UNLOCK,
			   acpi_ns_name_compare, &find, NULL);

	if (list) {
		/* null-terminate the output array */

		list[*count] = (ACPI_HANDLE) 0;
	}

	return;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_find_names

 *
 * PARAMETERS:  *Search_for         - pattern to be found.
 *                                    4 bytes, ? matches any character.
 *                                    If NULL, "????" will be used.
 *              Start_handle        - Root of subtree to be searched, or
 *                                    NS_ALL to search the entire namespace
 *              Max_depth           - Maximum depth of search.  Use INT_MAX
 *                                    for an effectively unlimited depth.
 *
 * DESCRIPTION: Traverse the name space finding names which match a search
 *              pattern, and return an array of handles.  The end of the
 *              array is marked by the value (ACPI_HANDLE)0.  A return value
 *              of (ACPI_HANDLE *)0 indicates that no matching names were
 *              found or that space for the list could not be allocated.
 *              if Start_handle is NS_ALL (null) search from the root,
 *              else it is a handle whose children are to be searched.
 *
 ***************************************************************************/

ACPI_HANDLE *
acpi_ns_find_names (
	char                    *search_for,
	ACPI_HANDLE             start_handle,
	s32                     max_depth)
{
	ACPI_HANDLE             *list = NULL;
	s32                     count;


	if (!acpi_gbl_root_object->child_table) {
		/*
		 * If the name space has not been initialized,
		 * there surely are no matching names.
		 */
		return (NULL);
	}

	if (NS_ALL == start_handle) {
		/* base is root */

		start_handle = acpi_gbl_root_object;
	}

	else if (((ACPI_NAMED_OBJECT *) start_handle)->child_table) {
		/* base has children to search */

		start_handle =
			((ACPI_NAMED_OBJECT *) start_handle)->child_table->entries;
	}

	else {
		/*
		 * If base is not the root and has no children,
		 * there is nothing to search.
		 */
		return (NULL);
	}

	if (!search_for) {
		/* Search name not specified */

		search_for = "????";
	}


	/* Pass 1.  Get required buffer size, don't try to build list */

	count = 0;
	acpi_ns_low_find_names (start_handle, search_for, &count,
			   NULL, max_depth);

	if (0 == count) {
		return (NULL);
	}

	/* Allow for trailing null */
	count++;

	list = acpi_cm_callocate (count * sizeof(ACPI_HANDLE));
	if (!list) {
		REPORT_ERROR ("Ns_find_names: allocation failure");
		return (NULL);
	}

	/* Pass 2.  Fill buffer */

	count = 0;
	acpi_ns_low_find_names (start_handle, search_for, &count, list, max_depth);

	return (list);
}


