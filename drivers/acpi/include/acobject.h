
/******************************************************************************
 *
 * Name: acobject.h - Definition of ACPI_OBJECT_INTERNAL (Internal object only)
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

#ifndef _ACOBJECT_H
#define _ACOBJECT_H

#include "actypes.h"
#include "macros.h"
#include "internal.h"

/*
 * The ACPI_OBJECT_INTERNAL is used to pass AML operands from the dispatcher
 * to the interpreter, and to keep track of the various handlers such as
 * address space handlers and notify handlers.  The object is a constant
 * size in order to allow them to be cached and reused.
 *
 * All variants of the ACPI_OBJECT_INTERNAL are defined with the same
 * sequence of field types, with fields that are not used in a particular
 * variant being named "Reserved".  This is not strictly necessary, but
 * may in some circumstances simplify understanding if these structures
 * need to be displayed in a debugger having limited (or no) support for
 * union types.  It also simplifies some debug code in Dump_table() which
 * dumps multi-level values: fetching Buffer.Pointer suffices to pick up
 * the value or next level for any of several types.
 */

/******************************************************************************
 *
 * Common Descriptors
 *
 *****************************************************************************/

/*
 * Common area for all objects.
 *
 * Data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */


#define ACPI_OBJECT_COMMON_HEADER           /* Two 32-bit fields */\
	u8                      data_type;          /* To differentiate various internal objs */\
	u8                      type;               /* ACPI_OBJECT_TYPE */\
	u8                      size;               /* Size of entire descriptor */\
	u8                      flags;\
	u16                     reference_count;    /* For object deletion management */\
	u16                     acpi_cm_fill2;\
	union acpi_obj_internal *next; \

/* Defines for flag byte above */

#define AO_STATIC_ALLOCATION        0x1


/*
 * Common bitfield for the field objects
 */
#define ACPI_COMMON_FIELD_INFO              /* Three 32-bit values */\
	u32                     offset;             /* Byte offset within containing object */\
	u16                     length;             /* # of bits in buffer */ \
	u8                      granularity;\
	u8                      bit_offset;         /* Bit offset within min read/write data unit */\
	u8                      access;             /* Access_type */\
	u8                      lock_rule;\
	u8                      update_rule;\
	u8                      access_attribute;


/******************************************************************************
 *
 * Individual Object Descriptors
 *
 *****************************************************************************/


typedef struct /* COMMON */
{
	ACPI_OBJECT_COMMON_HEADER
	UCHAR   first_non_common_byte;

} ACPI_OBJECT_COMMON;


typedef struct /* NUMBER - has value */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     value;
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	void                    *reserved_p1;
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_NUMBER;


typedef struct /* STRING - has length and pointer */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     length;         /* # of bytes in string, excluding trailing null */
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	char                    *pointer;       /* String value in AML stream or in allocated space */
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_STRING;


typedef struct /* BUFFER - has length, sequence, and pointer */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     length;         /* # of bytes in buffer */
	u32                     sequence;       /* Sequential count of buffers created */
	u32                     reserved3;
	u32                     reserved4;

	u8                      *pointer;       /* points to the buffer in allocated space */
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_BUFFER;


typedef struct /* PACKAGE - has count, elements, next element */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     count;          /* # of elements in package */
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	union acpi_obj_internal **elements;     /* Array of pointers to Acpi_objects */
	union acpi_obj_internal **next_element; /* used only while initializing */
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_PACKAGE;


typedef struct /* FIELD UNIT */
{
	ACPI_OBJECT_COMMON_HEADER

	ACPI_COMMON_FIELD_INFO
	u32                     sequence;           /* Container's sequence number */

	union acpi_obj_internal *container;         /* Containing object (Buffer) */
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_FIELD_UNIT;


typedef struct /* DEVICE - has handle and notification handler/context */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     reserved1;
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	ACPI_HANDLE             handle;
	union acpi_obj_internal *sys_handler;       /* Handler for system notifies */
	union acpi_obj_internal *drv_handler;       /* Handler for driver notifies */
	union acpi_obj_internal *addr_handler;      /* Handler for Address space */
	void                    *reserved_p5;

} ACPI_OBJECT_DEVICE;


