/******************************************************************************
 *
 * Module Name: psopcode - Parser/Interpreter opcode information table
 *              $Revision: 49 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
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
#include "acparser.h"
#include "amlcode.h"


#define _COMPONENT          ACPI_PARSER
	 MODULE_NAME         ("psopcode")


#define _UNK                        0x6B
/*
 * Reserved ASCII characters.  Do not use any of these for
 * internal opcodes, since they are used to differentiate
 * name strings from AML opcodes
 */
#define _ASC                        0x6C
#define _NAM                        0x6C
#define _PFX                        0x6D
#define _UNKNOWN_OPCODE             0x02    /* An example unknown opcode */

#define MAX_EXTENDED_OPCODE         0x88
#define NUM_EXTENDED_OPCODE         MAX_EXTENDED_OPCODE + 1
#define MAX_INTERNAL_OPCODE
#define NUM_INTERNAL_OPCODE         MAX_INTERNAL_OPCODE + 1


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
 * All AML opcodes and the parse-time arguments for each.  Used by the AML parser  Each list is compressed
 * into a 32-bit number and stored in the master opcode table at the end of this file.
 */


#define ARGP_ACCESSFIELD_OP             ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_ACQUIRE_OP                 ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_WORDDATA)
#define ARGP_ADD_OP                     ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_ALIAS_OP                   ARGP_LIST2 (ARGP_NAMESTRING, ARGP_NAME)
#define ARGP_ARG0                       ARG_NONE
#define ARGP_ARG1                       ARG_NONE
#define ARGP_ARG2                       ARG_NONE
#define ARGP_ARG3                       ARG_NONE
#define ARGP_ARG4                       ARG_NONE
#define ARGP_ARG5                       ARG_NONE
#define ARGP_ARG6                       ARG_NONE
#define ARGP_BANK_FIELD_OP              ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_BIT_AND_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NAND_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOT_OP                 ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_BIT_OR_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_XOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BREAK_OP                   ARG_NONE
#define ARGP_BREAK_POINT_OP             ARG_NONE
#define ARGP_BUFFER_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_BYTELIST)
#define ARGP_BYTE_OP                    ARGP_LIST1 (ARGP_BYTEDATA)
#define ARGP_BYTELIST_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_CONCAT_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_CONCAT_RES_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_COND_REF_OF_OP             ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_SUPERNAME)
#define ARGP_CONTINUE_OP                ARG_NONE
#define ARGP_COPY_OP                    ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_SIMPLENAME)
#define ARGP_CREATE_BIT_FIELD_OP        ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_BYTE_FIELD_OP       ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_DWORD_FIELD_OP      ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_FIELD_OP            ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_NAME)
#define ARGP_CREATE_QWORD_FIELD_OP      ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_WORD_FIELD_OP       ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_DATA_REGION_OP             ARGP_LIST4 (ARGP_NAME,       ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_DEBUG_OP                   ARG_NONE
#define ARGP_DECREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_DEREF_OF_OP                ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_DEVICE_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_DIVIDE_OP                  ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET,    ARGP_TARGET)
#define ARGP_DWORD_OP                   ARGP_LIST1 (ARGP_DWORDDATA)
#define ARGP_ELSE_OP                    ARGP_LIST2 (ARGP_PKGLENGTH,  ARGP_TERMLIST)
#define ARGP_EVENT_OP                   ARGP_LIST1 (ARGP_NAME)
#define ARGP_FATAL_OP                   ARGP_LIST3 (ARGP_BYTEDATA,   ARGP_DWORDDATA,     ARGP_TERMARG)
#define ARGP_FIELD_OP                   ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_FIND_SET_LEFT_BIT_OP       ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FIND_SET_RIGHT_BIT_OP      ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FROM_BCD_OP                ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_IF_OP                      ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_TERMLIST)
#define ARGP_INCREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_INDEX_FIELD_OP             ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_INDEX_OP                   ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_LAND_OP                    ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LEQUAL_OP                  ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATER_OP                ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATEREQUAL_OP           ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESS_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESSEQUAL_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LNOT_OP                    ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_LNOTEQUAL_OP               ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LOAD_OP                    ARGP_LIST2 (ARGP_NAMESTRING, ARGP_SUPERNAME)
#define ARGP_LOAD_TABLE_OP              ARGP_LIST6 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TERMARG,  ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_LOCAL0                     ARG_NONE
#define ARGP_LOCAL1                     ARG_NONE
#define ARGP_LOCAL2                     ARG_NONE
#define ARGP_LOCAL3                     ARG_NONE
#define ARGP_LOCAL4                     ARG_NONE
#define ARGP_LOCAL5                     ARG_NONE
#define ARGP_LOCAL6                     ARG_NONE
#define ARGP_LOCAL7                     ARG_NONE
#define ARGP_LOR_OP                     ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_MATCH_OP                   ARGP_LIST6 (ARGP_TERMARG,    ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_METHOD_OP                  ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_TERMLIST)
#define ARGP_METHODCALL_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_MID_OP                     ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TARGET)
#define ARGP_MOD_OP                     ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_MULTIPLY_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_MUTEX_OP                   ARGP_LIST2 (ARGP_NAME,       ARGP_BYTEDATA)
#define ARGP_NAME_OP                    ARGP_LIST2 (ARGP_NAME,       ARGP_DATAOBJ)
#define ARGP_NAMEDFIELD_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_NAMEPATH_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_NOOP_OP                    ARG_NONE
#define ARGP_NOTIFY_OP                  ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_ONE_OP                     ARG_NONE
#define ARGP_ONES_OP                    ARG_NONE
#define ARGP_PACKAGE_OP                 ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_BYTEDATA,      ARGP_DATAOBJLIST)
#define ARGP_POWER_RES_OP               ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_WORDDATA,  ARGP_OBJLIST)
#define ARGP_PROCESSOR_OP               ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_DWORDDATA, ARGP_BYTEDATA,  ARGP_OBJLIST)
#define ARGP_QWORD_OP                   ARGP_LIST1 (ARGP_QWORDDATA)
#define ARGP_REF_OF_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_REGION_OP                  ARGP_LIST4 (ARGP_NAME,       ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_RELEASE_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_RESERVEDFIELD_OP           ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_RESET_OP                   ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_RETURN_OP                  ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_REVISION_OP                ARG_NONE
#define ARGP_SCOPE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_TERMLIST)
#define ARGP_SHIFT_LEFT_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SHIFT_RIGHT_OP             ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SIGNAL_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_SIZE_OF_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_SLEEP_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_STALL_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_STATICSTRING_OP            ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_STORE_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_SUPERNAME)
#define ARGP_STRING_OP                  ARGP_LIST1 (ARGP_CHARLIST)
#define ARGP_SUBTRACT_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_THERMAL_ZONE_OP            ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_TO_BCD_OP                  ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_BUFFER_OP               ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_DEC_STR_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_HEX_STR_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_INTEGER_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_STRING_OP               ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_TYPE_OP                    ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_UNLOAD_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_VAR_PACKAGE_OP             ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_BYTEDATA,      ARGP_DATAOBJLIST)
#define ARGP_WAIT_OP                    ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_WHILE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_TERMLIST)
#define ARGP_WORD_OP                    ARGP_LIST1 (ARGP_WORDDATA)
#define ARGP_ZERO_OP                    ARG_NONE


