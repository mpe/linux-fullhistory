/*
 * creator.c: Linux/Sun Ultra Creator console support.
 *
 * Copyright (C) 1997 MIguel de Icaza (miguel@nuclecu.unam.mx)
 *
 */
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#include "../../char/vt_kern.h"
#include "../../char/selection.h"
#include "../../char/console_struct.h"
#include "fb.h"

__initfunc(void creator_setup (fbinfo_t *fb, int slot, int con_node, unsigned long creator, int creator_io))
{
        uint bases [2];
        unsigned long *p;

        if (!creator) {
                prom_getproperty (con_node, "address", (char *) &bases[0], 4);
                prom_printf ("Bases: %x %x\n", bases [0], bases [1]);
                p = (unsigned long *) creator = bases[0];
                fb->base = creator;
                fb->base = 0xff168000;
        }

        fb->type.fb_cmsize = 256;
        fb->mmap =  0;
        fb->loadcmap = 0;
        fb->setcursor = 0;
        fb->setcursormap = 0;
        fb->setcurshape = 0;
        fb->ioctl = 0;
        fb->switch_from_graph = 0;
        fb->postsetup = sun_cg_postsetup;
        fb->reset = 0;
        fb->blank = 0;
        fb->unblank = 0;
        fb->type.fb_depth = 8;
}
