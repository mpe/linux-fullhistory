
/******************************************************************************
 *
 * Module Name: ixface - External interpreter interfaces
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


#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amxface");


/*
 * DEFINE_AML_GLOBALS is tested in amlcode.h
 * to determine whether certain global names should be "defined" or only
 * "declared" in the current compilation.  This enhances maintainability
 * by enabling a single header file to embody all knowledge of the names
 * in question.
 *
 * Exactly one module of any executable should #define DEFINE_GLOBALS
 * before #including the header files which use this convention.  The
 * names in question will be defined and initialized in that module,
 * and declared as extern in all other modules which #include those
 * header files.
 */

#define DEFINE_AML_GLOBALS
#include "amlcode.h"
#include "parser.h"
#include "namesp.h"


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_execute_method
 *
 * PARAMETERS:  Pcode               - Pointer to the pcode stream
 *              Pcode_length        - Length of pcode that comprises the method
 *              **Params            - List of parameters to pass to method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_execute_method (
	ACPI_NAMED_OBJECT       *method_entry,
	ACPI_OBJECT_INTERNAL    **params,
	ACPI_OBJECT_INTERNAL    **return_obj_desc)
{
	ACPI_STATUS             status;


	/*
	 * The point here is to lock the interpreter and call the low
	 * level execute.
	 */

	acpi_aml_enter_interpreter ();

	status = acpi_psx_execute (method_entry, params, return_obj_desc);

	acpi_aml_exit_interpreter ();

	return (status);
}