/*
 * All AML opcodes and the runtime arguments for each.  Used by the AML interpreter  Each list is compressed
 * into a 32-bit number and stored in the master opcode table at the end of this file.
 *
 * (Used by Prep_operands procedure and the ASL Compiler)
 */


#define ARGI_ACCESSFIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_ACQUIRE_OP                 ARGI_LIST2 (ARGI_MUTEX,      ARGI_INTEGER)
#define ARGI_ADD_OP                     ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_ALIAS_OP                   ARGI_INVALID_OPCODE
#define ARGI_ARG0                       ARG_NONE
#define ARGI_ARG1                       ARG_NONE
#define ARGI_ARG2                       ARG_NONE
#define ARGI_ARG3                       ARG_NONE
#define ARGI_ARG4                       ARG_NONE
#define ARGI_ARG5                       ARG_NONE
#define ARGI_ARG6                       ARG_NONE
#define ARGI_BANK_FIELD_OP              ARGI_INVALID_OPCODE
#define ARGI_BIT_AND_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NAND_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NOR_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NOT_OP                 ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_BIT_OR_OP                  ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_XOR_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BREAK_OP                   ARG_NONE
#define ARGI_BREAK_POINT_OP             ARG_NONE
#define ARGI_BUFFER_OP                  ARGI_INVALID_OPCODE
#define ARGI_BYTE_OP                    ARGI_INVALID_OPCODE
#define ARGI_BYTELIST_OP                ARGI_INVALID_OPCODE
#define ARGI_CONCAT_OP                  ARGI_LIST3 (ARGI_COMPUTEDATA,ARGI_COMPUTEDATA,   ARGI_TARGETREF)
#define ARGI_CONCAT_RES_OP              ARGI_LIST3 (ARGI_BUFFER,     ARGI_BUFFER,        ARGI_TARGETREF)
#define ARGI_COND_REF_OF_OP             ARGI_LIST2 (ARGI_OBJECT_REF, ARGI_TARGETREF)
#define ARGI_CONTINUE_OP                ARGI_INVALID_OPCODE
#define ARGI_COPY_OP                    ARGI_LIST2 (ARGI_ANYTYPE,    ARGI_SIMPLE_TARGET)
#define ARGI_CREATE_BIT_FIELD_OP        ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_BYTE_FIELD_OP       ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_DWORD_FIELD_OP      ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_FIELD_OP            ARGI_LIST4 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_INTEGER,      ARGI_REFERENCE)
#define ARGI_CREATE_QWORD_FIELD_OP      ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_WORD_FIELD_OP       ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_DATA_REGION_OP             ARGI_LIST3 (ARGI_STRING,     ARGI_STRING,       ARGI_STRING)
#define ARGI_DEBUG_OP                   ARG_NONE
#define ARGI_DECREMENT_OP               ARGI_LIST1 (ARGI_INTEGER_REF)
#define ARGI_DEREF_OF_OP                ARGI_LIST1 (ARGI_REFERENCE)
#define ARGI_DEVICE_OP                  ARGI_INVALID_OPCODE
#define ARGI_DIVIDE_OP                  ARGI_LIST4 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF,    ARGI_TARGETREF)
#define ARGI_DWORD_OP                   ARGI_INVALID_OPCODE
#define ARGI_ELSE_OP                    ARGI_INVALID_OPCODE
#define ARGI_EVENT_OP                   ARGI_INVALID_OPCODE
#define ARGI_FATAL_OP                   ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_INTEGER)
#define ARGI_FIELD_OP                   ARGI_INVALID_OPCODE
#define ARGI_FIND_SET_LEFT_BIT_OP       ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_FIND_SET_RIGHT_BIT_OP      ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_FROM_BCD_OP                ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_IF_OP                      ARGI_INVALID_OPCODE
#define ARGI_INCREMENT_OP               ARGI_LIST1 (ARGI_INTEGER_REF)
#define ARGI_INDEX_FIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_INDEX_OP                   ARGI_LIST3 (ARGI_COMPLEXOBJ, ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_LAND_OP                    ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_LEQUAL_OP                  ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_LGREATER_OP                ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_LGREATEREQUAL_OP           ARGI_INVALID_OPCODE
#define ARGI_LLESS_OP                   ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_LLESSEQUAL_OP              ARGI_INVALID_OPCODE
#define ARGI_LNOT_OP                    ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_LNOTEQUAL_OP               ARGI_INVALID_OPCODE
#define ARGI_LOAD_OP                    ARGI_LIST2 (ARGI_REGION,     ARGI_TARGETREF)
#define ARGI_LOAD_TABLE_OP              ARGI_LIST6 (ARGI_STRING,     ARGI_STRING,        ARGI_STRING,       ARGI_STRING,    ARGI_STRING, ARGI_TARGETREF)
#define ARGI_LOCAL0                     ARG_NONE
#define ARGI_LOCAL1                     ARG_NONE
#define ARGI_LOCAL2                     ARG_NONE
#define ARGI_LOCAL3                     ARG_NONE
#define ARGI_LOCAL4                     ARG_NONE
#define ARGI_LOCAL5                     ARG_NONE
#define ARGI_LOCAL6                     ARG_NONE
#define ARGI_LOCAL7                     ARG_NONE
#define ARGI_LOR_OP                     ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_MATCH_OP                   ARGI_LIST6 (ARGI_PACKAGE,    ARGI_INTEGER,       ARGI_INTEGER,      ARGI_INTEGER,   ARGI_INTEGER,   ARGI_INTEGER)
#define ARGI_METHOD_OP                  ARGI_INVALID_OPCODE
#define ARGI_METHODCALL_OP              ARGI_INVALID_OPCODE
#define ARGI_MID_OP                     ARGI_LIST4 (ARGI_BUFFERSTRING,ARGI_INTEGER,      ARGI_INTEGER,      ARGI_TARGETREF)
#define ARGI_MOD_OP                     ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_MULTIPLY_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_MUTEX_OP                   ARGI_INVALID_OPCODE
#define ARGI_NAME_OP                    ARGI_INVALID_OPCODE
#define ARGI_NAMEDFIELD_OP              ARGI_INVALID_OPCODE
#define ARGI_NAMEPATH_OP                ARGI_INVALID_OPCODE
#define ARGI_NOOP_OP                    ARG_NONE
#define ARGI_NOTIFY_OP                  ARGI_LIST2 (ARGI_DEVICE_REF, ARGI_INTEGER)
#define ARGI_ONE_OP                     ARG_NONE
#define ARGI_ONES_OP                    ARG_NONE
#define ARGI_PACKAGE_OP                 ARGI_INVALID_OPCODE
#define ARGI_POWER_RES_OP               ARGI_INVALID_OPCODE
#define ARGI_PROCESSOR_OP               ARGI_INVALID_OPCODE
#define ARGI_QWORD_OP                   ARGI_INVALID_OPCODE
#define ARGI_REF_OF_OP                  ARGI_LIST1 (ARGI_OBJECT_REF)
#define ARGI_REGION_OP                  ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_RELEASE_OP                 ARGI_LIST1 (ARGI_MUTEX)
#define ARGI_RESERVEDFIELD_OP           ARGI_INVALID_OPCODE
#define ARGI_RESET_OP                   ARGI_LIST1 (ARGI_EVENT)
#define ARGI_RETURN_OP                  ARGI_INVALID_OPCODE
#define ARGI_REVISION_OP                ARG_NONE
#define ARGI_SCOPE_OP                   ARGI_INVALID_OPCODE
#define ARGI_SHIFT_LEFT_OP              ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_SHIFT_RIGHT_OP             ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_SIGNAL_OP                  ARGI_LIST1 (ARGI_EVENT)
#define ARGI_SIZE_OF_OP                 ARGI_LIST1 (ARGI_DATAOBJECT)
#define ARGI_SLEEP_OP                   ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_STALL_OP                   ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_STATICSTRING_OP            ARGI_INVALID_OPCODE
#define ARGI_STORE_OP                   ARGI_LIST2 (ARGI_ANYTYPE,    ARGI_TARGETREF)
#define ARGI_STRING_OP                  ARGI_INVALID_OPCODE
#define ARGI_SUBTRACT_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_THERMAL_ZONE_OP            ARGI_INVALID_OPCODE
#define ARGI_TO_BCD_OP                  ARGI_LIST2 (ARGI_INTEGER,    ARGI_FIXED_TARGET)
#define ARGI_TO_BUFFER_OP               ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_DEC_STR_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_HEX_STR_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_INTEGER_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_STRING_OP               ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_FIXED_TARGET)
#define ARGI_TYPE_OP                    ARGI_LIST1 (ARGI_ANYTYPE)
#define ARGI_UNLOAD_OP                  ARGI_LIST1 (ARGI_DDBHANDLE)
#define ARGI_VAR_PACKAGE_OP             ARGI_INVALID_OPCODE
#define ARGI_WAIT_OP                    ARGI_LIST2 (ARGI_EVENT,      ARGI_INTEGER)
#define ARGI_WHILE_OP                   ARGI_INVALID_OPCODE
#define ARGI_WORD_OP                    ARGI_INVALID_OPCODE
#define ARGI_ZERO_OP                    ARG_NONE


