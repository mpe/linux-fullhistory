/*
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MCONSOLE_KERN_H__
#define __MCONSOLE_KERN_H__

#include "linux/config.h"
#include "linux/list.h"
#include "mconsole.h"

struct mconsole_entry {
	struct list_head list;
	struct mc_request request;
};

struct mc_device {
	struct list_head list;
	char *name;
	int (*config)(char *);
	int (*remove)(char *);
};

#ifdef CONFIG_MCONSOLE

extern void mconsole_register_dev(struct mc_device *new);

#else

static inline void mconsole_register_dev(struct mc_device *new)
{
}

#endif

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
