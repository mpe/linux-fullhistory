/******************************************************************************
 *
 * Module Name: cmxface - External interfaces for "global" ACPI functions
 *              $Revision: 43 $
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
#include "acevents.h"
#include "achware.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acdebug.h"


#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cmxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes all global variables.  This is the first function
 *              called, so any early initialization belongs here.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_initialize (ACPI_INIT_DATA *init_data)
{
	ACPI_STATUS             status;


	/* Initialize all globals used by the subsystem */

	acpi_cm_init_globals (init_data);

	/* Initialize the OS-Dependent layer */

	status = acpi_os_initialize ();
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR ("OSD Initialization Failure");
		return (status);
	}

	/* Create the default mutex objects */

	status = acpi_cm_mutex_initialize ();
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR ("Global Mutex Initialization Failure");
		return (status);
	}

	/* If configured, initialize the AML debugger */

	DEBUGGER_EXEC (acpi_db_initialize ());

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Shutdown the ACPI subsystem.  Release all resources.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_terminate (void)
{

	/* Terminate the AML Debuger if present */

	acpi_gbl_db_terminate_threads = TRUE;

	/* TBD: [Investigate] This is no longer needed?*/
/*    Acpi_cm_release_mutex (ACPI_MTX_DEBUG_CMD_READY); */


	/* Shutdown and free all resources */

	acpi_cm_subsystem_shutdown ();


	/* Free the mutex objects */

	acpi_cm_mutex_terminate ();


	/* Now we can shutdown the OS-dependent layer */

	acpi_os_terminate ();

	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_system_info
 *
 * PARAMETERS:  Out_buffer      - a pointer to a buffer to receive the
 *                                resources for the device
 *              Buffer_length   - the number of bytes available in the buffer
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to get information about the current
 *              state of the ACPI subsystem.  It will return system information
 *              in the Out_buffer.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of Out_buffer is undefined.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_get_system_info (
	ACPI_BUFFER             *out_buffer)
{
	ACPI_SYSTEM_INFO        *info_ptr;
	u32                     i;


	/*
	 *  Must have a valid buffer
	 */
	if ((!out_buffer)         ||
		(!out_buffer->pointer))
	{
		return (AE_BAD_PARAMETER);
	}

	if (out_buffer->length < sizeof (ACPI_SYSTEM_INFO)) {
		/*
		 *  Caller's buffer is too small
		 */
		out_buffer->length = sizeof (ACPI_SYSTEM_INFO);

		return (AE_BUFFER_OVERFLOW);
	}


	/*
	 *  Set return length and get data
	 */
	out_buffer->length = sizeof (ACPI_SYSTEM_INFO);
	info_ptr = (ACPI_SYSTEM_INFO *) out_buffer->pointer;

	/* TBD [Future]: need a version number, or use the version string */
	info_ptr->acpi_ca_version   = 0x1234;

	/* System flags (ACPI capabilities) */

	info_ptr->flags             = acpi_gbl_system_flags;

	/* Timer resolution - 24 or 32 bits  */

	info_ptr->timer_resolution  = acpi_hw_pmt_resolution ();

	/* Clear the reserved fields */

	info_ptr->reserved1         = 0;
	info_ptr->reserved2         = 0;

	/* Current debug levels */

	info_ptr->debug_layer       = acpi_dbg_layer;
	info_ptr->debug_level       = acpi_dbg_level;

	/* Current status of the ACPI tables, per table type */

	info_ptr->num_table_types = NUM_ACPI_TABLES;
	for (i = 0; i < NUM_ACPI_TABLES; i++); {
		info_ptr->table_info[i].count = acpi_gbl_acpi_tables[i].count;
	}

	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_format_exception
 *
 * PARAMETERS:  Out_buffer      - a pointer to a buffer to receive the
 *                                exception name
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function translates an ACPI exception into an ASCII string.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_format_exception (
	ACPI_STATUS             exception,
	ACPI_BUFFER             *out_buffer)
{
	u32                     length;
	NATIVE_CHAR             *formatted_exception;


	/*
	 *  Must have a valid buffer
	 */
	if ((!out_buffer)         ||
		(!out_buffer->pointer))
	{
		return (AE_BAD_PARAMETER);
	}


	/* Convert the exception code (Handles bad exception codes) */

	formatted_exception = acpi_cm_format_exception (exception);

	/*
	 * Get length of string and check if it will fit in caller's buffer
	 */

	length = STRLEN (formatted_exception);
	if (out_buffer->length < length) {
		out_buffer->length = length;
		return (AE_BUFFER_OVERFLOW);
	}


	/* Copy the string, all done */

	STRCPY (out_buffer->pointer, formatted_exception);

	return (AE_OK);
}

