/*
 *    in2000.h -  Linux device driver definitions for the
 *                Always IN2000 ISA SCSI card.
 *
 *    IMPORTANT: This file is for version 1.30 - 14/Oct/1996
 *
 * Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *    john@geolog.com
 *    jshiffle@netcom.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef IN2000_H
#define IN2000_H

extern struct proc_dir_entry proc_scsi_in2000;

int in2000_detect(Scsi_Host_Template *);
int in2000_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int in2000_abort(Scsi_Cmnd *);
void in2000_setup(char *, int *);
int in2000_proc_info(char *, char **, off_t, int, int, int);
int in2000_biosparam(struct scsi_disk *, kdev_t, int *);
int in2000_reset(Scsi_Cmnd *, unsigned int);


#define IN2000_CAN_Q    16
#define IN2000_SG       SG_ALL
#define IN2000_CPL      2
#define IN2000_HOST_ID  7

#define IN2000 {  NULL,                /* link pointer for modules */ \
                  NULL,                /* module pointer for modules */ \
                  &proc_scsi_in2000,   /* pointer to /proc/scsi directory entry */ \
                  in2000_proc_info,    /* pointer to proc info function */ \
                  "Always IN2000",     /* device name */ \
                  in2000_detect,       /* returns number of in2000's found */ \
                  NULL,                /* optional unload function for modules */ \
                  NULL,                /* optional misc info function */ \
                  NULL,                /* send scsi command, wait for completion */ \
                  in2000_queuecommand, /* queue scsi command, don't wait */ \
                  in2000_abort,        /* abort current command */ \
                  in2000_reset,        /* reset scsi bus */ \
                  NULL,                /* slave_attach - unused */ \
                  in2000_biosparam,    /* figures out BIOS parameters for lilo, etc */ \
                  IN2000_CAN_Q,        /* max commands we can queue up */ \
                  IN2000_HOST_ID,      /* host-adapter scsi id */ \
                  IN2000_SG,           /* scatter-gather table size */ \
                  IN2000_CPL,          /* commands per lun */ \
                  0,                   /* board counter */ \
                  0,                   /* unchecked dma */ \
                  DISABLE_CLUSTERING \
               }


#endif /* IN2000_H */
