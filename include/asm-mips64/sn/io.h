/* $Id: io.h,v 1.1 2000/02/04 07:40:53 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SN_IO_H
#define _ASM_SN_IO_H

#include <asm/sn/sn0/addrs.h>

#define IO_SPACE_BASE IO_BASE

/* Because we only have PCI I/O ports.  */
#define IO_SPACE_LIMIT 0xffffffff

/* No isa_* versions, the Origin doesn't have ISA / EISA bridges.  */

#endif /* _ASM_SN_IO_H */
