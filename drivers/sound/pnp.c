/*
 *    sound/pnp.c - Temporary kludge for PnP soundcards.
 *
 *      Copyright by Hannu Savolainen 1997.
 *
 *      This file is just a temporary solution to be used with
 *      PnP soundcards until final kernel PnP support gets ready.
 *      The code contained in this file is largely untested and
 *      may cause failures in some systems. In particular it will
 *      cause troubles with other PnP ISA cards such as network cards.
 *      This file is also incompatible with (future) PnP support in kernel.
 *
 *      For the above reasons I don't want this file to be widely distributed.
 *      You have permission to use this file with this particular sound driver
 *      and only for your own evaluation purposes. Any other use of this file
 *      or parts of it requires written permission by the author.
 */
extern int      pnp_trace;
int             pnp_trace_io = 0;

#define UDELAY(x) udelay(x)

#ifdef TEST_PNP
#include "pnp.h"
#include <malloc.h>
#include <stdio.h>
#define printk printf

#define MALLOC(sz) malloc(sz)

unsigned char   res[10000];
int             rp;

#else
#include "sound_config.h"
#endif
