
/******************************************************************************
 *
 * Module Name: nsalloc - Namespace allocation and deletion utilities
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


#define _COMPONENT          NAMESPACE
	 MODULE_NAME         ("nsalloc");


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_allocate_name_table
 *
 * PARAMETERS:  Nte_count           - Count of NTEs to allocate
 *
 * RETURN:      The address of the first nte in the array, or NULL
 *
 * DESCRIPTION: Allocate an array of nte, including prepended link space
 *              Array is set to all zeros via Acpi_os_callcate().
 *
 ***************************************************************************/

ACPI_NAME_TABLE *
acpi_ns_allocate_name_table (
	u32                     num_entries)
{
	ACPI_NAME_TABLE         *name_table = NULL;
	ACPI_SIZE               alloc_size;


	alloc_size = sizeof (ACPI_NAME_TABLE) + ((num_entries - 1) *
			 sizeof (ACPI_NAMED_OBJECT));

	name_table = acpi_cm_callocate (alloc_size);


	return (name_table);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_namespace_subtree
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a subtree of the namespace.  This includes all objects stored
 *              within the subtree.  Scope tables are deleted also
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_delete_namespace_subtree (
	ACPI_NAMED_OBJECT       *parent_entry)
{
	ACPI_NAMED_OBJECT       *child_entry;
	u32                     level;
	ACPI_OBJECT_INTERNAL    *obj_desc;


	child_entry    = 0;
	level           = 1;

	/*
	 * Traverse the tree of objects until we bubble back up
	 * to where we started.
	 */

	while (level > 0) {
		/*
		 * Get the next typed object in this scope.
		 * Null returned if not found
		 */

		child_entry = acpi_ns_get_next_object (ACPI_TYPE_ANY,
				 parent_entry,
				 child_entry);

		if (child_entry) {
			/*
			 * Found an object - delete the object within
			 * the Value field
			 */

			obj_desc = acpi_ns_get_attached_object (child_entry);
			if (obj_desc) {
				acpi_ns_detach_object (child_entry);
				acpi_cm_remove_reference (obj_desc);
			}


			/*
			 * Clear the NTE in case this scope is reused
			 * (e.g., a control method scope)
			 */

			child_entry->type = ACPI_TYPE_ANY;
			child_entry->name = 0;

			/* Check if this object has any children */

			if (acpi_ns_get_next_object (ACPI_TYPE_ANY, child_entry, 0)) {
				/*
				 * There is at least one child of this object,
				 * visit the object
				 */

				level++;
				parent_entry   = child_entry;
				child_entry    = 0;
			}

			else {
				/*
				 * There may be a name table even if there are
				 * no children
				 */

				acpi_ns_delete_name_table (child_entry->child_table);
				child_entry->child_table = NULL;

			}
		}

		else {
			/*
			 * No more children in this object.
			 * We will move up to the grandparent.
			 */
			level--;

			/*
			 * Delete the scope (Name Table) associated with
			 * the parent object
			 */
			/* Don't delete the top level scope, this allows
			 * the dynamic deletion of objects created underneath
			 * control methods!
			 */

			if (level != 0) {
				acpi_ns_delete_name_table (parent_entry->child_table);
				parent_entry->child_table = NULL;
			}

			/* New "last child" is this parent object */

			child_entry = parent_entry;

			/* Now we can move up the tree to the grandparent */

			parent_entry = acpi_ns_get_parent_entry (parent_entry);
		}
	}


	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_remove_reference
 *
 * PARAMETERS:  Entry           - NTE whose reference count is to be decremented
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Remove an NTE reference.  Decrements the reference count of
 *              all parent NTEs up to the root.  Any NTE along the way that
 *              reaches zero references is freed.
 *
 ***************************************************************************/

void
acpi_ns_remove_reference (
	ACPI_NAMED_OBJECT       *entry)
{
	ACPI_NAMED_OBJECT       *this_entry;


	/* There may be a name table even if there are no children */

	acpi_ns_delete_name_table (entry->child_table);
	entry->child_table = NULL;


	/*
	 * Decrement the reference count(s) of all parents up to the root,
	 * And delete anything with zero remaining references.
	 */
	this_entry = entry;
	while (this_entry) {
		/* Decrement the reference */

		this_entry->reference_count--;

		/* Delete entry if no more references */

		if (!this_entry->reference_count) {
			/* Delete the scope if present */

			if (this_entry->child_table) {
				acpi_ns_delete_name_table (this_entry->child_table);
				this_entry->child_table = NULL;
			}

			/*
			 * Mark the entry free
			 * (This doesn't deallocate anything)
			 */

			acpi_ns_free_table_entry (this_entry);

		}

		/* Move up to parent */

		this_entry = acpi_ns_get_parent_entry (this_entry);
	}
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_namespace_by_owner
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete entries within the namespace that are owned by a
 *              specific ID.  Used to delete entire ACPI tables.  All
 *              reference counts are updated.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_ns_delete_namespace_by_owner (
	u16                     owner_id)
{
	ACPI_NAMED_OBJECT       *child_entry;
	u32                     level;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_NAMED_OBJECT       *parent_entry;


	parent_entry   = acpi_gbl_root_object;
	child_entry    = 0;
	level           = 1;

	/*
	 * Traverse the tree of objects until we bubble back up
	 * to where we started.
	 */

	while (level > 0) {
		/*
		 * Get the next typed object in this scope.
		 * Null returned if not found
		 */

		child_entry = acpi_ns_get_next_object (ACPI_TYPE_ANY,
				 parent_entry,
				 child_entry);

		if (child_entry) {
			if (child_entry->owner_id == owner_id) {
				/*
				 * Found an object - delete the object within
				 * the Value field
				 */

				obj_desc = acpi_ns_get_attached_object (child_entry);
				if (obj_desc) {
					acpi_ns_detach_object (child_entry);
					acpi_cm_remove_reference (obj_desc);
				}
			}

			/* Check if this object has any children */

			if (acpi_ns_get_next_object (ACPI_TYPE_ANY, child_entry, 0)) {
				/*
				 * There is at least one child of this object,
				 * visit the object
				 */

				level++;
				parent_entry   = child_entry;
				child_entry    = 0;
			}

			else if (child_entry->owner_id == owner_id) {
				acpi_ns_remove_reference (child_entry);
			}
		}

		else {
			/*
			 * No more children in this object.
			 * We will move up to the grandparent.
			 */
			level--;

			/*
			 * Delete the scope (Name Table) associated with
			 * the parent object
			 */
			/* Don't delete the top level scope, this allows
			 * the dynamic deletion of objects created underneath
			 * control methods!
			 */


			if (level != 0) {
				if (parent_entry->owner_id == owner_id) {
					acpi_ns_remove_reference (parent_entry);
				}
			}


			/* New "last child" is this parent object */

			child_entry = parent_entry;

			/* Now we can move up the tree to the grandparent */

			parent_entry = acpi_ns_get_parent_entry (parent_entry);
		}
	}


	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_name_table
 *
 * PARAMETERS:  Scope           - A handle to the scope to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a namespace Name Table with zero or
 *              more appendages.  The table and all appendages are deleted.
 *
 ***************************************************************************/

void
acpi_ns_delete_name_table (
	ACPI_NAME_TABLE         *name_table)
{
	ACPI_NAME_TABLE         *this_table;
	ACPI_NAME_TABLE         *next_table;


	if (!name_table) {
		return;
	}

	this_table = name_table;


	/*
	 * Deallocate the name table and all appendages
	 */
	do
	{
		next_table = this_table->next_table;

		/* Now we can free the table */

		acpi_cm_free (this_table);
		this_table = next_table;

	} while (this_table);

	return;
}


