/******************************************************************************
 *
 * Module Name: evxface - External interfaces for ACPI events
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
#include "hardware.h"
#include "namesp.h"
#include "events.h"
#include "amlcode.h"
#include "interp.h"

#define _COMPONENT          EVENT_HANDLING
	 MODULE_NAME         ("evxface");


/******************************************************************************
 *
 * FUNCTION:    Acpi_install_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to enable.
 *              Handler         - Pointer to the handler function for the
 *                                event
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Saves the pointer to the handler function and then enables the
 *              event.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_install_fixed_event_handler (
	u32                     event,
	FIXED_EVENT_HANDLER     handler,
	void                    *context)
{
	ACPI_STATUS             status = AE_OK;


	/* Sanity check the parameters. */

	if (event >= NUM_FIXED_EVENTS) {
		return (AE_BAD_PARAMETER);
	}

	acpi_cm_acquire_mutex (ACPI_MTX_EVENTS);

	/* Don't allow two handlers. */

	if (NULL != acpi_gbl_fixed_event_handlers[event].handler) {
		status = AE_EXIST;
		goto cleanup;
	}


	/* Install the handler before enabling the event - just in case... */

	acpi_gbl_fixed_event_handlers[event].handler = handler;
	acpi_gbl_fixed_event_handlers[event].context = context;

	if (1 != acpi_hw_register_access (ACPI_WRITE,
			  ACPI_MTX_LOCK, event + TMR_EN, 1))
	{
		/* Remove the handler */

		acpi_gbl_fixed_event_handlers[event].handler = NULL;
		acpi_gbl_fixed_event_handlers[event].context = NULL;

		status = AE_ERROR;
		goto cleanup;
	}


