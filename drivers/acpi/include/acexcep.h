
/******************************************************************************
 *
 * Name: acexcep.h - Exception codes returned by the ACPI subsystem
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

#ifndef __ACEXCEP_H__
#define __ACEXCEP_H__


/*
 * Exceptions returned by external ACPI interfaces
 */

#define ACPI_SUCCESS(a)                 (!(a))
#define ACPI_FAILURE(a)                 (a)

#define AE_OK                           (ACPI_STATUS) 0x0000
#define AE_CTRL_RETURN_VALUE            (ACPI_STATUS) 0x0001
#define AE_CTRL_PENDING                 (ACPI_STATUS) 0x0002
#define AE_CTRL_TERMINATE               (ACPI_STATUS) 0x0003
#define AE_CTRL_TRUE                    (ACPI_STATUS) 0x0004
#define AE_CTRL_FALSE                   (ACPI_STATUS) 0x0005
#define AE_CTRL_DEPTH                   (ACPI_STATUS) 0x0006
#define AE_CTRL_RESERVED                (ACPI_STATUS) 0x0007
#define AE_AML_ERROR                    (ACPI_STATUS) 0x0008
#define AE_AML_PARSE                    (ACPI_STATUS) 0x0009
#define AE_AML_BAD_OPCODE               (ACPI_STATUS) 0x000A
#define AE_AML_NO_OPERAND               (ACPI_STATUS) 0x000B
#define AE_AML_OPERAND_TYPE             (ACPI_STATUS) 0x000C
#define AE_AML_OPERAND_VALUE            (ACPI_STATUS) 0x000D
#define AE_AML_UNINITIALIZED_LOCAL      (ACPI_STATUS) 0x000E
#define AE_AML_UNINITIALIZED_ARG        (ACPI_STATUS) 0x000F
#define AE_AML_UNINITIALIZED_ELEMENT    (ACPI_STATUS) 0x0010
#define AE_AML_NUMERIC_OVERFLOW         (ACPI_STATUS) 0x0011
#define AE_AML_REGION_LIMIT             (ACPI_STATUS) 0x0012
#define AE_AML_BUFFER_LIMIT             (ACPI_STATUS) 0x0013
#define AE_AML_PACKAGE_LIMIT            (ACPI_STATUS) 0x0014
#define AE_AML_DIVIDE_BY_ZERO           (ACPI_STATUS) 0x0015
#define AE_AML_BAD_NAME                 (ACPI_STATUS) 0x0016
#define AE_AML_NAME_NOT_FOUND           (ACPI_STATUS) 0x0017
#define AE_AML_INTERNAL                 (ACPI_STATUS) 0x0018
#define AE_AML_RESERVED                 (ACPI_STATUS) 0x0019
#define AE_ERROR                        (ACPI_STATUS) 0x001A
#define AE_NO_ACPI_TABLES               (ACPI_STATUS) 0x001B
#define AE_NO_NAMESPACE                 (ACPI_STATUS) 0x001C
#define AE_NO_MEMORY                    (ACPI_STATUS) 0x001D
#define AE_BAD_SIGNATURE                (ACPI_STATUS) 0x001E
#define AE_BAD_HEADER                   (ACPI_STATUS) 0x001F
#define AE_BAD_CHECKSUM                 (ACPI_STATUS) 0x0020
#define AE_BAD_PARAMETER                (ACPI_STATUS) 0x0021
#define AE_BAD_CHARACTER                (ACPI_STATUS) 0x0022
#define AE_BAD_PATHNAME                 (ACPI_STATUS) 0x0023
#define AE_BAD_DATA                     (ACPI_STATUS) 0x0024
#define AE_BAD_ADDRESS                  (ACPI_STATUS) 0x0025
#define AE_NOT_FOUND                    (ACPI_STATUS) 0x0026
#define AE_NOT_EXIST                    (ACPI_STATUS) 0x0027
#define AE_EXIST                        (ACPI_STATUS) 0x0028
#define AE_TYPE                         (ACPI_STATUS) 0x0029
#define AE_NULL_OBJECT                  (ACPI_STATUS) 0x002A
#define AE_NULL_ENTRY                   (ACPI_STATUS) 0x002B
#define AE_BUFFER_OVERFLOW              (ACPI_STATUS) 0x002C
#define AE_STACK_OVERFLOW               (ACPI_STATUS) 0x002D
#define AE_STACK_UNDERFLOW              (ACPI_STATUS) 0x002E
#define AE_NOT_IMPLEMENTED              (ACPI_STATUS) 0x002F
#define AE_VERSION_MISMATCH             (ACPI_STATUS) 0x0030
#define AE_SUPPORT                      (ACPI_STATUS) 0x0031
#define AE_SHARE                        (ACPI_STATUS) 0x0032
#define AE_LIMIT                        (ACPI_STATUS) 0x0033
#define AE_TIME                         (ACPI_STATUS) 0x0034
#define AE_UNKNOWN_STATUS               (ACPI_STATUS) 0x0035
#define ACPI_MAX_STATUS                 (ACPI_STATUS) 0x0035
#define ACPI_NUM_STATUS                 (ACPI_STATUS) 0x0036


