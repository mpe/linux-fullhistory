/*******************************************************************************
 *
 * Module Name: nsobject - Utilities for objects attached to namespace
 *                         table entries
 *              $Revision: 80 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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
#include "acnamesp.h"
#include "acinterp.h"
#include "actables.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsobject")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_attach_object
 *
 * PARAMETERS:  Node                - Parent Node
 *              Object              - Object to be attached
 *              Type                - Type of object, or ACPI_TYPE_ANY if not
 *                                    known
 *
 * DESCRIPTION: Record the given object as the value associated with the
 *              name whose acpi_handle is passed.  If Object is NULL
 *              and Type is ACPI_TYPE_ANY, set the name as having no value.
 *              Note: Future may require that the Node->Flags field be passed
 *              as a parameter.
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

acpi_status
acpi_ns_attach_object (
	acpi_namespace_node     *node,
	acpi_operand_object     *object,
	acpi_object_type        type)
{
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *last_obj_desc;
	acpi_object_type        object_type = ACPI_TYPE_ANY;


	ACPI_FUNCTION_TRACE ("Ns_attach_object");


	/*
	 * Parameter validation
	 */
	if (!node) {
		/* Invalid handle */

		ACPI_REPORT_ERROR (("Ns_attach_object: Null Named_obj handle\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (!object && (ACPI_TYPE_ANY != type)) {
		/* Null object */

		ACPI_REPORT_ERROR (("Ns_attach_object: Null object, but type not ACPI_TYPE_ANY\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (node) != ACPI_DESC_TYPE_NAMED) {
		/* Not a name handle */

		ACPI_REPORT_ERROR (("Ns_attach_object: Invalid handle\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Check if this object is already attached */

	if (node->object == object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj %p already installed in Name_obj %p\n",
			object, node));

		return_ACPI_STATUS (AE_OK);
	}

	/* If null object, we will just install it */

	if (!object) {
		obj_desc   = NULL;
		object_type = ACPI_TYPE_ANY;
	}

	/*
	 * If the source object is a namespace Node with an attached object,
	 * we will use that (attached) object
	 */
	else if ((ACPI_GET_DESCRIPTOR_TYPE (object) == ACPI_DESC_TYPE_NAMED) &&
			((acpi_namespace_node *) object)->object) {
		/*
		 * Value passed is a name handle and that name has a
		 * non-null value.  Use that name's value and type.
		 */
		obj_desc   = ((acpi_namespace_node *) object)->object;
		object_type = ((acpi_namespace_node *) object)->type;
	}

	/*
	 * Otherwise, we will use the parameter object, but we must type
	 * it first
	 */
	else {
		obj_desc = (acpi_operand_object *) object;

		/* If a valid type (non-ANY) was given, just use it */

		if (ACPI_TYPE_ANY != type) {
			object_type = type;
		}
		else {
			object_type = INTERNAL_TYPE_DEF_ANY;
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Installing %p into Node %p [%4.4s]\n",
		obj_desc, node, (char *) &node->name));

	/*
	 * Must increment the new value's reference count
	 * (if it is an internal object)
	 */
	acpi_ut_add_reference (obj_desc);

	/* Detach an existing attached object if present */

	if (node->object) {
		acpi_ns_detach_object (node);
	}


	/*
	 * Handle objects with multiple descriptors - walk
	 * to the end of the descriptor list
	 */
	last_obj_desc = obj_desc;
	while (last_obj_desc->common.next_object) {
		last_obj_desc = last_obj_desc->common.next_object;
	}

	/* Install the object at the front of the object list */

	last_obj_desc->common.next_object = node->object;

	node->type     = (u8) object_type;
	node->object   = obj_desc;

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_detach_object
 *
 * PARAMETERS:  Node           - An object whose Value will be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete the Value associated with a namespace object.  If the
 *              Value is an allocated object, it is freed.  Otherwise, the
 *              field is simply cleared.
 *
 ******************************************************************************/

void
acpi_ns_detach_object (
	acpi_namespace_node     *node)
{
	acpi_operand_object     *obj_desc;


	ACPI_FUNCTION_TRACE ("Ns_detach_object");


	obj_desc = node->object;
	if (!obj_desc   ||
		(obj_desc->common.type == INTERNAL_TYPE_DATA)) {
		return_VOID;
	}

	/* Clear the entry in all cases */

	node->object = NULL;
	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) == ACPI_DESC_TYPE_INTERNAL) {
		node->object = obj_desc->common.next_object;
		if (node->object &&
		   (node->object->common.type != INTERNAL_TYPE_DATA)) {
			node->object = node->object->common.next_object;
		}
	}

	/* Reset the node type to untyped */

	node->type = ACPI_TYPE_ANY;

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Node %p [%4.4s] Object %p\n",
		node, (char *) &node->name, obj_desc));

	/* Remove one reference on the object (and all subobjects) */

	acpi_ut_remove_reference (obj_desc);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_attached_object
 *
 * PARAMETERS:  Node             - Parent Node to be examined
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed
 *
 ******************************************************************************/

acpi_operand_object *
acpi_ns_get_attached_object (
	acpi_namespace_node     *node)
{
	ACPI_FUNCTION_TRACE_PTR ("Ns_get_attached_object", node);


	if (!node) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Null Node ptr\n"));
		return_PTR (NULL);
	}

	if (!node->object ||
			((ACPI_GET_DESCRIPTOR_TYPE (node->object) != ACPI_DESC_TYPE_INTERNAL)  &&
			 (ACPI_GET_DESCRIPTOR_TYPE (node->object) != ACPI_DESC_TYPE_NAMED))    ||
		(node->object->common.type == INTERNAL_TYPE_DATA)) {
		return_PTR (NULL);
	}

	return_PTR (node->object);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_secondary_object
 *
 * PARAMETERS:  Node             - Parent Node to be examined
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed
 *
 ******************************************************************************/

acpi_operand_object *
acpi_ns_get_secondary_object (
	acpi_operand_object     *obj_desc)
{
	ACPI_FUNCTION_TRACE_PTR ("Ns_get_secondary_object", obj_desc);


	if ((!obj_desc)                                  ||
		(obj_desc->common.type == INTERNAL_TYPE_DATA) ||
		(!obj_desc->common.next_object)              ||
		(obj_desc->common.next_object->common.type == INTERNAL_TYPE_DATA)) {
		return_PTR (NULL);
	}

	return_PTR (obj_desc->common.next_object);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_attach_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ns_attach_data (
	acpi_namespace_node     *node,
	ACPI_OBJECT_HANDLER     handler,
	void                    *data)
{
	acpi_operand_object     *prev_obj_desc;
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *data_desc;


	/* */
	prev_obj_desc = NULL;
	obj_desc = node->object;
	while (obj_desc) {
		if ((obj_desc->common.type == INTERNAL_TYPE_DATA) &&
			(obj_desc->data.handler == handler)) {
			return (AE_ALREADY_EXISTS);
		}

		prev_obj_desc = obj_desc;
		obj_desc = obj_desc->common.next_object;
	}


	/* Create an internal object for the data */

	data_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_DATA);
	if (!data_desc) {
		return (AE_NO_MEMORY);
	}

	data_desc->data.handler = handler;
	data_desc->data.pointer = data;


	/* Install the data object */

	if (prev_obj_desc) {
		prev_obj_desc->common.next_object = data_desc;
	}
	else {
		node->object = data_desc;
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_detach_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ns_detach_data (
	acpi_namespace_node     *node,
	ACPI_OBJECT_HANDLER     handler)
{
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *prev_obj_desc;


	prev_obj_desc = NULL;
	obj_desc = node->object;
	while (obj_desc) {
		if ((obj_desc->common.type == INTERNAL_TYPE_DATA) &&
			(obj_desc->data.handler == handler)) {
			if (prev_obj_desc) {
				prev_obj_desc->common.next_object = obj_desc->common.next_object;
			}
			else {
				node->object = obj_desc->common.next_object;
			}

			acpi_ut_remove_reference (obj_desc);
			return (AE_OK);
		}

		prev_obj_desc = obj_desc;
		obj_desc = obj_desc->common.next_object;
	}

	return (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_attached_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ns_get_attached_data (
	acpi_namespace_node     *node,
	ACPI_OBJECT_HANDLER     handler,
	void                    **data)
{
	acpi_operand_object     *obj_desc;


	obj_desc = node->object;
	while (obj_desc) {
		if ((obj_desc->common.type == INTERNAL_TYPE_DATA) &&
			(obj_desc->data.handler == handler)) {
			*data = obj_desc->data.pointer;
			return (AE_OK);
		}

		obj_desc = obj_desc->common.next_object;
	}

	return (AE_NOT_FOUND);
}


