/*
 * Wrap-around code for a console using the
 * SGI PROM io-routines.
 *
 * Copyright (c) 1999 Ulf Carlsson
 *
 * Derived from DECstation promcon.c
 * Copyright (c) 1998 Harald Koerfgen 
 */

#include <linux/tty.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>

#include <asm/sgialib.h>

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
    unsigned i;

    /*
     *    Now, do each character
     */
    for (i = 0; i < count; i++) {
	if (*s == 10)
	    prom_printf("%c", 13);
	prom_printf("%c", *s++);
    }
}

static int prom_console_wait_key(struct console *co)
{
    return prom_getchar();
}

__initfunc(static int prom_console_setup(struct console *co, char *options))
{
    return 0;
}

static kdev_t prom_console_device(struct console *c)
{
    return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console sercons =
{
    "ttyS",
    prom_console_write,
    NULL,
    prom_console_device,
    prom_console_wait_key,
    NULL,
    prom_console_setup,
    CON_PRINTBUFFER,
    -1,
    0,
    NULL
};

/*
 *    Register console.
 */

__initfunc(long sgi_prom_console_init(long kmem_start, long kmem_end))
{
    register_console(&sercons);
    return kmem_start;
}
