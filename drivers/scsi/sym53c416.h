/*
 *  sym53c416.h
 * 
 *  Copyright (C) 1998 Lieven Willems (lw_linux@hotmail.com)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */

#ifndef _SYM53C416_H
#define _SYM53C416_H

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#include <linux/types.h>
#include <linux/kdev_t.h>

#define SYM53C416_SCSI_ID 7

extern struct proc_dir_entry proc_scsi_sym53c416;

extern int sym53c416_detect(Scsi_Host_Template *);
extern const char *sym53c416_info(struct Scsi_Host *);
extern int sym53c416_command(Scsi_Cmnd *);
extern int sym53c416_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int sym53c416_abort(Scsi_Cmnd *);
extern int sym53c416_reset(Scsi_Cmnd *, unsigned int);
extern int sym53c416_bios_param(Disk *, kdev_t, int *);
extern void sym53c416_setup(char *str, int *ints);

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,75)

#define SYM53C416 {                                          \
                  proc_dir:          &proc_scsi_sym53c416,   \
                  name:              "Symbios Logic 53c416", \
                  detect:            sym53c416_detect,       \
                  info:              sym53c416_info,         \
                  command:           sym53c416_command,      \
                  queuecommand:      sym53c416_queuecommand, \
                  abort:             sym53c416_abort,        \
                  reset:             sym53c416_reset,        \
                  bios_param:        sym53c416_bios_param,   \
                  can_queue:         1,                      \
                  this_id:           SYM53C416_SCSI_ID,      \
                  sg_tablesize:      32,                     \
                  cmd_per_lun:       1,                      \
                  unchecked_isa_dma: 1,                      \
                  use_clustering:    ENABLE_CLUSTERING       \
                  }

#else

#define SYM53C416 {                       \
                  NULL,                   \
                  NULL,                   \
                  &proc_scsi_sym53c416,   \
                  NULL,                   \
                  "Symbios Logic 53c416", \
                  sym53c416_detect,       \
                  NULL,                   \
                  sym53c416_info,         \
                  sym53c416_command,      \
                  sym53c416_queuecommand, \
                  sym53c416_abort,        \
                  sym53c416_reset,        \
                  NULL,                   \
                  sym53c416_bios_param,   \
                  1,                      \
                  SYM53C416_SCSI_ID,      \
                  32, /* ???? */          \
                  1,                      \
                  0,                      \
                  1,                      \
                  ENABLE_CLUSTERING       \
                  }

#endif

#endif