/*
 * Summary of opcode types/flags
 */

/******************************************************************************

 Opcodes that have associated namespace objects

	AML_SCOPE_OP
	AML_DEVICE_OP
	AML_THERMAL_ZONE_OP
	AML_METHOD_OP
	AML_POWER_RES_OP
	AML_PROCESSOR_OP
	AML_FIELD_OP
	AML_INDEX_FIELD_OP
	AML_BANK_FIELD_OP
	AML_NAME_OP
	AML_ALIAS_OP
	AML_MUTEX_OP
	AML_EVENT_OP
	AML_REGION_OP
	AML_CREATE_FIELD_OP
	AML_CREATE_BIT_FIELD_OP
	AML_CREATE_BYTE_FIELD_OP
	AML_CREATE_WORD_FIELD_OP
	AML_CREATE_DWORD_FIELD_OP
	AML_CREATE_QWORD_FIELD_OP
	AML_INT_NAMEDFIELD_OP
	AML_INT_METHODCALL_OP
	AML_INT_NAMEPATH_OP

  Opcodes that are "namespace" opcodes

	AML_SCOPE_OP
	AML_DEVICE_OP
	AML_THERMAL_ZONE_OP
	AML_METHOD_OP
	AML_POWER_RES_OP
	AML_PROCESSOR_OP
	AML_FIELD_OP
	AML_INDEX_FIELD_OP
	AML_BANK_FIELD_OP
	AML_NAME_OP
	AML_ALIAS_OP
	AML_MUTEX_OP
	AML_EVENT_OP
	AML_REGION_OP
	AML_INT_NAMEDFIELD_OP

  Opcodes that have an associated namespace node

	AML_SCOPE_OP
	AML_DEVICE_OP
	AML_THERMAL_ZONE_OP
	AML_METHOD_OP
	AML_POWER_RES_OP
	AML_PROCESSOR_OP
	AML_NAME_OP
	AML_ALIAS_OP
	AML_MUTEX_OP
	AML_EVENT_OP
	AML_REGION_OP
	AML_CREATE_FIELD_OP
	AML_CREATE_BIT_FIELD_OP
	AML_CREATE_BYTE_FIELD_OP
	AML_CREATE_WORD_FIELD_OP
	AML_CREATE_DWORD_FIELD_OP
	AML_CREATE_QWORD_FIELD_OP
	AML_INT_NAMEDFIELD_OP
	AML_INT_METHODCALL_OP
	AML_INT_NAMEPATH_OP

  Opcodes that define named ACPI objects

	AML_SCOPE_OP
	AML_DEVICE_OP
	AML_THERMAL_ZONE_OP
	AML_METHOD_OP
	AML_POWER_RES_OP
	AML_PROCESSOR_OP
	AML_NAME_OP
	AML_ALIAS_OP
	AML_MUTEX_OP
	AML_EVENT_OP
	AML_REGION_OP
	AML_INT_NAMEDFIELD_OP

	Opcodes that contain executable AML as part of the definition that
	must be deferred until needed

	AML_METHOD_OP
	AML_VAR_PACKAGE_OP
	AML_CREATE_FIELD_OP
	AML_CREATE_BIT_FIELD_OP
	AML_CREATE_BYTE_FIELD_OP
	AML_CREATE_WORD_FIELD_OP
	AML_CREATE_DWORD_FIELD_OP
	AML_CREATE_QWORD_FIELD_OP
	AML_REGION_OP

  Field opcodes

	AML_CREATE_FIELD_OP
	AML_FIELD_OP
	AML_INDEX_FIELD_OP
	AML_BANK_FIELD_OP

  Field "Create" opcodes

	AML_CREATE_FIELD_OP
	AML_CREATE_BIT_FIELD_OP
	AML_CREATE_BYTE_FIELD_OP
	AML_CREATE_WORD_FIELD_OP
	AML_CREATE_DWORD_FIELD_OP
	AML_CREATE_QWORD_FIELD_OP

******************************************************************************/


