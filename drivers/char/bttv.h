/* 
    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97 Ralph Metzler (rjkm@thp.uni-koeln.de)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BTTV_H_
#define _BTTV_H_

#define TEST_VBI

#include <linux/types.h>
#include <linux/wait.h>

#include "bt848.h"

typedef unsigned int dword;

struct riscprog {
  uint length;  
  dword *busadr;
  dword *prog;
};

/* values that can be set by user programs */

struct bttv_window {
  int x, y;
  ushort width, height;
  ushort bpp, bpl;
  ushort swidth, sheight;
  short cropx, cropy;
  ushort cropwidth, cropheight;
  int vidadr;
  ushort freq;
  int norm;
  int interlace;
  int color_fmt;
};

/* private data that can only be read (or set indirectly) by user program */

struct bttv {
  struct video_device video_dev;
  struct video_picture picture;		/* Current picture params */
  struct video_audio audio_dev;		/* Current audio params */
  u_char bus;          /* PCI bus the Bt848 is on */
  u_char devfn;
  u_char revision;
  u_char irq;          /* IRQ used by Bt848 card */
  uint bt848_adr;      /* bus address of IO mem returned by PCI BIOS */
  u_char *bt848_mem;   /* pointer to mapped IO memory */
  ulong busriscmem; 
  dword *riscmem;
  
  u_char *vbibuf;
  struct bttv_window win;
  int type;            /* card type  */
  int audio;           /* audio mode */
  int user;
  int tuner;
  int tuneradr;
  int dbx;

  dword *risc_jmp;
  dword *vbi_odd;
  dword *vbi_even;
  dword bus_vbi_even;
  dword bus_vbi_odd;
  struct wait_queue *vbiq;
  struct wait_queue *capq;
  int vbip;

  dword *risc_odd;
  dword *risc_even;
  int cap;
};

/*The following should be done in more portable way. It depends on define
  of _ALPHA_BTTV in the Makefile.*/
#ifdef _ALPHA_BTTV
#define btwrite(dat,adr)    writel((dat),(char *) (btv->bt848_adr+(adr)))
#define btread(adr)         readl(btv->bt848_adr+(adr))
#else
#define btwrite(dat,adr)    writel((dat), (char *) (btv->bt848_mem+(adr)))
#define btread(adr)         readl(btv->bt848_mem+(adr))
#endif

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

/* bttv ioctls */
#define BTTV_WRITE_BTREG   0x00
#define BTTV_READ_BTREG    0x01
#define BTTV_SET_BTREG     0x02
#define BTTV_SETRISC       0x03
#define BTTV_SETWTW        0x04
#define BTTV_GETWTW        0x05
#define BTTV_DMA           0x06
#define BTTV_CAP_OFF       0x07
#define BTTV_CAP_ON        0x08
#define BTTV_GETBTTV       0x09
#define BTTV_SETFREQ       0x0a
#define BTTV_SETCHAN       0x0b
#define BTTV_INPUT         0x0c
#define BTTV_READEE        0x0d
#define BTTV_WRITEEE       0x0e
#define BTTV_BRIGHT        0x0f
#define BTTV_HUE           0x10
#define BTTV_COLOR         0x11
#define BTTV_CONTRAST      0x12
#define BTTV_SET_FFREQ     0x13
#define BTTV_MUTE          0x14

#define BTTV_GRAB          0x20
#define BTTV_TESTM         0x20


#define BTTV_UNKNOWN       0x00
#define BTTV_MIRO          0x01
#define BTTV_HAUPPAUGE     0x02
#define BTTV_STB           0x03
#define BTTV_INTEL         0x04
#define BTTV_DIAMOND       0x05 

#define AUDIO_TUNER        0x00
#define AUDIO_EXTERN       0x01
#define AUDIO_INTERN       0x02
#define AUDIO_OFF          0x03 
#define AUDIO_ON           0x04
#define AUDIO_MUTE         0x80
#define AUDIO_UNMUTE       0x81

#define I2C_TSA5522        0xc2
#define I2C_TDA9850        0xb6
#define I2C_HAUPEE         0xa0
#define I2C_STBEE          0xae

#define TDA9850_CON1       0x04
#define TDA9850_CON2       0x05
#define TDA9850_CON3       0x06
#define TDA9850_CON4       0x07
#define TDA9850_ALI1       0x08
#define TDA9850_ALI2       0x09
#define TDA9850_ALI3       0x0a

#endif
