
/******************************************************************************
 *
 * Module Name: nswalk - Functions for walking the APCI namespace
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
#include "interp.h"
#include "namesp.h"


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nswalk");


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_next_object
 *
 * PARAMETERS:  Type            - Type of object to be searched for
 *              Parent          - Parent object whose children we are getting
 *              Last_child      - Previous child that was found.
 *                                The NEXT child will be returned
 *              Ret_handle      - Where handle to the next object is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the next peer object within the namespace.  If Handle is
 *              valid, Scope is ignored.  Otherwise, the first object within
 *              Scope is returned.
 *
 ******************************************************************************/

ACPI_NAMED_OBJECT*
acpi_ns_get_next_object (
	OBJECT_TYPE_INTERNAL    type,
	ACPI_NAMED_OBJECT       *parent,
	ACPI_NAMED_OBJECT       *child)
{
	ACPI_NAMED_OBJECT       *this_entry = NULL;


	if (!child) {

		/* It's really the parent's _scope_ that we want */

		if (parent->child_table) {
			this_entry = parent->child_table->entries;
		}
	}

	else {
		/* Start search at the NEXT object */

		this_entry = acpi_ns_get_next_valid_entry (child);
	}


	/* If any type is OK, we are done */

	if (type == ACPI_TYPE_ANY) {
		/* Make sure this is valid entry first */

		if ((!this_entry) ||
			(!this_entry->name))
		{
			return NULL;
		}

		return (this_entry);
	}


	/* Must search for the object -- but within this scope only */

	while (this_entry) {
		/* If type matches, we are done */

		if (this_entry->type == type) {
			return (this_entry);
		}

		/* Otherwise, move on to the next object */

		this_entry = acpi_ns_get_next_valid_entry (this_entry);
	}


	/* Not found */

	return NULL;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ns_walk_namespace
 *
 * PARAMETERS:  Type                - ACPI_OBJECT_TYPE to search for
 *              Start_object        - Handle in namespace where search begins
 *              Max_depth           - Depth to which search is to reach
 *              Unlock_before_callback- Whether to unlock the NS before invoking
 *                                    the callback routine
 *              User_function       - Called when an object of "Type" is found
 *              Context             - Passed to user function
 *
 * RETURNS      Return value from the User_function if terminated early.
 *              Otherwise, returns NULL.
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the object specified by Start_handle.
 *              The User_function is called whenever an object that matches
 *              the type parameter is found.  If the user function returns
 *              a non-zero value, the search is terminated immediately and this
 *              value is returned to the caller.
 *
 *              The point of this procedure is to provide a generic namespace
 *              walk routine that can be called from multiple places to
 *              provide multiple services;  the User Function can be tailored
 *              to each task, whether it is a print function, a compare
 *              function, etc.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ns_walk_namespace (
	OBJECT_TYPE_INTERNAL    type,
	ACPI_HANDLE             start_object,
	u32                     max_depth,
	u8                      unlock_before_callback,
	WALK_CALLBACK           user_function,
	void                    *context,
	void                    **return_value)
{
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT       *child_entry;
	ACPI_NAMED_OBJECT       *parent_entry;
	OBJECT_TYPE_INTERNAL    child_type;
	u32                     level;


	/* Special case for the namespace root object */

	if (start_object == ACPI_ROOT_OBJECT) {
		start_object = acpi_gbl_root_object;
	}


	/* Null child means "get first object" */

	parent_entry   = start_object;
	child_entry    = 0;
	child_type      = ACPI_TYPE_ANY;
	level           = 1;

	/*
	 * Traverse the tree of objects until we bubble back up to where we
	 * started. When Level is zero, the loop is done because we have
	 * bubbled up to (and passed) the original parent handle (Start_entry)
	 */

	while (level > 0) {
		/*
		 * Get the next typed object in this scope.  Null returned
		 * if not found
		 */

		status = AE_OK;
		child_entry = acpi_ns_get_next_object (ACPI_TYPE_ANY,
				 parent_entry,
				 child_entry);

		if (child_entry) {
			/*
			 * Found an object, Get the type if we are not
			 * searching for ANY
			 */

			if (type != ACPI_TYPE_ANY) {
				child_type = child_entry->type;
			}

			if (child_type == type) {
				/*
				 * Found a matching object, invoke the user
				 * callback function
				 */

				if (unlock_before_callback) {
					acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
				}

				status = user_function (child_entry, level,
						 context, return_value);

				if (unlock_before_callback) {
					acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);
				}

				switch (status)
				{
				case AE_OK:
				case AE_CTRL_DEPTH:
					/* Just keep going */
					break;

				case AE_CTRL_TERMINATE:
					/* Exit now, with OK status */
					return (AE_OK);
					break;

				default:
					/* All others are valid exceptions */
					return (status);
					break;
				}
			}

			/*
			 * Depth first search:
			 * Attempt to go down another level in the namespace
			 * if we are allowed to.  Don't go any further if we
			 * have reached the caller specified maximum depth
			 * or if the user function has specified that the
			 * maximum depth has been reached.
			 */

			if ((level < max_depth) && (status != AE_CTRL_DEPTH)) {
				if (acpi_ns_get_next_object (ACPI_TYPE_ANY,
						 child_entry, 0))
				{
					/*
					 * There is at least one child of this
					 * object, visit the object
					 */
					level++;
					parent_entry   = child_entry;
					child_entry    = 0;
				}
			}
		}

		else {
			/*
			 * No more children in this object (Acpi_ns_get_next_object
			 * failed), go back upwards in the namespace tree to
			 * the object's parent.
			 */
			level--;
			child_entry = parent_entry;
			parent_entry = acpi_ns_get_parent_entry (parent_entry);
		}
	}

	/* Complete walk, not terminated by user function */
	return (AE_OK);
}