typedef struct /* EVENT */
{
	ACPI_OBJECT_COMMON_HEADER

	u16                     lock_count;
	u16                     thread_id;
	u16                     signal_count;
	u16                     fill1;
	u32                     reserved3;
	u32                     reserved4;

	void                    *semaphore;
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_EVENT;


#define INFINITE_CONCURRENCY        0xFF

typedef struct /* METHOD */
{
	ACPI_OBJECT_COMMON_HEADER

	u8                      method_flags;
	u8                      param_count;
	u8                      concurrency;
	u8                      fill1;
	u32                     pcode_length;
	u32                     table_length;
	ACPI_OWNER_ID           owning_id;
	u16                     reserved4;

	u8                      *pcode;
	u8                      *acpi_table;
	void                    *parser_op;
	void                    *semaphore;
	void                    *reserved_p5;

} ACPI_OBJECT_METHOD;


typedef struct /* MUTEX */
{
	ACPI_OBJECT_COMMON_HEADER

	u16                     lock_count;
	u16                     thread_id;
	u16                     sync_level;
	u16                     fill1;
	u32                     reserved3;
	u32                     reserved4;

	void                    *semaphore;
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_MUTEX;

/*  Flags for Region */

#define INITIAL_REGION_FLAGS        0x0000  /* value set when the region is created */
#define REGION_AGRUMENT_DATA_VALID  0x0001  /* Addr/Len are set */
#define REGION_INITIALIZED          0x0002  /* region init handler has been called */
			  /* this includes _REG method, if any */

typedef struct /* REGION */
{
	ACPI_OBJECT_COMMON_HEADER

	u16                     space_id;
	u16                     region_flags;       /* bits defined above */
	u32                     address;
	u32                     length;
	u32                     reserved4;          /* Region Specific data (PCI _ADR) */

	union acpi_obj_internal *method;            /* Associated control method */
	union acpi_obj_internal *addr_handler;      /* Handler for system notifies */
	union acpi_obj_internal *link;              /* Link in list of regions */
			   /* list is owned by Addr_handler */
	ACPI_NAMED_OBJECT      *REGmethod;          /* _REG method for this region (if any) */
	ACPI_NAMED_OBJECT      *nte;                /* containing object */

} ACPI_OBJECT_REGION;


typedef struct /* POWER RESOURCE - has Handle and notification handler/context*/
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     system_level;
	u32                     resource_order;
	u32                     reserved3;
	u32                     reserved4;

	ACPI_HANDLE             handle;
	union acpi_obj_internal *sys_handler;       /* Handler for system notifies */
	union acpi_obj_internal *drv_handler;       /* Handler for driver notifies */
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_POWER_RESOURCE;


typedef struct /* PROCESSOR - has Handle and notification handler/context*/
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     proc_id;
	ACPI_IO_ADDRESS         pblk_address;
	u16                     fill1;
	u32                     pblk_length;
	u32                     reserved4;

	ACPI_HANDLE             handle;
	union acpi_obj_internal *sys_handler;       /* Handler for system notifies */
	union acpi_obj_internal *drv_handler;       /* Handler for driver notifies */
	union acpi_obj_internal *addr_handler;      /* Handler for Address space */
	void                    *reserved_p5;

} ACPI_OBJECT_PROCESSOR;


typedef struct /* THERMAL ZONE - has Handle and Handler/Context */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     reserved1;
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	ACPI_HANDLE             handle;
	union acpi_obj_internal *sys_handler;       /* Handler for system notifies */
	union acpi_obj_internal *drv_handler;       /* Handler for driver notifies */
	union acpi_obj_internal *addr_handler;      /* Handler for Address space */
	void                    *reserved_p5;

} ACPI_OBJECT_THERMAL_ZONE;


/*
 * Internal types
 */

