/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/init.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/module.h>
#include "autofs_i.h"

#if LINUX_VERSION_CODE < kver(2,1,36)
#define __initfunc(X) X
#else
#include <linux/init.h>
#endif

static struct file_system_type autofs_fs_type = {
	"autofs",
	0 /* FS_NO_DCACHE doesn't work correctly */,
	autofs_read_super,
	NULL
};

#ifdef MODULE
int init_module(void)
{
	int status;
	
	if ((status = register_filesystem(&autofs_fs_type)) == 0)
		register_symtab(0);
	return status;
}

void cleanup_module(void)
{
	unregister_filesystem(&autofs_fs_type);
}

#else /* MODULE */

__initfunc(int init_autofs_fs(void))
{
	return register_filesystem(&autofs_fs_type);
}

#endif /* !MODULE */

#ifdef DEBUG
void autofs_say(const char *name, int len)
{
	printk("(%d: ", len);
	while ( len-- )
		printk("%c", *name++);
	printk(")\n");
}
#endif