cleanup:
	acpi_cm_release_mutex (ACPI_MTX_EVENTS);
	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_remove_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to disable.
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disables the event and unregisters the event handler.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_remove_fixed_event_handler (
	u32                     event,
	FIXED_EVENT_HANDLER     handler)
{
	ACPI_STATUS             status = AE_OK;


	/* Sanity check the parameters. */

	if (event >= NUM_FIXED_EVENTS) {
		return (AE_BAD_PARAMETER);
	}

	acpi_cm_acquire_mutex (ACPI_MTX_EVENTS);

	/* Disable the event before removing the handler - just in case... */

	if (0 != acpi_hw_register_access (ACPI_WRITE,
			  ACPI_MTX_LOCK, event + TMR_EN, 0))
	{
		status = AE_ERROR;
		goto cleanup;
	}

	/* Remove the handler */

	acpi_gbl_fixed_event_handlers[event].handler = NULL;
	acpi_gbl_fixed_event_handlers[event].context = NULL;

cleanup:
	acpi_cm_release_mutex (ACPI_MTX_EVENTS);
	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_install_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              Handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: Driver_handler (80-ff)
 *              Handler         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for notifies on an ACPI device
 *
 ******************************************************************************/

ACPI_STATUS
acpi_install_notify_handler (
	ACPI_HANDLE             device,
	u32                     handler_type,
	NOTIFY_HANDLER          handler,
	void                    *context)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_OBJECT_INTERNAL    *notify_obj;
	ACPI_NAMED_OBJECT       *obj_entry;
	ACPI_STATUS             status = AE_OK;


	/* Parameter validation */

	if ((!handler) ||
		(handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE))
	{
		return (AE_BAD_PARAMETER);
	}

	/* Convert and validate the device handle */

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	obj_entry = acpi_ns_convert_handle_to_entry (device);
	if (!obj_entry) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}


	/*
	 * Support for global notify handlers.  These handlers are invoked for
	 * every notifiy of the type specifiec
	 */

	if (device == ACPI_ROOT_OBJECT) {
		/*
		 *  Make sure the handler is not already installed.
		 */

		if (((handler_type == ACPI_SYSTEM_NOTIFY) &&
			  acpi_gbl_sys_notify.handler) ||
			((handler_type == ACPI_DEVICE_NOTIFY) &&
			  acpi_gbl_drv_notify.handler))
		{
			status = AE_EXIST;
			goto unlock_and_exit;
		}

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			acpi_gbl_sys_notify.nte = obj_entry;
			acpi_gbl_sys_notify.handler = handler;
			acpi_gbl_sys_notify.context = context;
		}

		else {
			acpi_gbl_drv_notify.nte = obj_entry;
			acpi_gbl_drv_notify.handler = handler;
			acpi_gbl_drv_notify.context = context;
		}


		/* Global notify handler installed */

		goto unlock_and_exit;
	}


	/*
	 * These are the ONLY objects that can receive ACPI notifications
	 */

	if ((obj_entry->type != ACPI_TYPE_DEVICE)    &&
		(obj_entry->type != ACPI_TYPE_PROCESSOR) &&
		(obj_entry->type != ACPI_TYPE_POWER)     &&
		(obj_entry->type != ACPI_TYPE_THERMAL))
	{
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Check for an existing internal object */

	obj_desc = acpi_ns_get_attached_object ((ACPI_HANDLE) obj_entry);
	if (obj_desc) {
		/*
		 *  The object exists.
		 *  Make sure the handler is not already installed.
		 */

		if (((handler_type == ACPI_SYSTEM_NOTIFY) &&
			  obj_desc->device.sys_handler) ||
			((handler_type == ACPI_DEVICE_NOTIFY) &&
			  obj_desc->device.drv_handler))
		{
			status = AE_EXIST;
			goto unlock_and_exit;
		}
	}

	else {
		/* Create a new object */

		obj_desc = acpi_cm_create_internal_object (obj_entry->type);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		/* Attach new object to the NTE */

		status = acpi_ns_attach_object (device, obj_desc, (u8) obj_entry->type);

		if (ACPI_FAILURE (status)) {
			goto unlock_and_exit;
		}
	}


	/*
	 *  If we get here, we know that there is no handler installed
	 *  so let's party
	 */
	notify_obj = acpi_cm_create_internal_object (INTERNAL_TYPE_NOTIFY);
	if (!notify_obj) {
		status = AE_NO_MEMORY;
		goto unlock_and_exit;
	}

	notify_obj->notify_handler.nte = obj_entry;
	notify_obj->notify_handler.handler = handler;
	notify_obj->notify_handler.context = context;


	if (handler_type == ACPI_SYSTEM_NOTIFY) {
		obj_desc->device.sys_handler = notify_obj;
	}

	else {
		obj_desc->device.drv_handler = notify_obj;
	}


unlock_and_exit:
	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_remove_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              Handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: Driver_handler (80-ff)
 *              Handler         - Address of the handler
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for notifies on an ACPI device
 *
 ******************************************************************************/

ACPI_STATUS
acpi_remove_notify_handler (
	ACPI_HANDLE             device,
	u32                     handler_type,
	NOTIFY_HANDLER          handler)
{
	ACPI_OBJECT_INTERNAL    *notify_obj;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_NAMED_OBJECT       *obj_entry;
	ACPI_STATUS             status = AE_OK;


	/* Parameter validation */

	if ((!handler) ||
		(handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE))
	{
		return (AE_BAD_PARAMETER);
	}

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	/* Convert and validate the device handle */

	obj_entry = acpi_ns_convert_handle_to_entry (device);
	if (!obj_entry) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * These are the ONLY objects that can receive ACPI notifications
	 */

	if ((obj_entry->type != ACPI_TYPE_DEVICE)    &&
		(obj_entry->type != ACPI_TYPE_PROCESSOR) &&
		(obj_entry->type != ACPI_TYPE_POWER)     &&
		(obj_entry->type != ACPI_TYPE_THERMAL))
	{
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Check for an existing internal object */

	obj_desc = acpi_ns_get_attached_object ((ACPI_HANDLE) obj_entry);
	if (!obj_desc) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	/*
	 *  The object exists.
	 *
	 *  Make sure the handler is installed.
	 */

	if (handler_type == ACPI_SYSTEM_NOTIFY) {
		notify_obj = obj_desc->device.sys_handler;
	}
	else {
		notify_obj = obj_desc->device.drv_handler;
	}

	if ((!notify_obj) ||
		(notify_obj->notify_handler.handler != handler))
	{
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * Now we can remove the handler
	 */
	if (handler_type == ACPI_SYSTEM_NOTIFY) {
		obj_desc->device.sys_handler = NULL;
	}
	else {
		obj_desc->device.drv_handler = NULL;
	}

	acpi_cm_remove_reference (notify_obj);

unlock_and_exit:
	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_install_gpe_handler
 *
 * PARAMETERS:  Gpe_number      - The GPE number.  The numbering scheme is
 *                                bank 0 first, then bank 1.
 *              Trigger         - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              Handler         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for a General Purpose Acpi_event.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_install_gpe_handler (
	u32                     gpe_number,
	u32                     type,
	GPE_HANDLER             handler,
	void                    *context)
{
	ACPI_STATUS             status = AE_OK;

	/* Parameter validation */

	if (!handler || (gpe_number > NUM_GPE)) {
		return (AE_BAD_PARAMETER);
	}

	/* Ensure that we have a valid GPE number */

	if (acpi_gbl_gpe_valid[gpe_number] == ACPI_GPE_INVALID) {
		return (AE_BAD_PARAMETER);
	}

	acpi_cm_acquire_mutex (ACPI_MTX_EVENTS);

	/* Make sure that there isn't a handler there already */

	if (acpi_gbl_gpe_info[gpe_number].handler) {
		status = AE_EXIST;
		goto cleanup;
	}

	/* Install the handler */

	acpi_gbl_gpe_info[gpe_number].handler = handler;
	acpi_gbl_gpe_info[gpe_number].context = context;
	acpi_gbl_gpe_info[gpe_number].type = (u8) type;

	/* Clear the GPE (of stale events), the enable it */

	acpi_hw_clear_gpe (gpe_number);
	acpi_hw_enable_gpe (gpe_number);

cleanup:
	acpi_cm_release_mutex (ACPI_MTX_EVENTS);
	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_remove_gpe_handler
 *
 * PARAMETERS:  Gpe_number      - The event to remove a handler
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for a General Purpose Acpi_event.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_remove_gpe_handler (
	u32                     gpe_number,
	GPE_HANDLER             handler)
{
	ACPI_STATUS             status = AE_OK;


	/* Parameter validation */

	if (!handler || (gpe_number > NUM_GPE)) {
		return (AE_BAD_PARAMETER);
	}

	/* Ensure that we have a valid GPE number */

	if (acpi_gbl_gpe_valid[gpe_number] == ACPI_GPE_INVALID) {
		return (AE_BAD_PARAMETER);
	}

	/* Disable the GPE before removing the handler */

	acpi_hw_disable_gpe (gpe_number);

	acpi_cm_acquire_mutex (ACPI_MTX_EVENTS);

	/* Make sure that the installed handler is the same */

	if (acpi_gbl_gpe_info[gpe_number].handler != handler) {
		acpi_hw_enable_gpe (gpe_number);
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Remove the handler */

	acpi_gbl_gpe_info[gpe_number].handler = NULL;
	acpi_gbl_gpe_info[gpe_number].context = NULL;

cleanup:
	acpi_cm_release_mutex (ACPI_MTX_EVENTS);
	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_acquire_global_lock
 *
 * PARAMETERS:  Timeout         - How long the caller is willing to wait
 *              Out_handle      - A handle to the lock if acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire the ACPI Global Lock
 *
 ******************************************************************************/

ACPI_STATUS
acpi_acquire_global_lock (
	u32                     timeout,
	u32                     *out_handle)
{
	ACPI_STATUS             status;


	acpi_aml_enter_interpreter ();

	/*
	 * TBD: [Restructure] add timeout param to internal interface, and
	 * perhaps INTERPRETER_LOCKED
	 */

	status = acpi_ev_acquire_global_lock ();
	acpi_aml_exit_interpreter ();

	*out_handle = 0;
	return status;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_release_global_lock
 *
 * PARAMETERS:  Handle      - Returned from Acpi_acquire_global_lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the ACPI Global Lock
 *
 ******************************************************************************/

ACPI_STATUS
acpi_release_global_lock (
	u32                     handle)
{


	/* TBD: [Restructure] Validate handle */

	acpi_ev_release_global_lock ();
	return AE_OK;
}


