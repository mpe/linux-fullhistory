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
#include <linux/auto_fs.h>

struct file_system_type autofs_fs_type = {
	autofs_read_super, "autofs", 0, NULL
};

int init_autofs_fs(void)
{
	return register_filesystem(&autofs_fs_type);
}

#ifdef MODULE
int init_module(void)
{
	int status;
	
	if ((status = init_autofs_fs()) == 0)
		register_symtab(0);
	return status;
}

void cleanup_module(void)
{
	unregister_filesystem(&autofs_fs_type);
}
#endif

#ifdef DEBUG
void autofs_say(const char *name, int len)
{
	printk("(%d: ", len);
	while ( len-- )
		printk("%c", *name++);
	printk(")\n");
}
#endif
