/* -- sjcd.h
 *
 * Definitions for a Sanyo CD-ROM interface
 *
 *   Copyright (C) 1995  Vadim V. Model
 *
 *   model@cecmow.enet.dec.com
 *   vadim@rbrf.msk.su
 *   vadim@ipsun.ras.ru
 *
 */

#ifndef __SJCD_H__
#define __SJCD_H__

/*
 * Change this to set the I/O port address.
 */
#define SJCD_BASE_ADDR      0x340

/*
 * Change this to set the irq.
 */
#define SJCD_INTR_NR        10

/*
 * Change this to set the dma channel.
 */
#define SJCD_DMA            0

/*
 * port access macros
 */
#define SJCDPORT( x )       ( sjcd_port + ( x ) )

/* status bits */

#define SST_NOT_READY       0x10        /* no disk in the drive */
#define SST_MEDIA_CHANGED   0x20        /* disk is changed */
#define SST_DOOR_OPENED     0x40        /* door is open */

/* flag bits */

/* commands */

#define SCMD_EJECT_TRAY     0xD0        /* eject tray if not locked */
#define SCMD_LOCK_TRAY      0xD2        /* lock tray when in */
#define SCMD_UNLOCK_TRAY    0xD4        /* unlock tray when in */
#define SCMD_CLOSE_TRAY     0xD6        /* load tray in */

#define SCMD_RESET          0xFA        /* soft reset */
#define SCMD_GET_STATUS     0x80
#define SCMD_GET_VERSION    0xCC

#define SCMD_DATA_READ      0xA0
#define SCMD_SEEK           0xA0
#define SCMD_PLAY           0xA0

#define SCMD_GET_QINFO      0xA8

#define SCMD_SET_MODE       0xC4
#define SCMD_MODE_PLAY      0xE0
#define SCMD_MODE_COOKED    0xF8
#define SCMD_MODE_RAW       0xF9
#define SCMD_MODE_x20_BIT   0x20

#define SCMD_SET_VOLUME     0xAE
#define SCMD_PAUSE          0xE0
#define SCMD_STOP           0xE0

#define SCMD_GET_DISK_INFO  0xAA
#define SCMD_GET_1_TRACK    0xA0    /* get the first track information */
#define SCMD_GET_L_TRACK    0xA1    /* get the last track information */
#define SCMD_GET_D_SIZE     0xA2    /* get the whole disk information */

/*
 * borrowed from hd.c
 */
#define S_READ_DATA( port, buf, nr )      insb( port, buf, nr )

#define SJCD_MAX_TRACKS		100

struct msf {
  unsigned char   min;
  unsigned char   sec;
  unsigned char   frame;
};

struct sjcd_hw_disk_info {
  unsigned char track_control;
  unsigned char track_no;
  unsigned char x, y, z;
  union {
    unsigned char track_no;
    struct msf track_msf;
  } un;
};

struct sjcd_hw_qinfo {
  unsigned char track_control;
  unsigned char track_no;
  unsigned char x;
  struct msf rel;
  struct msf abs;
};

struct sjcd_play_msf {
  struct msf  start;
  struct msf  end;
};

struct sjcd_disk_info {
  unsigned char   first;
  unsigned char   last;
  struct msf      disk_length;
  struct msf      first_track;
};

struct sjcd_toc {
  unsigned char   ctrl_addr;
  unsigned char   track;
  unsigned char   point_index;
  struct msf      track_time;
  struct msf      disk_time;
};

struct sjcd_stat {
  int ticks;
  int tticks[ 8 ];
  int idle_ticks;
  int start_ticks;
  int mode_ticks;
  int read_ticks;
  int data_ticks;
  int stop_ticks;
  int stopping_ticks;
};

#endif
