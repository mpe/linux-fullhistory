/*
 * QLogic ISP2100 SCSI-FCP
 * 
 * Written by Erik H. Moe, ehm@cris.com
 * Copyright 1995, Erik H. Moe
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/* Renamed and updated to 1.3.x by Michael Griffith <grif@cs.ucr.edu> */

/* This is a version of the isp1020 driver which was modified by
 * Chris Loveland <cwl@iol.unh.edu> to support the isp2100
 */


/*
 * $Date: 1995/09/22 02:32:56 $
 * $Revision: 0.5 $
 *
 * $Log: isp1020.h,v $
 * Revision 0.5  1995/09/22  02:32:56  root
 * do auto request sense
 *
 * Revision 0.4  1995/08/07  04:48:28  root
 * supply firmware with driver.
 * numerous bug fixes/general cleanup of code.
 *
 * Revision 0.3  1995/07/16  16:17:16  root
 * added reset/abort code.
 *
 * Revision 0.2  1995/06/29  03:19:43  root
 * fixed biosparam.
 * added queue protocol.
 *
 * Revision 0.1  1995/06/25  01:56:13  root
 * Initial release.
 *
 */

#ifndef _QLOGICFC_H
#define _QLOGICFC_H

/*
 * With the qlogic interface, every queue slot can hold a SCSI
 * command with up to 2 scatter/gather entries.  If we need more
 * than 2 entries, continuation entries can be used that hold
 * another 5 entries each.  Unlike for other drivers, this means
 * that the maximum number of scatter/gather entries we can
 * support at any given time is a function of the number of queue
 * slots available.  That is, host->can_queue and host->sg_tablesize
 * are dynamic and _not_ independent.  This all works fine because
 * requests are queued serially and the scatter/gather limit is
 * determined for each queue request anew.
 */
#define QLOGICFC_REQ_QUEUE_LEN	63	/* must be power of two - 1 */
#define QLOGICFC_MAX_SG(ql)	(2 + (((ql) > 0) ? 5*((ql) - 1) : 0))
#define QLOGICFC_CMD_PER_LUN    8

int isp2100_detect(Scsi_Host_Template *);
int isp2100_release(struct Scsi_Host *);
const char * isp2100_info(struct Scsi_Host *);
int isp2100_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int isp2100_abort(Scsi_Cmnd *);
int isp2100_reset(Scsi_Cmnd *, unsigned int);
int isp2100_biosparam(Disk *, kdev_t, int[]);

#ifndef NULL
#define NULL (0)
#endif

extern struct proc_dir_entry proc_scsi_isp2100;

#define QLOGICFC {							   \
        detect:                 isp2100_detect,                            \
        release:                isp2100_release,                           \
        info:                   isp2100_info,                              \
        queuecommand:           isp2100_queuecommand,                      \
        abort:                  isp2100_abort,                             \
        reset:                  isp2100_reset,                             \
        bios_param:             isp2100_biosparam,                         \
        can_queue:              QLOGICFC_REQ_QUEUE_LEN,                    \
        this_id:                -1,                                        \
        sg_tablesize:           QLOGICFC_MAX_SG(QLOGICFC_REQ_QUEUE_LEN),   \
        cmd_per_lun:            QLOGICFC_CMD_PER_LUN,                      \
        present:                0,                                         \
        unchecked_isa_dma:      0,                                         \
        use_clustering:         DISABLE_CLUSTERING                         \
}

#endif /* _QLOGICFC_H */