typedef struct /* FIELD */
{
	ACPI_OBJECT_COMMON_HEADER

	ACPI_COMMON_FIELD_INFO
	u32                     reserved4;

	union acpi_obj_internal *container;         /* Containing object */
	void                    *reserved_p2;
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_FIELD;


typedef struct /* BANK FIELD */
{
	ACPI_OBJECT_COMMON_HEADER

	ACPI_COMMON_FIELD_INFO
	u32                     value;              /* Value to store into Bank_select */

	ACPI_HANDLE             bank_select;        /* Bank select register */
	union acpi_obj_internal *container;         /* Containing object */
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_BANK_FIELD;


typedef struct /* INDEX FIELD */
{
	/*
	 * No container pointer needed since the index and data register definitions
	 * will define how to access the respective registers
	 */
	ACPI_OBJECT_COMMON_HEADER

	ACPI_COMMON_FIELD_INFO
	u32                     value;              /* Value to store into Index register */

	ACPI_HANDLE             index;              /* Index register */
	ACPI_HANDLE             data;               /* Data register */
	void                    *reserved_p3;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_INDEX_FIELD;


typedef struct /* NOTIFY HANDLER */
{
	ACPI_OBJECT_COMMON_HEADER

	u32                     reserved1;
	u32                     reserved2;
	u32                     reserved3;
	u32                     reserved4;

	ACPI_NAMED_OBJECT       *nte;               /* Parent device */
	NOTIFY_HANDLER          handler;
	void                    *context;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_NOTIFY_HANDLER;


/* Flags for address handler */

#define ADDR_HANDLER_DEFAULT_INSTALLED  0x1

typedef struct /* ADDRESS HANDLER */
{
	ACPI_OBJECT_COMMON_HEADER

	u16                     space_id;
	u16                     hflags;
	ADDRESS_SPACE_HANDLER   handler;

	ACPI_NAMED_OBJECT       *nte;               /* Parent device */
	void                    *context;
	ADDRESS_SPACE_SETUP     setup;
	union acpi_obj_internal *link;              /* Link to next handler on device */
	union acpi_obj_internal *region_list;       /* regions using this handler */

} ACPI_OBJECT_ADDR_HANDLER;


/*
 * The Reference object type is used for these opcodes:
 * Arg[0-6], Local[0-7], Index_op, Name_op, Zero_op, One_op, Ones_op, Debug_op
 */

typedef struct /* Reference - Local object type */
{
	ACPI_OBJECT_COMMON_HEADER

	u16                     op_code;
	u8                      fill1;
	u8                      target_type;        /* Used for Index_op */
	u32                     offset;             /* Used for Arg_op, Local_op, and Index_op */
	u32                     reserved3;
	u32                     reserved4;

	void                    *object;            /* Name_op=>HANDLE to obj, Index_op=>ACPI_OBJECT_INTERNAL */
	ACPI_NAMED_OBJECT       *nte;
	union acpi_obj_internal **where;
	void                    *reserved_p4;
	void                    *reserved_p5;

} ACPI_OBJECT_REFERENCE;


/******************************************************************************
 *
 * ACPI_OBJECT_INTERNAL Descriptor - a giant union of all of the above
 *
 *****************************************************************************/

typedef union acpi_obj_internal
{
	ACPI_OBJECT_COMMON          common;
	ACPI_OBJECT_NUMBER          number;
	ACPI_OBJECT_STRING          string;
	ACPI_OBJECT_BUFFER          buffer;
	ACPI_OBJECT_PACKAGE         package;
	ACPI_OBJECT_FIELD_UNIT      field_unit;
	ACPI_OBJECT_DEVICE          device;
	ACPI_OBJECT_EVENT           event;
	ACPI_OBJECT_METHOD          method;
	ACPI_OBJECT_MUTEX           mutex;
	ACPI_OBJECT_REGION          region;
	ACPI_OBJECT_POWER_RESOURCE  power_resource;
	ACPI_OBJECT_PROCESSOR       processor;
	ACPI_OBJECT_THERMAL_ZONE    thermal_zone;
	ACPI_OBJECT_FIELD           field;
	ACPI_OBJECT_BANK_FIELD      bank_field;
	ACPI_OBJECT_INDEX_FIELD     index_field;
	ACPI_OBJECT_REFERENCE       reference;
	ACPI_OBJECT_NOTIFY_HANDLER  notify_handler;
	ACPI_OBJECT_ADDR_HANDLER    addr_handler;

} ACPI_OBJECT_INTERNAL;

#endif /* _ACOBJECT_H */
