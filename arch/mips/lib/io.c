/*
 * include/asm-mips/string.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994, 1995, 1996 by Ralf Baechle
 *
 * For now io.c contains only the definition of isa_slot_offset.  The
 * real io.S doesn't assemble due to a GAS bug.
 */

/*
 * port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
unsigned long port_base;

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;
