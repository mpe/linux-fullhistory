/* sigio.c -- Support for SIGIO handler -*- linux-c -*-
 * Created: Thu Jun  3 15:39:18 1999 by faith@precisioninsight.com
 * Revised: Thu Jun  3 16:16:35 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * $PI: xc/programs/Xserver/hw/xfree86/os-support/shared/sigio.c,v 1.2 1999/06/14 21:11:29 faith Exp $
 * $XFree86: xc/programs/Xserver/hw/xfree86/os-support/shared/sigio.c,v 1.2 1999/06/14 12:02:11 dawes Exp $
 * 
 */


#ifdef XFree86Server
# include "X.h"
# include "xf86.h"
# include "xf86drm.h"
# include "xf86_OSlib.h"
#else
# include <unistd.h>
# include <signal.h>
# include <fcntl.h>
#endif

/*
 * Linux libc5 defines FASYNC, but not O_ASYNC.  Don't know if it is
 * functional or not.
 */
#if defined(FASYNC) && !defined(O_ASYNC)
#  define O_ASYNC FASYNC
#endif

int
xf86InstallSIGIOHandler(int fd, void (*f)(int))
{
    struct sigaction sa;
    struct sigaction osa;

    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGIO);
    sa.sa_flags   = 0;
    sa.sa_handler = f;
    sigaction(SIGIO, &sa, &osa);
    fcntl(fd, F_SETOWN, getpid());
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC);
    return 0;
}

int
xf86RemoveSIGIOHandler(int fd)
{
    struct sigaction sa;
    struct sigaction osa;

    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGIO);
    sa.sa_flags   = 0;
    sa.sa_handler = SIG_DFL;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_ASYNC);
    sigaction(SIGIO, &sa, &osa);
    return 0;
}