#ifdef DEFINE_ACPI_GLOBALS

/*
 * String versions of the exception codes above
 * These strings must match the corresponding defines exactly
 */
static char                 *acpi_gbl_exception_names[] =
{
	"AE_OK",
	"AE_CTRL_RETURN_VALUE",
	"AE_CTRL_PENDING",
	"AE_CTRL_TERMINATE",
	"AE_CTRL_TRUE",
	"AE_CTRL_FALSE",
	"AE_CTRL_DEPTH",
	"AE_CTRL_RESERVED",
	"AE_AML_ERROR",
	"AE_AML_PARSE",
	"AE_AML_BAD_OPCODE",
	"AE_AML_NO_OPERAND",
	"AE_AML_OPERAND_TYPE",
	"AE_AML_OPERAND_VALUE",
	"AE_AML_UNINITIALIZED_LOCAL",
	"AE_AML_UNINITIALIZED_ARG",
	"AE_AML_UNINITIALIZED_ELEMENT",
	"AE_AML_NUMERIC_OVERFLOW",
	"AE_AML_REGION_LIMIT",
	"AE_AML_BUFFER_LIMIT",
	"AE_AML_PACKAGE_LIMIT",
	"AE_AML_DIVIDE_BY_ZERO",
	"AE_AML_BAD_NAME",
	"AE_AML_NAME_NOT_FOUND",
	"AE_AML_INTERNAL",
	"AE_AML_RESERVED",
	"AE_ERROR",
	"AE_NO_ACPI_TABLES",
	"AE_NO_NAMESPACE",
	"AE_NO_MEMORY",
	"AE_BAD_SIGNATURE",
	"AE_BAD_HEADER",
	"AE_BAD_CHECKSUM",
	"AE_BAD_PARAMETER",
	"AE_BAD_CHARACTER",
	"AE_BAD_PATHNAME",
	"AE_BAD_DATA",
	"AE_BAD_ADDRESS",
	"AE_NOT_FOUND",
	"AE_NOT_EXIST",
	"AE_EXIST",
	"AE_TYPE",
	"AE_NULL_OBJECT",
	"AE_NULL_ENTRY",
	"AE_BUFFER_OVERFLOW",
	"AE_STACK_OVERFLOW",
	"AE_STACK_UNDERFLOW",
	"AE_NOT_IMPLEMENTED",
	"AE_VERSION_MISMATCH",
	"AE_SUPPORT",
	"AE_SHARE",
	"AE_LIMIT",
	"AE_TIME",
	"AE_UNKNOWN_STATUS"
};

#endif /* DEFINE_ACPI_GLOBALS */


#endif /* __ACEXCEP_H__ */
