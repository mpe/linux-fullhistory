
/******************************************************************************
 *
 * Name: config.h - Global configuration constants
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

#ifndef _CONFIG_H
#define _CONFIG_H


/******************************************************************************
 *
 * Compile-time options
 *
 *****************************************************************************/

/*
 * ACPI_DEBUG           - This switch enables all the debug facilities of the ACPI
 *                          subsystem.  This includes the DEBUG_PRINT output statements
 *                          When disabled, all DEBUG_PRINT statements are compiled out.
 *
 * ACPI_APPLICATION     - Use this switch if the subsystem is going to be run
 *                          at the application level.
 *
 */


/******************************************************************************
 *
 * Subsystem Constants
 *
 *****************************************************************************/


/* Version string */

#define ACPI_CA_VERSION             __DATE__

/* Name of host operating system (returned by the _OS_ namespace object) */

#ifdef _LINUX
#define ACPI_OS_NAME                "Linux"
#else
#define ACPI_OS_NAME                "Intel ACPI/CA Core Subsystem"
#endif


/*
 * How and when control methods will be parsed
 * The default action is to parse all methods at table load time to verify them, but delete the parse trees
 * to conserve memory.  Methods are parsed just in time before execution and the parse tree is deleted
 * when execution completes.
 */
#define METHOD_PARSE_AT_INIT        0x0     /* Parse at table init, never delete the method parse tree */
#define METHOD_PARSE_JUST_IN_TIME   0x1     /* Parse only when a method is invoked */
#define METHOD_DELETE_AT_COMPLETION 0x2     /* Delete parse tree on method completion */

/* Default parsing configuration */

#define METHOD_PARSE_CONFIGURATION  (METHOD_PARSE_JUST_IN_TIME | METHOD_DELETE_AT_COMPLETION)


/* Maximum objects in the various object caches */

#define MAX_STATE_CACHE_DEPTH       24          /* State objects for stacks */
#define MAX_PARSE_CACHE_DEPTH       512         /* Parse tree objects */
#define MAX_OBJECT_CACHE_DEPTH      32          /* Interpreter operand objects */
#define MAX_WALK_CACHE_DEPTH        2           /* Objects for parse tree walks (method execution) */

/*
 * Name_space Table size
 *
 * All tables are the same size to simplify the implementation.
 * Tables may be extended by allocating additional tables that
 * are in turn linked together to form a chain of tables.
 */

#define NS_TABLE_SIZE               16

/* String size constants */

#define MAX_STRING_LENGTH           512
#define PATHNAME_MAX                256     /* A full namespace pathname */


/* Maximum count for a semaphore object */

#define MAX_SEMAPHORE_COUNT         256


/* Max reference count (for debug only) */

#define MAX_REFERENCE_COUNT         0x200


/* Size of cached memory mapping for system memory operation region */

#define SYSMEM_REGION_WINDOW_SIZE   4096


/*
 * Debugger threading model
 * Use single threaded if the entire subsystem is contained in an application
 * Use multiple threaded when the the subsystem is running in the kernel.
 *
 * By default the model is single threaded if ACPI_APPLICATION is set,
 * multi-threaded if ACPI_APPLICATION is not set.
 */

#define DEBUGGER_SINGLE_THREADED    0
#define DEBUGGER_MULTI_THREADED     1

#ifdef ACPI_APPLICATION
#define DEBUGGER_THREADING          DEBUGGER_SINGLE_THREADED

#else
#define DEBUGGER_THREADING          DEBUGGER_MULTI_THREADED
#endif


/******************************************************************************
 *
 * ACPI Specification constants (Do not change unless the specification changes)
 *
 *****************************************************************************/

/*
 * Method info (in WALK_STATE), containing local variables and argumetns
 */

#define MTH_NUM_LOCALS              8
#define MTH_MAX_LOCAL               7

#define MTH_NUM_ARGS                7
#define MTH_MAX_ARG                 6

/*
 * Operand Stack (in WALK_STATE), Must be large enough to contain MTH_MAX_ARG
 */

#define OBJ_NUM_OPERANDS            8
#define OBJ_MAX_OPERAND             7

/* Names within the namespace are 4 bytes long */

#define ACPI_NAME_SIZE              4
#define PATH_SEGMENT_LENGTH         5       /* 4 chars for name + 1 char for separator */
#define PATH_SEPARATOR              '.'


/* Constants used in searching for the RSDP in low memory */

#define LO_RSDP_WINDOW_BASE         (void *) 0
#define HI_RSDP_WINDOW_BASE         (void *) 0xE0000
#define LO_RSDP_WINDOW_SIZE         0x400
#define HI_RSDP_WINDOW_SIZE         0x20000
#define RSDP_SCAN_STEP              16


/* Maximum nesting of package objects */

#define MAX_PACKAGE_DEPTH           16


#endif /* _CONFIG_H */

