/* $Id: isdn_syms.c,v 1.3 1997/02/16 01:02:47 fritz Exp $

 * Linux ISDN subsystem, exported symbols (linklevel).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: isdn_syms.c,v $
 * Revision 1.3  1997/02/16 01:02:47  fritz
 * Added GPL-Header, Id and Log
 *
 */
#include <linux/module.h>
#include <linux/version.h>

#ifndef __GENKSYMS__      /* Don't want genksyms report unneeded structs */
#include <linux/isdn.h>
#endif
#include "isdn_common.h"

#if (LINUX_VERSION_CODE < 0x020111)
static int has_exported;

static struct symbol_table isdn_syms = {
#include <linux/symtab_begin.h>
        X(register_isdn),
#include <linux/symtab_end.h>
};

void
isdn_export_syms(void)
{
	if (has_exported)
		return;
        register_symtab(&isdn_syms);
        has_exported = 1;
}

#else

EXPORT_SYMBOL(register_isdn);

#endif
