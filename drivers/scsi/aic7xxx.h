/*+M*************************************************************************
 * Adaptec 274x/284x/294x device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *   All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of Calgary
 *      Department of Computer Science and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $Id: aic7xxx.h,v 1.18 1995/06/22 04:17:56 deang Exp $
 *-M*************************************************************************/
#ifndef _aic7xxx_h
#define _aic7xxx_h

#define AIC7XXX_H_VERSION  "$Revision: 1.18 $"

/*
 * Scsi_Host_Template (see hosts.h) for AIC-7770/AIC-7870 - some fields
 * to do with card config are filled in after the card is detected.
 */
#define AIC7XXX	{						\
	NULL,							\
	NULL,							\
	generic_proc_info,					\
	"aic7xxx",						\
	PROC_SCSI_AIC7XXX,					\
	NULL,							\
	aic7xxx_detect,						\
	NULL,							\
	aic7xxx_info,						\
	NULL,							\
	aic7xxx_queue,						\
	aic7xxx_abort,						\
	aic7xxx_reset,						\
	NULL,							\
	aic7xxx_biosparam,					\
	-1,			/* max simultaneous cmds      */\
	-1,			/* scsi id of host adapter    */\
	SG_ALL,			/* max scatter-gather cmds    */\
	2,			/* cmds per lun (linked cmds) */\
	0,			/* number of 7xxx's present   */\
	0,			/* no memory DMA restrictions */\
	ENABLE_CLUSTERING					\
}

extern int aic7xxx_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
extern int aic7xxx_biosparam(Disk *, int, int[]);
extern int aic7xxx_detect(Scsi_Host_Template *);
extern int aic7xxx_command(Scsi_Cmnd *);
extern int aic7xxx_abort(Scsi_Cmnd *);
extern int aic7xxx_reset(Scsi_Cmnd *);

extern const char *aic7xxx_info(struct Scsi_Host *);

extern int generic_proc_info(char *, char **, off_t, int, int, int);

#endif /* _aic7xxx_h */