/*
 * Master Opcode information table.  A summary of everything we know about each opcode, all in one place.
 */


static const acpi_opcode_info    aml_op_info[] =
{
/* Index           Name                 Parser Args               Interpreter Args                 Class                      Type                  Flags */

/* 00 */ ACPI_OP ("Zero",               ARGP_ZERO_OP,              ARGI_ZERO_OP,               AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 01 */ ACPI_OP ("One",                ARGP_ONE_OP,               ARGI_ONE_OP,                AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 02 */ ACPI_OP ("Alias",              ARGP_ALIAS_OP,             ARGI_ALIAS_OP,              AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 03 */ ACPI_OP ("Name",               ARGP_NAME_OP,              ARGI_NAME_OP,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 04 */ ACPI_OP ("Byte_const",         ARGP_BYTE_OP,              ARGI_BYTE_OP,               AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 05 */ ACPI_OP ("Word_const",         ARGP_WORD_OP,              ARGI_WORD_OP,               AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 06 */ ACPI_OP ("Dword_const",        ARGP_DWORD_OP,             ARGI_DWORD_OP,              AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 07 */ ACPI_OP ("String",             ARGP_STRING_OP,            ARGI_STRING_OP,             AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 08 */ ACPI_OP ("Scope",              ARGP_SCOPE_OP,             ARGI_SCOPE_OP,              AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 09 */ ACPI_OP ("Buffer",             ARGP_BUFFER_OP,            ARGI_BUFFER_OP,             AML_CLASS_ARGUMENT,        AML_TYPE_DATA_TERM,       AML_HAS_ARGS),
/* 0A */ ACPI_OP ("Package",            ARGP_PACKAGE_OP,           ARGI_PACKAGE_OP,            AML_CLASS_ARGUMENT,        AML_TYPE_DATA_TERM,       AML_HAS_ARGS),
/* 0B */ ACPI_OP ("Method",             ARGP_METHOD_OP,            ARGI_METHOD_OP,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED | AML_DEFER),
/* 0C */ ACPI_OP ("Local0",             ARGP_LOCAL0,               ARGI_LOCAL0,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0D */ ACPI_OP ("Local1",             ARGP_LOCAL1,               ARGI_LOCAL1,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0E */ ACPI_OP ("Local2",             ARGP_LOCAL2,               ARGI_LOCAL2,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0F */ ACPI_OP ("Local3",             ARGP_LOCAL3,               ARGI_LOCAL3,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 10 */ ACPI_OP ("Local4",             ARGP_LOCAL4,               ARGI_LOCAL4,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 11 */ ACPI_OP ("Local5",             ARGP_LOCAL5,               ARGI_LOCAL5,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 12 */ ACPI_OP ("Local6",             ARGP_LOCAL6,               ARGI_LOCAL6,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 13 */ ACPI_OP ("Local7",             ARGP_LOCAL7,               ARGI_LOCAL7,                AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 14 */ ACPI_OP ("Arg0",               ARGP_ARG0,                 ARGI_ARG0,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 15 */ ACPI_OP ("Arg1",               ARGP_ARG1,                 ARGI_ARG1,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 16 */ ACPI_OP ("Arg2",               ARGP_ARG2,                 ARGI_ARG2,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 17 */ ACPI_OP ("Arg3",               ARGP_ARG3,                 ARGI_ARG3,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 18 */ ACPI_OP ("Arg4",               ARGP_ARG4,                 ARGI_ARG4,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 19 */ ACPI_OP ("Arg5",               ARGP_ARG5,                 ARGI_ARG5,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 1_a */ ACPI_OP ("Arg6",              ARGP_ARG6,                 ARGI_ARG6,                  AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 1_b */ ACPI_OP ("Store",             ARGP_STORE_OP,             ARGI_STORE_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 1_c */ ACPI_OP ("Ref_of",            ARGP_REF_OF_OP,            ARGI_REF_OF_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 1_d */ ACPI_OP ("Add",               ARGP_ADD_OP,               ARGI_ADD_OP,                AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 1_e */ ACPI_OP ("Concatenate",       ARGP_CONCAT_OP,            ARGI_CONCAT_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 1_f */ ACPI_OP ("Subtract",          ARGP_SUBTRACT_OP,          ARGI_SUBTRACT_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 20 */ ACPI_OP ("Increment",          ARGP_INCREMENT_OP,         ARGI_INCREMENT_OP,          AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 21 */ ACPI_OP ("Decrement",          ARGP_DECREMENT_OP,         ARGI_DECREMENT_OP,          AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 22 */ ACPI_OP ("Multiply",           ARGP_MULTIPLY_OP,          ARGI_MULTIPLY_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 23 */ ACPI_OP ("Divide",             ARGP_DIVIDE_OP,            ARGI_DIVIDE_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_2T_1R,   AML_FLAGS_EXEC_2A_2T_1R),
/* 24 */ ACPI_OP ("Shift_left",         ARGP_SHIFT_LEFT_OP,        ARGI_SHIFT_LEFT_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 25 */ ACPI_OP ("Shift_right",        ARGP_SHIFT_RIGHT_OP,       ARGI_SHIFT_RIGHT_OP,        AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 26 */ ACPI_OP ("And",                ARGP_BIT_AND_OP,           ARGI_BIT_AND_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 27 */ ACPI_OP ("NAnd",               ARGP_BIT_NAND_OP,          ARGI_BIT_NAND_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 28 */ ACPI_OP ("Or",                 ARGP_BIT_OR_OP,            ARGI_BIT_OR_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 29 */ ACPI_OP ("NOr",                ARGP_BIT_NOR_OP,           ARGI_BIT_NOR_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 2_a */ ACPI_OP ("XOr",               ARGP_BIT_XOR_OP,           ARGI_BIT_XOR_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH),
/* 2_b */ ACPI_OP ("Not",               ARGP_BIT_NOT_OP,           ARGI_BIT_NOT_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 2_c */ ACPI_OP ("Find_set_left_bit", ARGP_FIND_SET_LEFT_BIT_OP, ARGI_FIND_SET_LEFT_BIT_OP,  AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 2_d */ ACPI_OP ("Find_set_right_bit", ARGP_FIND_SET_RIGHT_BIT_OP,ARGI_FIND_SET_RIGHT_BIT_OP, AML_CLASS_EXECUTE,        AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 2_e */ ACPI_OP ("Deref_of",          ARGP_DEREF_OF_OP,          ARGI_DEREF_OF_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 2_f */ ACPI_OP ("Notify",            ARGP_NOTIFY_OP,            ARGI_NOTIFY_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_0R,   AML_FLAGS_EXEC_2A_0T_0R),
/* 30 */ ACPI_OP ("Size_of",            ARGP_SIZE_OF_OP,           ARGI_SIZE_OF_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 31 */ ACPI_OP ("Index",              ARGP_INDEX_OP,             ARGI_INDEX_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 32 */ ACPI_OP ("Match",              ARGP_MATCH_OP,             ARGI_MATCH_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_6A_0T_1R,   AML_FLAGS_EXEC_6A_0T_1R),
/* 33 */ ACPI_OP ("Create_dWord_field", ARGP_CREATE_DWORD_FIELD_OP,ARGI_CREATE_DWORD_FIELD_OP, AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 34 */ ACPI_OP ("Create_word_field",  ARGP_CREATE_WORD_FIELD_OP, ARGI_CREATE_WORD_FIELD_OP,  AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 35 */ ACPI_OP ("Create_byte_field",  ARGP_CREATE_BYTE_FIELD_OP, ARGI_CREATE_BYTE_FIELD_OP,  AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 36 */ ACPI_OP ("Create_bit_field",   ARGP_CREATE_BIT_FIELD_OP,  ARGI_CREATE_BIT_FIELD_OP,   AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 37 */ ACPI_OP ("Object_type",        ARGP_TYPE_OP,              ARGI_TYPE_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 38 */ ACPI_OP ("LAnd",               ARGP_LAND_OP,              ARGI_LAND_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL),
/* 39 */ ACPI_OP ("LOr",                ARGP_LOR_OP,               ARGI_LOR_OP,                AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL),
/* 3_a */ ACPI_OP ("LNot",              ARGP_LNOT_OP,              ARGI_LNOT_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 3_b */ ACPI_OP ("LEqual",            ARGP_LEQUAL_OP,            ARGI_LEQUAL_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL),
/* 3_c */ ACPI_OP ("LGreater",          ARGP_LGREATER_OP,          ARGI_LGREATER_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL),
/* 3_d */ ACPI_OP ("LLess",             ARGP_LLESS_OP,             ARGI_LLESS_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL),
/* 3_e */ ACPI_OP ("If",                ARGP_IF_OP,                ARGI_IF_OP,                 AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 3_f */ ACPI_OP ("Else",              ARGP_ELSE_OP,              ARGI_ELSE_OP,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 40 */ ACPI_OP ("While",              ARGP_WHILE_OP,             ARGI_WHILE_OP,              AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 41 */ ACPI_OP ("Noop",               ARGP_NOOP_OP,              ARGI_NOOP_OP,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 42 */ ACPI_OP ("Return",             ARGP_RETURN_OP,            ARGI_RETURN_OP,             AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 43 */ ACPI_OP ("Break",              ARGP_BREAK_OP,             ARGI_BREAK_OP,              AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 44 */ ACPI_OP ("Break_point",        ARGP_BREAK_POINT_OP,       ARGI_BREAK_POINT_OP,        AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 45 */ ACPI_OP ("Ones",               ARGP_ONES_OP,              ARGI_ONES_OP,               AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),

/* Prefixed opcodes (Two-byte opcodes with a prefix op) */

/* 46 */ ACPI_OP ("Mutex",              ARGP_MUTEX_OP,             ARGI_MUTEX_OP,              AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 47 */ ACPI_OP ("Event",              ARGP_EVENT_OP,             ARGI_EVENT_OP,              AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED ),
/* 48 */ ACPI_OP ("Cond_ref_of",        ARGP_COND_REF_OF_OP,       ARGI_COND_REF_OF_OP,        AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 49 */ ACPI_OP ("Create_field",       ARGP_CREATE_FIELD_OP,      ARGI_CREATE_FIELD_OP,       AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_FIELD | AML_CREATE),
/* 4_a */ ACPI_OP ("Load",              ARGP_LOAD_OP,              ARGI_LOAD_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_0R,   AML_FLAGS_EXEC_1A_1T_0R),
/* 4_b */ ACPI_OP ("Stall",             ARGP_STALL_OP,             ARGI_STALL_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4_c */ ACPI_OP ("Sleep",             ARGP_SLEEP_OP,             ARGI_SLEEP_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4_d */ ACPI_OP ("Acquire",           ARGP_ACQUIRE_OP,           ARGI_ACQUIRE_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R),
/* 4_e */ ACPI_OP ("Signal",            ARGP_SIGNAL_OP,            ARGI_SIGNAL_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4_f */ ACPI_OP ("Wait",              ARGP_WAIT_OP,              ARGI_WAIT_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R),
/* 50 */ ACPI_OP ("Reset",              ARGP_RESET_OP,             ARGI_RESET_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 51 */ ACPI_OP ("Release",            ARGP_RELEASE_OP,           ARGI_RELEASE_OP,            AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 52 */ ACPI_OP ("From_bCD",           ARGP_FROM_BCD_OP,          ARGI_FROM_BCD_OP,           AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 53 */ ACPI_OP ("To_bCD",             ARGP_TO_BCD_OP,            ARGI_TO_BCD_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 54 */ ACPI_OP ("Unload",             ARGP_UNLOAD_OP,            ARGI_UNLOAD_OP,             AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 55 */ ACPI_OP ("Revision",           ARGP_REVISION_OP,          ARGI_REVISION_OP,           AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 56 */ ACPI_OP ("Debug",              ARGP_DEBUG_OP,             ARGI_DEBUG_OP,              AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 57 */ ACPI_OP ("Fatal",              ARGP_FATAL_OP,             ARGI_FATAL_OP,              AML_CLASS_EXECUTE,         AML_TYPE_EXEC_3A_0T_0R,   AML_FLAGS_EXEC_3A_0T_0R),
/* 58 */ ACPI_OP ("Op_region",          ARGP_REGION_OP,            ARGI_REGION_OP,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED | AML_DEFER),
/* 59 */ ACPI_OP ("Field",              ARGP_FIELD_OP,             ARGI_FIELD_OP,              AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD),
/* 5_a */ ACPI_OP ("Device",            ARGP_DEVICE_OP,            ARGI_DEVICE_OP,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5_b */ ACPI_OP ("Processor",         ARGP_PROCESSOR_OP,         ARGI_PROCESSOR_OP,          AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5_c */ ACPI_OP ("Power_resource",    ARGP_POWER_RES_OP,         ARGI_POWER_RES_OP,          AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5_d */ ACPI_OP ("Thermal_zone",      ARGP_THERMAL_ZONE_OP,      ARGI_THERMAL_ZONE_OP,       AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5_e */ ACPI_OP ("Index_field",       ARGP_INDEX_FIELD_OP,       ARGI_INDEX_FIELD_OP,        AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD),
/* 5_f */ ACPI_OP ("Bank_field",        ARGP_BANK_FIELD_OP,        ARGI_BANK_FIELD_OP,         AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD),

/* Internal opcodes that map to invalid AML opcodes */

/* 60 */ ACPI_OP ("LNot_equal",         ARGP_LNOTEQUAL_OP,         ARGI_LNOTEQUAL_OP,          AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 61 */ ACPI_OP ("LLess_equal",        ARGP_LLESSEQUAL_OP,        ARGI_LLESSEQUAL_OP,         AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 62 */ ACPI_OP ("LGreater_equal",     ARGP_LGREATEREQUAL_OP,     ARGI_LGREATEREQUAL_OP,      AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 63 */ ACPI_OP ("[Name_path]",        ARGP_NAMEPATH_OP,          ARGI_NAMEPATH_OP,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_NSOBJECT | AML_NSNODE ),
/* 64 */ ACPI_OP ("[Method_call]",      ARGP_METHODCALL_OP,        ARGI_METHODCALL_OP,         AML_CLASS_METHOD_CALL,     AML_TYPE_METHOD_CALL,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE),
/* 65 */ ACPI_OP ("[Byte_list]",        ARGP_BYTELIST_OP,          ARGI_BYTELIST_OP,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 66 */ ACPI_OP ("[Reserved_field]",   ARGP_RESERVEDFIELD_OP,     ARGI_RESERVEDFIELD_OP,      AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 67 */ ACPI_OP ("[Named_field]",      ARGP_NAMEDFIELD_OP,        ARGI_NAMEDFIELD_OP,         AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED ),
/* 68 */ ACPI_OP ("[Access_field]",     ARGP_ACCESSFIELD_OP,       ARGI_ACCESSFIELD_OP,        AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 69 */ ACPI_OP ("[Static_string",     ARGP_STATICSTRING_OP,      ARGI_STATICSTRING_OP,       AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 6_a */ ACPI_OP ("[Return Value]",    ARG_NONE,                  ARG_NONE,                   AML_CLASS_RETURN_VALUE,    AML_TYPE_RETURN,          AML_HAS_ARGS | AML_HAS_RETVAL),
/* 6_b */ ACPI_OP ("UNKNOWN_OP!",       ARG_NONE,                  ARG_NONE,                   AML_CLASS_UNKNOWN,         AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 6_c */ ACPI_OP ("ASCII_ONLY!",       ARG_NONE,                  ARG_NONE,                   AML_CLASS_ASCII,           AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 6_d */ ACPI_OP ("PREFIX_ONLY!",      ARG_NONE,                  ARG_NONE,                   AML_CLASS_PREFIX,          AML_TYPE_BOGUS,           AML_HAS_ARGS),


/* ACPI 2.0 opcodes */

/* 6_e */ ACPI_OP ("Qword_const",       ARGP_QWORD_OP,             ARGI_QWORD_OP,              AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 6_f */ ACPI_OP ("Var_package",       ARGP_VAR_PACKAGE_OP,       ARGI_VAR_PACKAGE_OP,        AML_CLASS_ARGUMENT,        AML_TYPE_DATA_TERM,       AML_HAS_ARGS | AML_DEFER),
/* 70 */ ACPI_OP ("Concat_res",         ARGP_CONCAT_RES_OP,        ARGI_CONCAT_RES_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 71 */ ACPI_OP ("Mod",                ARGP_MOD_OP,               ARGI_MOD_OP,                AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 72 */ ACPI_OP ("Create_qWord_field", ARGP_CREATE_QWORD_FIELD_OP,ARGI_CREATE_QWORD_FIELD_OP, AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 73 */ ACPI_OP ("To_buffer",          ARGP_TO_BUFFER_OP,         ARGI_TO_BUFFER_OP,          AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 74 */ ACPI_OP ("To_decimal_string",  ARGP_TO_DEC_STR_OP,        ARGI_TO_DEC_STR_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 75 */ ACPI_OP ("To_hex_string",      ARGP_TO_HEX_STR_OP,        ARGI_TO_HEX_STR_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 76 */ ACPI_OP ("To_integer",         ARGP_TO_INTEGER_OP,        ARGI_TO_INTEGER_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 77 */ ACPI_OP ("To_string",          ARGP_TO_STRING_OP,         ARGI_TO_STRING_OP,          AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 78 */ ACPI_OP ("Copy_object",        ARGP_COPY_OP,              ARGI_COPY_OP,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 79 */ ACPI_OP ("Mid",                ARGP_MID_OP,               ARGI_MID_OP,                AML_CLASS_EXECUTE,         AML_TYPE_EXEC_3A_1T_1R,   AML_FLAGS_EXEC_3A_1T_1R),
/* 7_a */ ACPI_OP ("Continue",          ARGP_CONTINUE_OP,          ARGI_CONTINUE_OP,           AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 7_b */ ACPI_OP ("Load_table",        ARGP_LOAD_TABLE_OP,        ARGI_LOAD_TABLE_OP,         AML_CLASS_EXECUTE,         AML_TYPE_EXEC_6A_0T_1R,   AML_FLAGS_EXEC_6A_0T_1R),
/* 7_c */ ACPI_OP ("Data_op_region",    ARGP_DATA_REGION_OP,       ARGI_DATA_REGION_OP,        AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),

};

/*
 * This table is directly indexed by the opcodes, and returns an
 * index into the table above
 */

static const u8 aml_short_op_info_index[256] =
{
/*              0     1     2     3     4     5     6     7  */
/*              8     9     A     B     C     D     E     F  */
/* 0x00 */	0x00, 0x01, _UNK, _UNK, _UNK, _UNK, 0x02, _UNK,
/* 0x08 */	0x03, _UNK, 0x04, 0x05, 0x06, 0x07, 0x6E, _UNK,
/* 0x10 */	0x08, 0x09, 0x0a, 0x6F, 0x0b, _UNK, _UNK, _UNK,
/* 0x18 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x20 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x28 */	_UNK, _UNK, _UNK, _UNK, _UNK, 0x63, _PFX, _PFX,
/* 0x30 */	0x67, 0x66, 0x68, 0x65, 0x69, 0x64, 0x6A, _UNK,
/* 0x38 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x40 */	_UNK, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x48 */	_ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x50 */	_ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x58 */	_ASC, _ASC, _ASC, _UNK, _PFX, _UNK, _PFX, _ASC,
/* 0x60 */	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
/* 0x68 */	0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, _UNK,
/* 0x70 */	0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22,
/* 0x78 */	0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
/* 0x80 */	0x2b, 0x2c, 0x2d, 0x2e, 0x70, 0x71, 0x2f, 0x30,
/* 0x88 */	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x72,
/* 0x90 */	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x73, 0x74,
/* 0x98 */	0x75, 0x76, _UNK, _UNK, 0x77, 0x78, 0x79, 0x7A,
/* 0xA0 */	0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x60, 0x61,
/* 0xA8 */	0x62, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB0 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB8 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC0 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC8 */	_UNK, _UNK, _UNK, _UNK, 0x44, _UNK, _UNK, _UNK,
/* 0xD0 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xD8 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE0 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE8 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF0 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF8 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, 0x45,
};


static const u8 aml_long_op_info_index[NUM_EXTENDED_OPCODE] =
{
/*              0     1     2     3     4     5     6     7  */
/*              8     9     A     B     C     D     E     F  */
/* 0x00 */	_UNK, 0x46, 0x47, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x08 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x10 */	_UNK, _UNK, 0x48, 0x49, _UNK, _UNK, _UNK, _UNK,
/* 0x18 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, 0x7B,
/* 0x20 */	0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
/* 0x28 */	0x52, 0x53, 0x54, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x30 */	0x55, 0x56, 0x57, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x38 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x40 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x48 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x50 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x58 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x60 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x68 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x70 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x78 */	_UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x80 */	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
/* 0x88 */	0x7C,
};


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_opcode_info
 *
 * PARAMETERS:  Opcode              - The AML opcode
 *
 * RETURN:      A pointer to the info about the opcode.  NULL if the opcode was
 *              not found in the table.
 *
 * DESCRIPTION: Find AML opcode description based on the opcode.
 *              NOTE: This procedure must ALWAYS return a valid pointer!
 *
 ******************************************************************************/

const acpi_opcode_info *
acpi_ps_get_opcode_info (
	u16                     opcode)
{
	const acpi_opcode_info  *op_info;
	u8                      upper_opcode;
	u8                      lower_opcode;


	PROC_NAME ("Ps_get_opcode_info");


	/* Split the 16-bit opcode into separate bytes */

	upper_opcode = (u8) (opcode >> 8);
	lower_opcode = (u8) opcode;

	/* Default is "unknown opcode" */

	op_info = &aml_op_info [_UNK];


	/*
	 * Detect normal 8-bit opcode or extended 16-bit opcode
	 */

	switch (upper_opcode) {
	case 0:

		/* Simple (8-bit) opcode: 0-255, can't index beyond table  */

		op_info = &aml_op_info [aml_short_op_info_index [lower_opcode]];
		break;


	case AML_EXTOP:

		/* Extended (16-bit, prefix+opcode) opcode */

		if (lower_opcode <= MAX_EXTENDED_OPCODE) {
			op_info = &aml_op_info [aml_long_op_info_index [lower_opcode]];
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown extended opcode=%X\n", opcode));
		break;
	}


	/* Get the Op info pointer for this opcode */

	return (op_info);
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

NATIVE_CHAR *
acpi_ps_get_opcode_name (
	u16                     opcode)
{
	const acpi_opcode_info  *op;


	op = acpi_ps_get_opcode_info (opcode);

	/* Always guaranteed to return a valid pointer */

#ifdef ACPI_DEBUG
	return (op->name);
#else
	return ("AE_NOT_CONFIGURED");
#endif
}


