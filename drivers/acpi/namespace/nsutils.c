/******************************************************************************
 *
 * Module Name: nsutils - Utilities for accessing ACPI namespace, accessing
 *                        parents and siblings and Scope manipulation
 *              $Revision: 69 $
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
#include "acnamesp.h"
#include "acinterp.h"
#include "amlcode.h"
#include "actables.h"

#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nsutils")


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
	NATIVE_CHAR             prefix)
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
	NATIVE_CHAR             sep)
{

	return ((u8) (sep == '.'));
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_type
 *
 * PARAMETERS:  Handle              - Parent Node to be examined
 *
 * RETURN:      Type field from Node whose handle is passed
 *
 ***************************************************************************/

OBJECT_TYPE_INTERNAL
acpi_ns_get_type (
	ACPI_HANDLE             handle)
{

	if (!handle) {
		REPORT_WARNING ("Ns_get_type: Null handle");
		return (ACPI_TYPE_ANY);
	}

	return (((ACPI_NAMESPACE_NODE *) handle)->type);
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

u32
acpi_ns_local (
	OBJECT_TYPE_INTERNAL    type)
{

	if (!acpi_cm_valid_object_type (type)) {
		/* Type code out of range  */

		REPORT_WARNING ("Ns_local: Invalid Object Type");
		return (NSP_NORMAL);
	}

	return ((u32) acpi_gbl_ns_properties[type] & NSP_LOCAL);
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
	NATIVE_CHAR             *external_name,
	NATIVE_CHAR             **converted_name)
{
	NATIVE_CHAR             *result = NULL;
	NATIVE_CHAR             *internal_name;
	u32                     num_segments;
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
				/* Convert s8 to uppercase and save it */

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
 * FUNCTION:    Acpi_ns_convert_handle_to_entry
 *
 * PARAMETERS:  Handle          - Handle to be converted to an Node
 *
 * RETURN:      A Name table entry pointer
 *
 * DESCRIPTION: Convert a namespace handle to a real Node
 *
 ****************************************************************************/

ACPI_NAMESPACE_NODE *
acpi_ns_convert_handle_to_entry (
	ACPI_HANDLE             handle)
{

	/*
	 * Simple implementation for now;
	 * TBD: [Future] Real integer handles allow for more verification
	 * and keep all pointers within this subsystem!
	 */

	if (!handle) {
		return (NULL);
	}

	if (handle == ACPI_ROOT_OBJECT) {
		return (acpi_gbl_root_node);
	}


	/* We can at least attempt to verify the handle */

	if (!VALID_DESCRIPTOR_TYPE (handle, ACPI_DESC_TYPE_NAMED)) {
		return (NULL);
	}

	return ((ACPI_NAMESPACE_NODE *) handle);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_convert_entry_to_handle
 *
 * PARAMETERS:  Node          - Node to be converted to a Handle
 *
 * RETURN:      An USER ACPI_HANDLE
 *
 * DESCRIPTION: Convert a real Node to a namespace handle
 *
 ****************************************************************************/

ACPI_HANDLE
acpi_ns_convert_entry_to_handle (
	ACPI_NAMESPACE_NODE         *node)
{


	/*
	 * Simple implementation for now;
	 * TBD: [Future] Real integer handles allow for more verification
	 * and keep all pointers within this subsystem!
	 */

	return ((ACPI_HANDLE) node);


/* ---------------------------------------------------

	if (!Node) {
		return (NULL);
	}

	if (Node == Acpi_gbl_Root_node) {
		return (ACPI_ROOT_OBJECT);
	}


	return ((ACPI_HANDLE) Node);
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
	ACPI_OPERAND_OBJECT     *obj_desc;
	ACPI_NAMESPACE_NODE     *this_node;


	this_node = acpi_gbl_root_node;

	/*
	 * 1) Free the entire namespace -- all objects, tables, and stacks
	 */
	/*
	 * Delete all objects linked to the root
	 * (additional table descriptors)
	 */

	acpi_ns_delete_namespace_subtree (this_node);

	/* Detach any object(s) attached to the root */

	obj_desc = acpi_ns_get_attached_object (this_node);
	if (obj_desc) {
		acpi_ns_detach_object (this_node);
		acpi_cm_remove_reference (obj_desc);
	}

	acpi_ns_delete_children (this_node);


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

u32
acpi_ns_opens_scope (
	OBJECT_TYPE_INTERNAL    type)
{

	if (!acpi_cm_valid_object_type (type)) {
		/* type code out of range  */

		REPORT_WARNING ("Ns_opens_scope: Invalid Object Type");
		return (NSP_NORMAL);
	}

	return (((u32) acpi_gbl_ns_properties[type]) & NSP_NEWSCOPE);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_node
 *
 * PARAMETERS:  *Pathname   - Name to be found, in external (ASL) format. The
 *                            \ (backslash) and ^ (carat) prefixes, and the
 *                            . (period) to separate segments are supported.
 *              Start_node  - Root of subtree to be searched, or NS_ALL for the
 *                            root of the name space.  If Name is fully
 *                            qualified (first s8 is '\'), the passed value
 *                            of Scope will not be accessed.
 *              Return_node - Where the Node is returned
 *
 * DESCRIPTION: Look up a name relative to a given scope and return the
 *              corresponding Node.  NOTE: Scope can be null.
 *
 * MUTEX:       Locks namespace
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_get_node (
	NATIVE_CHAR             *pathname,
	ACPI_NAMESPACE_NODE     *start_node,
	ACPI_NAMESPACE_NODE     **return_node)
{
	ACPI_GENERIC_STATE      scope_info;
	ACPI_STATUS             status;
	NATIVE_CHAR             *internal_path = NULL;


	scope_info.scope.node = start_node;

	/* Ensure that the namespace has been initialized */

	if (!acpi_gbl_root_node) {
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

	if (NS_ALL == scope_info.scope.node) {
		scope_info.scope.node = acpi_gbl_root_node;
	}

	else {
		scope_info.scope.node = start_node;
		if (!scope_info.scope.node) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup (&scope_info, internal_path,
			 ACPI_TYPE_ANY, IMODE_EXECUTE,
			 NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			 NULL, return_node);



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
 * PARAMETERS:  *Child_node            - Named Obj whose name is to be found
 *
 * RETURN:      The ACPI name
 *
 * DESCRIPTION: Search for the given obj in its parent scope and return the
 *              name segment, or "????" if the parent name can't be found
 *              (which "should not happen").
 *
 ***************************************************************************/

ACPI_NAME
acpi_ns_find_parent_name (
	ACPI_NAMESPACE_NODE     *child_node)
{
	ACPI_NAMESPACE_NODE     *parent_node;


	if (child_node) {
		/* Valid entry.  Get the parent Node */

		parent_node = acpi_ns_get_parent_object (child_node);
		if (parent_node) {
			if (parent_node->name) {
				return (parent_node->name);
			}
		}

	}


	return (ACPI_UNKNOWN_NAME);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_parent_object
 *
 * PARAMETERS:  Node       - Current table entry
 *
 * RETURN:      Parent entry of the given entry
 *
 * DESCRIPTION: Obtain the parent entry for a given entry in the namespace.
 *
 ***************************************************************************/


ACPI_NAMESPACE_NODE *
acpi_ns_get_parent_object (
	ACPI_NAMESPACE_NODE     *node)
{


	/*
	 * Walk to the end of this peer list.
	 * The last entry is marked with a flag and the peer
	 * pointer is really a pointer back to the parent.
	 * This saves putting a parent back pointer in each and
	 * every named object!
	 */

	while (!(node->flags & ANOBJ_END_OF_PEER_LIST)) {
		node = node->peer;
	}


	return (node->peer);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_next_valid_object
 *
 * PARAMETERS:  Node       - Current table entry
 *
 * RETURN:      Next valid object in the table.  NULL if no more valid
 *              objects
 *
 * DESCRIPTION: Find the next valid object within a name table.
 *              Useful for implementing NULL-end-of-list loops.
 *
 ***************************************************************************/


ACPI_NAMESPACE_NODE *
acpi_ns_get_next_valid_object (
	ACPI_NAMESPACE_NODE     *node)
{

	/* If we are at the end of this peer list, return NULL */

	if (node->flags & ANOBJ_END_OF_PEER_LIST) {
		return NULL;
	}

	/* Otherwise just return the next peer */

	return (node->peer);
}


