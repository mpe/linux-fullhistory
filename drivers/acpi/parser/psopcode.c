/******************************************************************************
 *
 * Module Name: psopcode - Parser opcode information table
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
#include "parser.h"
#include "amlcode.h"


#define _COMPONENT          PARSER
	 MODULE_NAME         ("psopcode");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_opcode_info
 *
 * PARAMETERS:  Opcode              - The AML opcode
 *
 * RETURN:      A pointer to the info about the opcode.  NULL if the opcode was
 *              not found in the table.
 *
 * DESCRIPTION: Find AML opcode description based on the opcode
 *
 ******************************************************************************/

ACPI_OP_INFO *
acpi_ps_get_opcode_info (
	u16                     opcode)
{
	ACPI_OP_INFO             *op;
	s32                     hash;


	/* compute hash/index into the Acpi_aml_op_index table */

	switch (opcode >> 8)
	{
	case 0:

		/* Simple (8-bit) opcode */

		hash = opcode;
		break;


	case AML_EXTOP:

		/* Extended (16-bit, prefix+opcode) opcode */

		hash = (opcode + AML_EXTOP_HASH_OFFSET) & 0xff;
		break;


	case AML_LNOT_OP:

		/* This case is for the bogus opcodes LNOTEQUAL, LLESSEQUAL, LGREATEREQUAL */

		hash = (opcode + AML_LNOT_HASH_OFFSET) & 0xff;
		break;


	default:

		return NULL;
	}


	/* Get the Op info pointer for this opcode */

	op = &acpi_gbl_aml_op_info [(s32) acpi_gbl_aml_op_info_index [hash]];


	/* If the returned opcode matches, we have a valid opcode */

	if (op->opcode == opcode) {
		return op;
	}

	/* Otherwise, the opcode is an ASCII char or other non-opcode value */

	return NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_opcode_name
 *
 * PARAMETERS:  Opcode              - The AML opcode
 *
 * RETURN:      A pointer to the name of the opcode (ASCII String)
 *              Note: Never returns NULL.
 *
 * DESCRIPTION: Translate an opcode into a human-readable string
 *
 ******************************************************************************/

char *
acpi_ps_get_opcode_name (
	u16                     opcode)
{
	ACPI_OP_INFO             *op;


	op = acpi_ps_get_opcode_info (opcode);

	if (!op) {
		return "*ERROR*";
	}

	DEBUG_ONLY_MEMBERS (return op->name);
	return "AE_NOT_CONFIGURED";
}


/*******************************************************************************
 *
 * NAME:        Acpi_gbl_Aml_op_info
 *
 * DESCRIPTION: Opcode table. Each entry contains <opcode, type, name, operands>
 *              The name is a simple ascii string, the operand specifier is an
 *              ascii string with one letter per operand.  The letter specifies
 *              the operand type.
 *
 ******************************************************************************/


/*
 * Flags byte: 0-4 (5 bits) = Opcode Type
 *             5   (1 bit)  = Has arguments flag
 *             6-7 (2 bits) = Reserved
 */
#define AML_NO_ARGS         0
#define AML_HAS_ARGS        OP_INFO_HAS_ARGS

/*
 * All AML opcodes and the parse-time arguments for each.  Used by the AML parser  Each list is compressed
 * into a 32-bit number and stored in the master opcode table at the end of this file.
 */

#define ARGP_ZERO_OP                    ARG_NONE
#define ARGP_ONE_OP                     ARG_NONE
#define ARGP_ALIAS_OP                   ARGP_LIST2 (ARGP_NAMESTRING, ARGP_NAME)
#define ARGP_NAME_OP                    ARGP_LIST2 (ARGP_NAME,       ARGP_DATAOBJ)
#define ARGP_BYTE_OP                    ARGP_LIST1 (ARGP_BYTEDATA)
#define ARGP_WORD_OP                    ARGP_LIST1 (ARGP_WORDDATA)
#define ARGP_DWORD_OP                   ARGP_LIST1 (ARGP_DWORDDATA)
#define ARGP_STRING_OP                  ARGP_LIST1 (ARGP_CHARLIST)
#define ARGP_SCOPE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_TERMLIST)
#define ARGP_BUFFER_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_BYTELIST)
#define ARGP_PACKAGE_OP                 ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_BYTEDATA,      ARGP_DATAOBJLIST)
#define ARGP_METHOD_OP                  ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,      ARGP_TERMLIST)
#define ARGP_LOCAL0                     ARG_NONE
#define ARGP_LOCAL1                     ARG_NONE
#define ARGP_LOCAL2                     ARG_NONE
#define ARGP_LOCAL3                     ARG_NONE
#define ARGP_LOCAL4                     ARG_NONE
#define ARGP_LOCAL5                     ARG_NONE
#define ARGP_LOCAL6                     ARG_NONE
#define ARGP_LOCAL7                     ARG_NONE
#define ARGP_ARG0                       ARG_NONE
#define ARGP_ARG1                       ARG_NONE
#define ARGP_ARG2                       ARG_NONE
#define ARGP_ARG3                       ARG_NONE
#define ARGP_ARG4                       ARG_NONE
#define ARGP_ARG5                       ARG_NONE
#define ARGP_ARG6                       ARG_NONE
#define ARGP_STORE_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_SUPERNAME)
#define ARGP_REF_OF_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_ADD_OP                     ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_CONCAT_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SUBTRACT_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_INCREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_DECREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_MULTIPLY_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_DIVIDE_OP                  ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET,    ARGP_TARGET)
#define ARGP_SHIFT_LEFT_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SHIFT_RIGHT_OP             ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_AND_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NAND_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_OR_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_XOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOT_OP                 ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FIND_SET_LEFT_BIT_OP       ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FIND_SET_RIGHT_BIT_OP      ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_DEREF_OF_OP                ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_NOTIFY_OP                  ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_SIZE_OF_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_INDEX_OP                   ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_MATCH_OP                   ARGP_LIST6 (ARGP_TERMARG,    ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_DWORD_FIELD_OP             ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_WORD_FIELD_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_BYTE_FIELD_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_BIT_FIELD_OP               ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_TYPE_OP                    ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_LAND_OP                    ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LOR_OP                     ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LNOT_OP                    ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_LEQUAL_OP                  ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATER_OP                ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESS_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_IF_OP                      ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG, ARGP_TERMLIST)
#define ARGP_ELSE_OP                    ARGP_LIST2 (ARGP_PKGLENGTH,  ARGP_TERMLIST)
#define ARGP_WHILE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG, ARGP_TERMLIST)
#define ARGP_NOOP_CODE                  ARG_NONE
#define ARGP_RETURN_OP                  ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_BREAK_OP                   ARG_NONE
#define ARGP_BREAK_POINT_OP             ARG_NONE
#define ARGP_ONES_OP                    ARG_NONE
#define ARGP_MUTEX_OP                   ARGP_LIST2 (ARGP_NAME,       ARGP_BYTEDATA)
#define ARGP_EVENT_OP                   ARGP_LIST1 (ARGP_NAME)
#define ARGP_COND_REF_OF_OP             ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_SUPERNAME)
#define ARGP_CREATE_FIELD_OP            ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_NAME)
#define ARGP_LOAD_OP                    ARGP_LIST2 (ARGP_NAMESTRING, ARGP_SUPERNAME)
#define ARGP_STALL_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_SLEEP_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_ACQUIRE_OP                 ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_WORDDATA)
#define ARGP_SIGNAL_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_WAIT_OP                    ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_RESET_OP                   ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_RELEASE_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_FROM_BCDOP                 ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_BCDOP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_UN_LOAD_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_REVISION_OP                ARG_NONE
#define ARGP_DEBUG_OP                   ARG_NONE
#define ARGP_FATAL_OP                   ARGP_LIST3 (ARGP_BYTEDATA,   ARGP_DWORDDATA,     ARGP_TERMARG)
#define ARGP_REGION_OP                  ARGP_LIST4 (ARGP_NAME,       ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_DEF_FIELD_OP               ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_DEVICE_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_PROCESSOR_OP               ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_DWORDDATA, ARGP_BYTEDATA,  ARGP_OBJLIST)
#define ARGP_POWER_RES_OP               ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_WORDDATA,  ARGP_OBJLIST)
#define ARGP_THERMAL_ZONE_OP            ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_INDEX_FIELD_OP             ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_BANK_FIELD_OP              ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_LNOTEQUAL_OP               ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESSEQUAL_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATEREQUAL_OP           ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_NAMEPATH_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_METHODCALL_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_BYTELIST_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_RESERVEDFIELD_OP           ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_NAMEDFIELD_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_ACCESSFIELD_OP             ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_STATICSTRING_OP            ARGP_LIST1 (ARGP_NAMESTRING)


/*
 * All AML opcodes and the runtime arguments for each.  Used by the AML interpreter  Each list is compressed
 * into a 32-bit number and stored in the master opcode table at the end of this file.
 *
 * (Used by Acpi_aml_prep_operands procedure)
 */

#define ARGI_ZERO_OP                    ARG_NONE
#define ARGI_ONE_OP                     ARG_NONE
#define ARGI_ALIAS_OP                   ARGI_INVALID_OPCODE
#define ARGI_NAME_OP                    ARGI_INVALID_OPCODE
#define ARGI_BYTE_OP                    ARGI_INVALID_OPCODE
#define ARGI_WORD_OP                    ARGI_INVALID_OPCODE
#define ARGI_DWORD_OP                   ARGI_INVALID_OPCODE
#define ARGI_STRING_OP                  ARGI_INVALID_OPCODE
#define ARGI_SCOPE_OP                   ARGI_INVALID_OPCODE
#define ARGI_BUFFER_OP                  ARGI_INVALID_OPCODE
#define ARGI_PACKAGE_OP                 ARGI_INVALID_OPCODE
#define ARGI_METHOD_OP                  ARGI_INVALID_OPCODE
#define ARGI_LOCAL0                     ARG_NONE
#define ARGI_LOCAL1                     ARG_NONE
#define ARGI_LOCAL2                     ARG_NONE
#define ARGI_LOCAL3                     ARG_NONE
#define ARGI_LOCAL4                     ARG_NONE
#define ARGI_LOCAL5                     ARG_NONE
#define ARGI_LOCAL6                     ARG_NONE
#define ARGI_LOCAL7                     ARG_NONE
#define ARGI_ARG0                       ARG_NONE
#define ARGI_ARG1                       ARG_NONE
#define ARGI_ARG2                       ARG_NONE
#define ARGI_ARG3                       ARG_NONE
#define ARGI_ARG4                       ARG_NONE
#define ARGI_ARG5                       ARG_NONE
#define ARGI_ARG6                       ARG_NONE
#define ARGI_STORE_OP                   ARGI_LIST2 (ARGI_ANYTYPE,    ARGI_TARGETREF)
#define ARGI_REF_OF_OP                  ARGI_LIST1 (ARGI_REFERENCE)
#define ARGI_ADD_OP                     ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_CONCAT_OP                  ARGI_LIST3 (ARGI_STRING,     ARGI_STRING,        ARGI_TARGETREF)
#define ARGI_SUBTRACT_OP                ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_INCREMENT_OP               ARGI_LIST1 (ARGI_REFERENCE)
#define ARGI_DECREMENT_OP               ARGI_LIST1 (ARGI_REFERENCE)
#define ARGI_MULTIPLY_OP                ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_DIVIDE_OP                  ARGI_LIST4 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF,    ARGI_TARGETREF)
#define ARGI_SHIFT_LEFT_OP              ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_SHIFT_RIGHT_OP             ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_AND_OP                 ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_NAND_OP                ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_OR_OP                  ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_NOR_OP                 ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_XOR_OP                 ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_BIT_NOT_OP                 ARGI_LIST2 (ARGI_NUMBER,     ARGI_TARGETREF)
#define ARGI_FIND_SET_LEFT_BIT_OP       ARGI_LIST2 (ARGI_NUMBER,     ARGI_TARGETREF)
#define ARGI_FIND_SET_RIGHT_BIT_OP      ARGI_LIST2 (ARGI_NUMBER,     ARGI_TARGETREF)
#define ARGI_DEREF_OF_OP                ARGI_LIST1 (ARGI_REFERENCE)
#define ARGI_NOTIFY_OP                  ARGI_LIST2 (ARGI_REFERENCE,  ARGI_NUMBER)
#define ARGI_SIZE_OF_OP                 ARGI_LIST1 (ARGI_DATAOBJECT)
#define ARGI_INDEX_OP                   ARGI_LIST3 (ARGI_COMPLEXOBJ, ARGI_NUMBER,        ARGI_TARGETREF)
#define ARGI_MATCH_OP                   ARGI_LIST6 (ARGI_PACKAGE,    ARGI_NUMBER,        ARGI_NUMBER,       ARGI_NUMBER,    ARGI_NUMBER,    ARGI_NUMBER)
#define ARGI_DWORD_FIELD_OP             ARGI_LIST3 (ARGI_BUFFER,     ARGI_NUMBER,        ARGI_REFERENCE)
#define ARGI_WORD_FIELD_OP              ARGI_LIST3 (ARGI_BUFFER,     ARGI_NUMBER,        ARGI_REFERENCE)
#define ARGI_BYTE_FIELD_OP              ARGI_LIST3 (ARGI_BUFFER,     ARGI_NUMBER,        ARGI_REFERENCE)
#define ARGI_BIT_FIELD_OP               ARGI_LIST3 (ARGI_BUFFER,     ARGI_NUMBER,        ARGI_REFERENCE)
#define ARGI_TYPE_OP                    ARGI_LIST1 (ARGI_ANYTYPE)
#define ARGI_LAND_OP                    ARGI_LIST2 (ARGI_NUMBER,     ARGI_NUMBER)
#define ARGI_LOR_OP                     ARGI_LIST2 (ARGI_NUMBER,     ARGI_NUMBER)
#define ARGI_LNOT_OP                    ARGI_LIST1 (ARGI_NUMBER)
#define ARGI_LEQUAL_OP                  ARGI_LIST2 (ARGI_NUMBER,     ARGI_NUMBER)
#define ARGI_LGREATER_OP                ARGI_LIST2 (ARGI_NUMBER,     ARGI_NUMBER)
#define ARGI_LLESS_OP                   ARGI_LIST2 (ARGI_NUMBER,     ARGI_NUMBER)
#define ARGI_IF_OP                      ARGI_INVALID_OPCODE
#define ARGI_ELSE_OP                    ARGI_INVALID_OPCODE
#define ARGI_WHILE_OP                   ARGI_INVALID_OPCODE
#define ARGI_NOOP_CODE                  ARG_NONE
#define ARGI_RETURN_OP                  ARGI_INVALID_OPCODE
#define ARGI_BREAK_OP                   ARG_NONE
#define ARGI_BREAK_POINT_OP             ARG_NONE
#define ARGI_ONES_OP                    ARG_NONE
#define ARGI_MUTEX_OP                   ARGI_INVALID_OPCODE
#define ARGI_EVENT_OP                   ARGI_INVALID_OPCODE
#define ARGI_COND_REF_OF_OP             ARGI_LIST2 (ARGI_REFERENCE,  ARGI_TARGETREF)
#define ARGI_CREATE_FIELD_OP            ARGI_LIST4 (ARGI_BUFFER,     ARGI_NUMBER,        ARGI_NUMBER,       ARGI_REFERENCE)
#define ARGI_LOAD_OP                    ARGI_LIST2 (ARGI_REGION,     ARGI_TARGETREF)
#define ARGI_STALL_OP                   ARGI_LIST1 (ARGI_NUMBER)
#define ARGI_SLEEP_OP                   ARGI_LIST1 (ARGI_NUMBER)
#define ARGI_ACQUIRE_OP                 ARGI_LIST2 (ARGI_MUTEX,      ARGI_NUMBER)
#define ARGI_SIGNAL_OP                  ARGI_LIST1 (ARGI_EVENT)
#define ARGI_WAIT_OP                    ARGI_LIST2 (ARGI_EVENT,      ARGI_NUMBER)
#define ARGI_RESET_OP                   ARGI_LIST1 (ARGI_EVENT)
#define ARGI_RELEASE_OP                 ARGI_LIST1 (ARGI_MUTEX)
#define ARGI_FROM_BCDOP                 ARGI_LIST2 (ARGI_NUMBER,     ARGI_TARGETREF)
#define ARGI_TO_BCDOP                   ARGI_LIST2 (ARGI_NUMBER,     ARGI_TARGETREF)
#define ARGI_UN_LOAD_OP                 ARGI_LIST1 (ARGI_DDBHANDLE)
#define ARGI_REVISION_OP                ARG_NONE
#define ARGI_DEBUG_OP                   ARG_NONE
#define ARGI_FATAL_OP                   ARGI_LIST3 (ARGI_NUMBER,     ARGI_NUMBER,        ARGI_NUMBER)
#define ARGI_REGION_OP                  ARGI_INVALID_OPCODE
#define ARGI_DEF_FIELD_OP               ARGI_INVALID_OPCODE
#define ARGI_DEVICE_OP                  ARGI_INVALID_OPCODE
#define ARGI_PROCESSOR_OP               ARGI_INVALID_OPCODE
#define ARGI_POWER_RES_OP               ARGI_INVALID_OPCODE
#define ARGI_THERMAL_ZONE_OP            ARGI_INVALID_OPCODE
#define ARGI_INDEX_FIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_BANK_FIELD_OP              ARGI_INVALID_OPCODE
#define ARGI_LNOTEQUAL_OP               ARGI_INVALID_OPCODE
#define ARGI_LLESSEQUAL_OP              ARGI_INVALID_OPCODE
#define ARGI_LGREATEREQUAL_OP           ARGI_INVALID_OPCODE
#define ARGI_NAMEPATH_OP                ARGI_INVALID_OPCODE
#define ARGI_METHODCALL_OP              ARGI_INVALID_OPCODE
#define ARGI_BYTELIST_OP                ARGI_INVALID_OPCODE
#define ARGI_RESERVEDFIELD_OP           ARGI_INVALID_OPCODE
#define ARGI_NAMEDFIELD_OP              ARGI_INVALID_OPCODE
#define ARGI_ACCESSFIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_STATICSTRING_OP            ARGI_INVALID_OPCODE


/*
 * Master Opcode information table.  A summary of everything we know about each opcode, all in one place.
 */


ACPI_OP_INFO        acpi_gbl_aml_op_info[] =
{
/*                          Opcode                 Opcode Type           Has Arguments? Child  Name                 Parser Args             Interpreter Args */

/*  00 */   OP_INFO_ENTRY (AML_ZERO_OP,           OPTYPE_CONSTANT|        AML_NO_ARGS|    0,  "Zero_op",            ARGP_ZERO_OP,           ARGI_ZERO_OP),
/*  01 */   OP_INFO_ENTRY (AML_ONE_OP,            OPTYPE_CONSTANT|        AML_NO_ARGS|    0,  "One_op",             ARGP_ONE_OP,            ARGI_ONE_OP),
/*  02 */   OP_INFO_ENTRY (AML_ALIAS_OP,          OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Alias",              ARGP_ALIAS_OP,          ARGI_ALIAS_OP),
/*  03 */   OP_INFO_ENTRY (AML_NAME_OP,           OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Name",               ARGP_NAME_OP,           ARGI_NAME_OP),
/*  04 */   OP_INFO_ENTRY (AML_BYTE_OP,           OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "Byte_const",         ARGP_BYTE_OP,           ARGI_BYTE_OP),
/*  05 */   OP_INFO_ENTRY (AML_WORD_OP,           OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "Word_const",         ARGP_WORD_OP,           ARGI_WORD_OP),
/*  06 */   OP_INFO_ENTRY (AML_DWORD_OP,          OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "Dword_const",        ARGP_DWORD_OP,          ARGI_DWORD_OP),
/*  07 */   OP_INFO_ENTRY (AML_STRING_OP,         OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "String",             ARGP_STRING_OP,         ARGI_STRING_OP),
/*  08 */   OP_INFO_ENTRY (AML_SCOPE_OP,          OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Scope",              ARGP_SCOPE_OP,          ARGI_SCOPE_OP),
/*  09 */   OP_INFO_ENTRY (AML_BUFFER_OP,         OPTYPE_DATA_TERM|       AML_HAS_ARGS|   0,  "Buffer",             ARGP_BUFFER_OP,         ARGI_BUFFER_OP),
/*  0A */   OP_INFO_ENTRY (AML_PACKAGE_OP,        OPTYPE_DATA_TERM|       AML_HAS_ARGS|   0,  "Package",            ARGP_PACKAGE_OP,        ARGI_PACKAGE_OP),
/*  0B */   OP_INFO_ENTRY (AML_METHOD_OP,         OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Method",             ARGP_METHOD_OP,         ARGI_METHOD_OP),
/*  0C */   OP_INFO_ENTRY (AML_LOCAL0,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local0",             ARGP_LOCAL0,            ARGI_LOCAL0),
/*  0D */   OP_INFO_ENTRY (AML_LOCAL1,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local1",             ARGP_LOCAL1,            ARGI_LOCAL1),
/*  0E */   OP_INFO_ENTRY (AML_LOCAL2,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local2",             ARGP_LOCAL2,            ARGI_LOCAL2),
/*  0F */   OP_INFO_ENTRY (AML_LOCAL3,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local3",             ARGP_LOCAL3,            ARGI_LOCAL3),
/*  10 */   OP_INFO_ENTRY (AML_LOCAL4,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local4",             ARGP_LOCAL4,            ARGI_LOCAL4),
/*  11 */   OP_INFO_ENTRY (AML_LOCAL5,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local5",             ARGP_LOCAL5,            ARGI_LOCAL5),
/*  12 */   OP_INFO_ENTRY (AML_LOCAL6,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local6",             ARGP_LOCAL6,            ARGI_LOCAL6),
/*  13 */   OP_INFO_ENTRY (AML_LOCAL7,            OPTYPE_LOCAL_VARIABLE|  AML_NO_ARGS|    0,  "Local7",             ARGP_LOCAL7,            ARGI_LOCAL7),
/*  14 */   OP_INFO_ENTRY (AML_ARG0,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg0",               ARGP_ARG0,              ARGI_ARG0),
/*  15 */   OP_INFO_ENTRY (AML_ARG1,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg1",               ARGP_ARG1,              ARGI_ARG1),
/*  16 */   OP_INFO_ENTRY (AML_ARG2,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg2",               ARGP_ARG2,              ARGI_ARG2),
/*  17 */   OP_INFO_ENTRY (AML_ARG3,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg3",               ARGP_ARG3,              ARGI_ARG3),
/*  18 */   OP_INFO_ENTRY (AML_ARG4,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg4",               ARGP_ARG4,              ARGI_ARG4),
/*  19 */   OP_INFO_ENTRY (AML_ARG5,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg5",               ARGP_ARG5,              ARGI_ARG5),
/*  1_a */  OP_INFO_ENTRY (AML_ARG6,              OPTYPE_METHOD_ARGUMENT| AML_NO_ARGS|    0,  "Arg6",               ARGP_ARG6,              ARGI_ARG6),
/*  1_b */  OP_INFO_ENTRY (AML_STORE_OP,          OPTYPE_MONADIC2_r|      AML_HAS_ARGS|   0,  "Store",              ARGP_STORE_OP,          ARGI_STORE_OP),
/*  1_c */  OP_INFO_ENTRY (AML_REF_OF_OP,         OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Ref_of",             ARGP_REF_OF_OP,         ARGI_REF_OF_OP),
/*  1_d */  OP_INFO_ENTRY (AML_ADD_OP,            OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Add",                ARGP_ADD_OP,            ARGI_ADD_OP),
/*  1_e */  OP_INFO_ENTRY (AML_CONCAT_OP,         OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Concat",             ARGP_CONCAT_OP,         ARGI_CONCAT_OP),
/*  1_f */  OP_INFO_ENTRY (AML_SUBTRACT_OP,       OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Subtract",           ARGP_SUBTRACT_OP,       ARGI_SUBTRACT_OP),
/*  20 */   OP_INFO_ENTRY (AML_INCREMENT_OP,      OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Increment",          ARGP_INCREMENT_OP,      ARGI_INCREMENT_OP),
/*  21 */   OP_INFO_ENTRY (AML_DECREMENT_OP,      OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Decrement",          ARGP_DECREMENT_OP,      ARGI_DECREMENT_OP),
/*  22 */   OP_INFO_ENTRY (AML_MULTIPLY_OP,       OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Multiply",           ARGP_MULTIPLY_OP,       ARGI_MULTIPLY_OP),
/*  23 */   OP_INFO_ENTRY (AML_DIVIDE_OP,         OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Divide",             ARGP_DIVIDE_OP,         ARGI_DIVIDE_OP),
/*  24 */   OP_INFO_ENTRY (AML_SHIFT_LEFT_OP,     OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Shift_left",         ARGP_SHIFT_LEFT_OP,     ARGI_SHIFT_LEFT_OP),
/*  25 */   OP_INFO_ENTRY (AML_SHIFT_RIGHT_OP,    OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Shift_right",        ARGP_SHIFT_RIGHT_OP,    ARGI_SHIFT_RIGHT_OP),
/*  26 */   OP_INFO_ENTRY (AML_BIT_AND_OP,        OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "And",                ARGP_BIT_AND_OP,        ARGI_BIT_AND_OP),
/*  27 */   OP_INFO_ENTRY (AML_BIT_NAND_OP,       OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "NAnd",               ARGP_BIT_NAND_OP,       ARGI_BIT_NAND_OP),
/*  28 */   OP_INFO_ENTRY (AML_BIT_OR_OP,         OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "Or",                 ARGP_BIT_OR_OP,         ARGI_BIT_OR_OP),
/*  29 */   OP_INFO_ENTRY (AML_BIT_NOR_OP,        OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "NOr",                ARGP_BIT_NOR_OP,        ARGI_BIT_NOR_OP),
/*  2_a */  OP_INFO_ENTRY (AML_BIT_XOR_OP,        OPTYPE_DYADIC2_r|       AML_HAS_ARGS|   0,  "XOr",                ARGP_BIT_XOR_OP,        ARGI_BIT_XOR_OP),
/*  2_b */  OP_INFO_ENTRY (AML_BIT_NOT_OP,        OPTYPE_MONADIC2_r|      AML_HAS_ARGS|   0,  "Not",                ARGP_BIT_NOT_OP,        ARGI_BIT_NOT_OP),
/*  2_c */  OP_INFO_ENTRY (AML_FIND_SET_LEFT_BIT_OP, OPTYPE_MONADIC2_r|   AML_HAS_ARGS|   0,  "Find_set_left_bit",  ARGP_FIND_SET_LEFT_BIT_OP, ARGI_FIND_SET_LEFT_BIT_OP),
/*  2_d */  OP_INFO_ENTRY (AML_FIND_SET_RIGHT_BIT_OP, OPTYPE_MONADIC2_r|  AML_HAS_ARGS|   0,  "Find_set_right_bit", ARGP_FIND_SET_RIGHT_BIT_OP, ARGI_FIND_SET_RIGHT_BIT_OP),
/*  2_e */  OP_INFO_ENTRY (AML_DEREF_OF_OP,       OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Deref_of",           ARGP_DEREF_OF_OP,       ARGI_DEREF_OF_OP),
/*  2_f */  OP_INFO_ENTRY (AML_NOTIFY_OP,         OPTYPE_DYADIC1|         AML_HAS_ARGS|   0,  "Notify",             ARGP_NOTIFY_OP,         ARGI_NOTIFY_OP),
/*  30 */   OP_INFO_ENTRY (AML_SIZE_OF_OP,        OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Size_of",            ARGP_SIZE_OF_OP,        ARGI_SIZE_OF_OP),
/*  31 */   OP_INFO_ENTRY (AML_INDEX_OP,          OPTYPE_INDEX|           AML_HAS_ARGS|   0,  "Index",              ARGP_INDEX_OP,          ARGI_INDEX_OP),
/*  32 */   OP_INFO_ENTRY (AML_MATCH_OP,          OPTYPE_MATCH|           AML_HAS_ARGS|   0,  "Match",              ARGP_MATCH_OP,          ARGI_MATCH_OP),
/*  33 */   OP_INFO_ENTRY (AML_DWORD_FIELD_OP,    OPTYPE_CREATE_FIELD|    AML_HAS_ARGS|   0,  "Create_dWord_field", ARGP_DWORD_FIELD_OP,    ARGI_DWORD_FIELD_OP),
/*  34 */   OP_INFO_ENTRY (AML_WORD_FIELD_OP,     OPTYPE_CREATE_FIELD|    AML_HAS_ARGS|   0,  "Create_word_field",  ARGP_WORD_FIELD_OP,     ARGI_WORD_FIELD_OP),
/*  35 */   OP_INFO_ENTRY (AML_BYTE_FIELD_OP,     OPTYPE_CREATE_FIELD|    AML_HAS_ARGS|   0,  "Create_byte_field",  ARGP_BYTE_FIELD_OP,     ARGI_BYTE_FIELD_OP),
/*  36 */   OP_INFO_ENTRY (AML_BIT_FIELD_OP,      OPTYPE_CREATE_FIELD|    AML_HAS_ARGS|   0,  "Create_bit_field",   ARGP_BIT_FIELD_OP,      ARGI_BIT_FIELD_OP),
/*  37 */   OP_INFO_ENTRY (AML_TYPE_OP,           OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "Object_type",        ARGP_TYPE_OP,           ARGI_TYPE_OP),
/*  38 */   OP_INFO_ENTRY (AML_LAND_OP,           OPTYPE_DYADIC2|         AML_HAS_ARGS|   0,  "LAnd",               ARGP_LAND_OP,           ARGI_LAND_OP),
/*  39 */   OP_INFO_ENTRY (AML_LOR_OP,            OPTYPE_DYADIC2|         AML_HAS_ARGS|   0,  "LOr",                ARGP_LOR_OP,            ARGI_LOR_OP),
/*  3_a */  OP_INFO_ENTRY (AML_LNOT_OP,           OPTYPE_MONADIC2|        AML_HAS_ARGS|   0,  "LNot",               ARGP_LNOT_OP,           ARGI_LNOT_OP),
/*  3_b */  OP_INFO_ENTRY (AML_LEQUAL_OP,         OPTYPE_DYADIC2|         AML_HAS_ARGS|   0,  "LEqual",             ARGP_LEQUAL_OP,         ARGI_LEQUAL_OP),
/*  3_c */  OP_INFO_ENTRY (AML_LGREATER_OP,       OPTYPE_DYADIC2|         AML_HAS_ARGS|   0,  "LGreater",           ARGP_LGREATER_OP,       ARGI_LGREATER_OP),
/*  3_d */  OP_INFO_ENTRY (AML_LLESS_OP,          OPTYPE_DYADIC2|         AML_HAS_ARGS|   0,  "LLess",              ARGP_LLESS_OP,          ARGI_LLESS_OP),
/*  3_e */  OP_INFO_ENTRY (AML_IF_OP,             OPTYPE_CONTROL|         AML_HAS_ARGS|   0,  "If",                 ARGP_IF_OP,             ARGI_IF_OP),
/*  3_f */  OP_INFO_ENTRY (AML_ELSE_OP,           OPTYPE_CONTROL|         AML_HAS_ARGS|   0,  "Else",               ARGP_ELSE_OP,           ARGI_ELSE_OP),
/*  40 */   OP_INFO_ENTRY (AML_WHILE_OP,          OPTYPE_CONTROL|         AML_HAS_ARGS|   0,  "While",              ARGP_WHILE_OP,          ARGI_WHILE_OP),
/*  41 */   OP_INFO_ENTRY (AML_NOOP_CODE,         OPTYPE_CONTROL|         AML_NO_ARGS|    0,  "Noop",               ARGP_NOOP_CODE,         ARGI_NOOP_CODE),
/*  42 */   OP_INFO_ENTRY (AML_RETURN_OP,         OPTYPE_CONTROL|         AML_HAS_ARGS|   0,  "Return",             ARGP_RETURN_OP,         ARGI_RETURN_OP),
/*  43 */   OP_INFO_ENTRY (AML_BREAK_OP,          OPTYPE_CONTROL|         AML_NO_ARGS|    0,  "Break",              ARGP_BREAK_OP,          ARGI_BREAK_OP),
/*  44 */   OP_INFO_ENTRY (AML_BREAK_POINT_OP,    OPTYPE_CONTROL|         AML_NO_ARGS|    0,  "Break_point",        ARGP_BREAK_POINT_OP,    ARGI_BREAK_POINT_OP),
/*  45 */   OP_INFO_ENTRY (AML_ONES_OP,           OPTYPE_CONSTANT|        AML_NO_ARGS|    0,  "Ones_op",            ARGP_ONES_OP,           ARGI_ONES_OP),

/* Prefixed opcodes (Two-byte opcodes with a prefix op) */

/*  46 */   OP_INFO_ENTRY (AML_MUTEX_OP,          OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Mutex",              ARGP_MUTEX_OP,          ARGI_MUTEX_OP),
/*  47 */   OP_INFO_ENTRY (AML_EVENT_OP,          OPTYPE_NAMED_OBJECT|    AML_NO_ARGS|    0,  "Event",              ARGP_EVENT_OP,          ARGI_EVENT_OP),
/*  48 */   OP_INFO_ENTRY (AML_COND_REF_OF_OP,    OPTYPE_MONADIC2_r|      AML_HAS_ARGS|   0,  "Cond_ref_of",        ARGP_COND_REF_OF_OP,    ARGI_COND_REF_OF_OP),
/*  49 */   OP_INFO_ENTRY (AML_CREATE_FIELD_OP,   OPTYPE_CREATE_FIELD|    AML_HAS_ARGS|   0,  "Create_field",       ARGP_CREATE_FIELD_OP,   ARGI_CREATE_FIELD_OP),
/*  4_a */  OP_INFO_ENTRY (AML_LOAD_OP,           OPTYPE_RECONFIGURATION| AML_HAS_ARGS|   0,  "Load",               ARGP_LOAD_OP,           ARGI_LOAD_OP),
/*  4_b */  OP_INFO_ENTRY (AML_STALL_OP,          OPTYPE_MONADIC1|        AML_HAS_ARGS|   0,  "Stall",              ARGP_STALL_OP,          ARGI_STALL_OP),
/*  4_c */  OP_INFO_ENTRY (AML_SLEEP_OP,          OPTYPE_MONADIC1|        AML_HAS_ARGS|   0,  "Sleep",              ARGP_SLEEP_OP,          ARGI_SLEEP_OP),
/*  4_d */  OP_INFO_ENTRY (AML_ACQUIRE_OP,        OPTYPE_DYADIC2_s|       AML_HAS_ARGS|   0,  "Acquire",            ARGP_ACQUIRE_OP,        ARGI_ACQUIRE_OP),
/*  4_e */  OP_INFO_ENTRY (AML_SIGNAL_OP,         OPTYPE_MONADIC1|        AML_HAS_ARGS|   0,  "Signal",             ARGP_SIGNAL_OP,         ARGI_SIGNAL_OP),
/*  4_f */  OP_INFO_ENTRY (AML_WAIT_OP,           OPTYPE_DYADIC2_s|       AML_HAS_ARGS|   0,  "Wait",               ARGP_WAIT_OP,           ARGI_WAIT_OP),
/*  50 */   OP_INFO_ENTRY (AML_RESET_OP,          OPTYPE_MONADIC1|        AML_HAS_ARGS|   0,  "Reset",              ARGP_RESET_OP,          ARGI_RESET_OP),
/*  51 */   OP_INFO_ENTRY (AML_RELEASE_OP,        OPTYPE_MONADIC1|        AML_HAS_ARGS|   0,  "Release",            ARGP_RELEASE_OP,        ARGI_RELEASE_OP),
/*  52 */   OP_INFO_ENTRY (AML_FROM_BCDOP,        OPTYPE_MONADIC2_r|      AML_HAS_ARGS|   0,  "From_bCD",           ARGP_FROM_BCDOP,        ARGI_FROM_BCDOP),
/*  53 */   OP_INFO_ENTRY (AML_TO_BCDOP,          OPTYPE_MONADIC2_r|      AML_HAS_ARGS|   0,  "To_bCD",             ARGP_TO_BCDOP,          ARGI_TO_BCDOP),
/*  54 */   OP_INFO_ENTRY (AML_UN_LOAD_OP,        OPTYPE_RECONFIGURATION| AML_HAS_ARGS|   0,  "Unload",             ARGP_UN_LOAD_OP,        ARGI_UN_LOAD_OP),
/*  55 */   OP_INFO_ENTRY (AML_REVISION_OP,       OPTYPE_CONSTANT|        AML_NO_ARGS|    0,  "Revision",           ARGP_REVISION_OP,       ARGI_REVISION_OP),
/*  56 */   OP_INFO_ENTRY (AML_DEBUG_OP,          OPTYPE_CONSTANT|        AML_NO_ARGS|    0,  "Debug",              ARGP_DEBUG_OP,          ARGI_DEBUG_OP),
/*  57 */   OP_INFO_ENTRY (AML_FATAL_OP,          OPTYPE_FATAL|           AML_HAS_ARGS|   0,  "Fatal",              ARGP_FATAL_OP,          ARGI_FATAL_OP),
/*  58 */   OP_INFO_ENTRY (AML_REGION_OP,         OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Op_region",          ARGP_REGION_OP,         ARGI_REGION_OP),
/*  59 */   OP_INFO_ENTRY (AML_DEF_FIELD_OP,      OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Field",              ARGP_DEF_FIELD_OP,      ARGI_DEF_FIELD_OP),
/*  5_a */  OP_INFO_ENTRY (AML_DEVICE_OP,         OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Device",             ARGP_DEVICE_OP,         ARGI_DEVICE_OP),
/*  5_b */  OP_INFO_ENTRY (AML_PROCESSOR_OP,      OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Processor",          ARGP_PROCESSOR_OP,      ARGI_PROCESSOR_OP),
/*  5_c */  OP_INFO_ENTRY (AML_POWER_RES_OP,      OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Power_res",          ARGP_POWER_RES_OP,      ARGI_POWER_RES_OP),
/*  5_d */  OP_INFO_ENTRY (AML_THERMAL_ZONE_OP,   OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Thermal_zone",       ARGP_THERMAL_ZONE_OP,   ARGI_THERMAL_ZONE_OP),
/*  5_e */  OP_INFO_ENTRY (AML_INDEX_FIELD_OP,    OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Index_field",        ARGP_INDEX_FIELD_OP,    ARGI_INDEX_FIELD_OP),
/*  5_f */  OP_INFO_ENTRY (AML_BANK_FIELD_OP,     OPTYPE_NAMED_OBJECT|    AML_HAS_ARGS|   0,  "Bank_field",         ARGP_BANK_FIELD_OP,     ARGI_BANK_FIELD_OP),

/* Internal opcodes that map to invalid AML opcodes */

/*  60 */   OP_INFO_ENTRY (AML_LNOTEQUAL_OP,      OPTYPE_BOGUS|           AML_HAS_ARGS|   0,  "LNot_equal",         ARGP_LNOTEQUAL_OP,      ARGI_LNOTEQUAL_OP),
/*  61 */   OP_INFO_ENTRY (AML_LLESSEQUAL_OP,     OPTYPE_BOGUS|           AML_HAS_ARGS|   0,  "LLess_equal",        ARGP_LLESSEQUAL_OP,     ARGI_LLESSEQUAL_OP),
/*  62 */   OP_INFO_ENTRY (AML_LGREATEREQUAL_OP,  OPTYPE_BOGUS|           AML_HAS_ARGS|   0,  "LGreater_equal",     ARGP_LGREATEREQUAL_OP,  ARGI_LGREATEREQUAL_OP),
/*  63 */   OP_INFO_ENTRY (AML_NAMEPATH_OP,       OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "Name_path",          ARGP_NAMEPATH_OP,       ARGI_NAMEPATH_OP),
/*  64 */   OP_INFO_ENTRY (AML_METHODCALL_OP,     OPTYPE_METHOD_CALL|     AML_HAS_ARGS|   0,  "Method_call",        ARGP_METHODCALL_OP,     ARGI_METHODCALL_OP),
/*  65 */   OP_INFO_ENTRY (AML_BYTELIST_OP,       OPTYPE_LITERAL|         AML_NO_ARGS|    0,  "Byte_list",          ARGP_BYTELIST_OP,       ARGI_BYTELIST_OP),
/*  66 */   OP_INFO_ENTRY (AML_RESERVEDFIELD_OP,  OPTYPE_BOGUS|           AML_NO_ARGS|    0,  "Reserved_field",     ARGP_RESERVEDFIELD_OP,  ARGI_RESERVEDFIELD_OP),
/*  67 */   OP_INFO_ENTRY (AML_NAMEDFIELD_OP,     OPTYPE_BOGUS|           AML_NO_ARGS|    0,  "Named_field",        ARGP_NAMEDFIELD_OP,     ARGI_NAMEDFIELD_OP),
/*  68 */   OP_INFO_ENTRY (AML_ACCESSFIELD_OP,    OPTYPE_BOGUS|           AML_NO_ARGS|    0,  "Access_field",       ARGP_ACCESSFIELD_OP,    ARGI_ACCESSFIELD_OP),
/*  69 */   OP_INFO_ENTRY (AML_STATICSTRING_OP,   OPTYPE_BOGUS|           AML_NO_ARGS|    0,  "Static_string",      ARGP_STATICSTRING_OP,   ARGI_STATICSTRING_OP),
/*  6_a */  OP_INFO_ENTRY (0,                     OPTYPE_BOGUS|           AML_HAS_ARGS|   0,  "UNKNOWN_OP!",        ARG_NONE,               ARG_NONE),
			OP_INFO_ENTRY (0,                     0|                      AML_HAS_ARGS|   0,  NULL,                 ARG_NONE,               ARG_NONE)
};

#define _UNK                0x6A
#define _UNKNOWN_OPCODE     0x02    /* An example unknown opcode */

/*
 * This table is directly indexed by the opcodes, and returns an
 * index into the table above
 */

u8 acpi_gbl_aml_op_info_index[256] =
{
/*              0     1     2     3     4     5     6     7  */
/* 0x00 */    0x00, 0x01, _UNK, _UNK, _UNK, _UNK, 0x02, _UNK,
/* 0x08 */    0x03, _UNK, 0x04, 0x05, 0x06, 0x07, _UNK, _UNK,
/* 0x10 */    0x08, 0x09, 0x0a, _UNK, 0x0b, _UNK, _UNK, 0x46,
/* 0x18 */    0x47, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x20 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x28 */    0x48, 0x49, _UNK, _UNK, _UNK, 0x63, _UNK, _UNK,
/* 0x30 */    0x67, 0x66, 0x68, 0x65, 0x69, 0x64, 0x4a, 0x4b,
/* 0x38 */    0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
/* 0x40 */    0x54, _UNK, _UNK, _UNK, _UNK, _UNK, 0x55, 0x56,
/* 0x48 */    0x57, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x50 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x58 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x60 */    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
/* 0x68 */    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, _UNK,
/* 0x70 */    0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22,
/* 0x78 */    0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
/* 0x80 */    0x2b, 0x2c, 0x2d, 0x2e, _UNK, _UNK, 0x2f, 0x30,
/* 0x88 */    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, _UNK,
/* 0x90 */    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x58, 0x59,
/* 0x98 */    0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, _UNK, _UNK,
/* 0xA0 */    0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x60, 0x61,
/* 0xA8 */    0x62, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC8 */    _UNK, _UNK, _UNK, _UNK, 0x44, _UNK, _UNK, _UNK,
/* 0xD0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xD8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, 0x45,
};
