/* $XFree86$ */
/*
 * OS depending defines
 *
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) All advertising materials mentioning features or use of this software
 * *    must display the following acknowledgement: "This product includes
 * *    software developed by Thomas Winischhofer, Vienna, Austria."
 * * 4) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *		Silicon Integrated Systems, Inc. (used by permission)
 *
 */

/* The choices are: */

#define LINUX_KERNEL	   /* Kernel framebuffer */
/* #define LINUX_XF86 */   /* XFree86 */

/**********************************************************************/
#ifdef LINUX_KERNEL  /* -------------------------- */
#include <linux/config.h>
#include <linux/version.h>

#ifdef CONFIG_FB_SIS_300
#define SIS300
#endif

#ifdef CONFIG_FB_SIS_315
#define SIS315H
#endif

#if 1
#define SISFBACCEL	/* Include 2D acceleration */
#endif

#endif

#ifdef LINUX_XF86 /* ----------------------------- */
#define SIS300
#define SIS315H
#endif

/**********************************************************************/
#ifdef LINUX_XF86
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#define SiS_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif

#ifdef LINUX_KERNEL
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#define SiS_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif

/**********************************************************************/

#ifdef OutPortByte
#undef OutPortByte
#endif /* OutPortByte */

#ifdef OutPortWord
#undef OutPortWord
#endif /* OutPortWord */

#ifdef OutPortLong
#undef OutPortLong
#endif /* OutPortLong */

#ifdef InPortByte
#undef InPortByte
#endif /* InPortByte */

#ifdef InPortWord
#undef InPortWord
#endif /* InPortWord */

#ifdef InPortLong
#undef InPortLong
#endif /* InPortLong */

/**********************************************************************/
/*  LINUX XF86                                                        */
/**********************************************************************/

#ifdef LINUX_XF86
#define OutPortByte(p,v) outb((CARD16)(p),(CARD8)(v))
#define OutPortWord(p,v) outw((CARD16)(p),(CARD16)(v))
#define OutPortLong(p,v) outl((CARD16)(p),(CARD32)(v))
#define InPortByte(p)    inb((CARD16)(p))
#define InPortWord(p)    inw((CARD16)(p))
#define InPortLong(p)    inl((CARD16)(p))
#endif

/**********************************************************************/
/*  LINUX KERNEL                                                      */
/**********************************************************************/

#ifdef LINUX_KERNEL
#define OutPortByte(p,v) outb((u8)(v),(u16)(p))
#define OutPortWord(p,v) outw((u16)(v),(u16)(p))
#define OutPortLong(p,v) outl((u32)(v),(u16)(p))
#define InPortByte(p)    inb((u16)(p))
#define InPortWord(p)    inw((u16)(p))
#define InPortLong(p)    inl((u16)(p))
#endif


