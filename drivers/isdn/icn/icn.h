/* $Id: icn.h,v 1.13 1996/04/20 16:51:41 fritz Exp $
 *
 * ISDN lowlevel-module for the ICN active ISDN-Card.
 *
 * Copyright 1994 by Fritz Elfert (fritz@wuemaus.franken.de)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: icn.h,v $
 * Revision 1.13  1996/04/20 16:51:41  fritz
 * Increased status buffer.
 * Misc. typos
 *
 * Revision 1.12  1996/01/22 05:01:22  fritz
 * Revert to GPL.
 *
 * Revision 1.11  1995/12/18  18:25:00  fritz
 * Support for ICN-2B Cards.
 * Change for supporting user-settable service-octet.
 *
 * Revision 1.10  1995/10/29  21:43:10  fritz
 * Added support for leased lines.
 *
 * Revision 1.9  1995/04/23  13:42:10  fritz
 * Added some constants for distinguishing 1TR6 and DSS1
 *
 * Revision 1.8  1995/03/25  23:18:55  fritz
 * Changed ICN_PORTLEN to reflect correct number of ports.
 *
 * Revision 1.7  1995/03/15  12:52:06  fritz
 * Some cleanup
 *
 * Revision 1.6  1995/02/20  03:49:22  fritz
 * Fixed ICN_MAX_SQUEUE to correctly reflect outstanding bytes, not number
 * of buffers.
 *
 * Revision 1.5  1995/01/29  23:36:50  fritz
 * Minor cleanup.
 *
 * Revision 1.4  1995/01/09  07:41:20  fritz
 * Added GPL-Notice
 *
 * Revision 1.3  1995/01/04  05:14:20  fritz
 * removed include of linux/asm/string.h for compiling with Linux 1.1.76
 *
 * Revision 1.2  1995/01/02  02:15:57  fritz
 * Misc. Bugfixes
 *
 * Revision 1.1  1994/12/14  18:02:38  fritz
 * Initial revision
 *
 */

#ifndef icn_h
#define icn_h

#define ICN_IOCTL_SETMMIO   0
#define ICN_IOCTL_GETMMIO   1
#define ICN_IOCTL_SETPORT   2
#define ICN_IOCTL_GETPORT   3
#define ICN_IOCTL_LOADBOOT  4
#define ICN_IOCTL_LOADPROTO 5
#define ICN_IOCTL_LEASEDCFG 6
#define ICN_IOCTL_GETDOUBLE 7
#define ICN_IOCTL_DEBUGVAR  8

#if defined(__KERNEL__) || defined(__DEBUGVAR__)

#ifdef __KERNEL__
/* Kernel includes */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>

#endif				/* __KERNEL__ */

/* some useful macros for debugging */
#ifdef ICN_DEBUG_PORT
#define OUTB_P(v,p) {printk(KERN_DEBUG "icn: outb_p(0x%02x,0x%03x)\n",v,p); outb_p(v,p);}
#else
#define OUTB_P outb
#endif

/* Defaults for Port-Address and shared-memory */
#define ICN_BASEADDR 0x320
#define ICN_PORTLEN (0x04)
#define ICN_MEMADDR 0x0d0000

/* Macros for accessing ports */
#define ICN_CFG    (dev->port)
#define ICN_MAPRAM (dev->port+1)
#define ICN_RUN    (dev->port+2)
#define ICN_BANK   (dev->port+3)

#define ICN_FLAGS_B1ACTIVE 1	/* B-Channel-1 is open                 */
#define ICN_FLAGS_B2ACTIVE 2	/* B-Channel-2 is open                 */
#define ICN_FLAGS_RBTIMER  8	/* cyclic scheduling of B-Channel-poll */

#define ICN_BOOT_TIMEOUT1  100	/* Delay for Boot-download (jiffies)   */
#define ICN_CHANLOCK_DELAY  10	/* Delay for Channel-mapping (jiffies) */

#define ICN_TIMER_BCREAD 3	/* B-Channel poll-cycle                */
#define ICN_TIMER_DCREAD 50	/* D-Channel poll-cycle                */

#define ICN_CODE_STAGE1 4096	/* Size of bootcode                    */
#define ICN_CODE_STAGE2 65536	/* Size of protocol-code               */

#define ICN_MAX_SQUEUE 65536	/* Max. outstanding send-data          */
#define ICN_FRAGSIZE (250)	/* Max. size of send-fragments         */
#define ICN_BCH 2		/* Number of supported channels        */

/* type-definitions for accessing the mmap-io-areas */

#define SHM_DCTL_OFFSET (0)	/* Offset to data-controlstructures in shm */
#define SHM_CCTL_OFFSET (0x1d2)	/* Offset to comm-controlstructures in shm */
#define SHM_CBUF_OFFSET (0x200)	/* Offset to comm-buffers in shm           */
#define SHM_DBUF_OFFSET (0x2000)	/* Offset to data-buffers in shm           */

typedef struct {
	unsigned char length;	/* Bytecount of fragment (max 250)     */
	unsigned char endflag;	/* 0=last frag., 0xff=frag. continued  */
	unsigned char data[ICN_FRAGSIZE];	/* The data                            */
	/* Fill to 256 bytes */
	char unused[0x100 - ICN_FRAGSIZE - 2];
} frag_buf;

typedef union {
	struct {
		unsigned char scns;	/* Index to free SendFrag.             */
		unsigned char scnr;	/* Index to active SendFrag   READONLY */
		unsigned char ecns;	/* Index to free RcvFrag.     READONLY */
		unsigned char ecnr;	/* Index to valid RcvFrag              */
		char unused[6];
		unsigned short fuell1;	/* Internal Buf Bytecount              */
	} data_control;
	struct {
		char unused[SHM_CCTL_OFFSET];
		unsigned char iopc_i;	/* Read-Ptr Status-Queue      READONLY */
		unsigned char iopc_o;	/* Write-Ptr Status-Queue              */
		unsigned char pcio_i;	/* Write-Ptr Command-Queue             */
		unsigned char pcio_o;	/* Read-Ptr Command Queue     READONLY */
	} comm_control;
	struct {
		char unused[SHM_CBUF_OFFSET];
		unsigned char pcio_buf[0x100];	/* Ring-Buffer Command-Queue           */
		unsigned char iopc_buf[0x100];	/* Ring-Buffer Status-Queue            */
	} comm_buffers;
	struct {
		char unused[SHM_DBUF_OFFSET];
		frag_buf receive_buf[0x10];
		frag_buf send_buf[0x10];
	} data_buffers;
} icn_shmem;

/* Sendbuffer-queue-element */
typedef struct {
	char *next;
	short length;
	short size;
	u_char *rptr;
	u_char buffer[1];
} pqueue;

typedef struct {
	unsigned short port;	/* Base-port-address                */
	icn_shmem *shmem;	/* Pointer to memory-mapped-buffers */
	int myid;		/* Driver-Nr. assigned by linklevel */
	int rvalid;		/* IO-portregion has been requested */
	int mvalid;		/* IO-shmem has been requested      */
	int leased;		/* Flag: This Adapter is connected  */
	/*       to a leased line           */
	unsigned short flags;	/* Statusflags                      */
	int doubleS0;		/* Flag: Double-S0-Card             */
	int secondhalf;		/* Flag: Second half of a doubleS0  */
	int ptype;		/* Protocol type (1TR6 or Euro)     */
	struct timer_list st_timer;	/* Timer for Status-Polls           */
	struct timer_list rb_timer;	/* Timer for B-Channel-Polls        */
	int channel;		/* Currently mapped Channel         */
	int chanlock;		/* Semaphore for Channel-Mapping    */
	u_char rcvbuf[ICN_BCH][4096];	/* B-Channel-Receive-Buffers      */
	int rcvidx[ICN_BCH];	/* Index for above buffers          */
	int l2_proto[ICN_BCH];	/* Current layer-2-protocol         */
	isdn_if interface;	/* Interface to upper layer         */
	int iptr;		/* Index to imsg-buffer             */
	char imsg[60];		/* Internal buf for status-parsing  */
	char msg_buf[2048];	/* Buffer for status-messages       */
	char *msg_buf_write;	/* Writepointer for statusbuffer    */
	char *msg_buf_read;	/* Readpointer for statusbuffer     */
	char *msg_buf_end;	/* Pointer to end of statusbuffer   */
	int sndcount[ICN_BCH];	/* Byte-counters for B-Ch.-send     */
	pqueue *spqueue[ICN_BCH];	/* Pointers to start of Send-Queue  */
#ifdef DEBUG_RCVCALLBACK
	int akt_pending[ICN_BCH];
	int max_pending[ICN_BCH];
#endif
} icn_dev;

typedef icn_dev *icn_devptr;

#ifdef __KERNEL__
static icn_dev *dev = (icn_dev *) 0;
static icn_dev *dev2 = (icn_dev *) 0;

/* With modutils >= 1.1.67 Integers can be changed while loading a
 * module. For this reason define the Port-Base an Shmem-Base as
 * integers.
 */
int portbase = ICN_BASEADDR;
int membase = ICN_MEMADDR;
char *icn_id = "\0";
char *icn_id2 = "\0";
static char regname[35];	/* Name used for port/mem-registration */

#endif				/* __KERNEL__ */

/* Utility-Macros */

/* Return true, if there is a free transmit-buffer */
#define sbfree (((dev->shmem->data_control.scns+1) & 0xf) != \
                dev->shmem->data_control.scnr)

/* Switch to next transmit-buffer */
#define sbnext (dev->shmem->data_control.scns = \
               ((dev->shmem->data_control.scns+1) & 0xf))

/* Shortcuts for transmit-buffer-access */
#define sbuf_n dev->shmem->data_control.scns
#define sbuf_d dev->shmem->data_buffers.send_buf[sbuf_n].data
#define sbuf_l dev->shmem->data_buffers.send_buf[sbuf_n].length
#define sbuf_f dev->shmem->data_buffers.send_buf[sbuf_n].endflag

/* Return true, if there is receive-data is available */
#define rbavl  (dev->shmem->data_control.ecnr != \
                dev->shmem->data_control.ecns)

/* Switch to next receive-buffer */
#define rbnext (dev->shmem->data_control.ecnr = \
               ((dev->shmem->data_control.ecnr+1) & 0xf))

/* Shortcuts for receive-buffer-access */
#define rbuf_n dev->shmem->data_control.ecnr
#define rbuf_d dev->shmem->data_buffers.receive_buf[rbuf_n].data
#define rbuf_l dev->shmem->data_buffers.receive_buf[rbuf_n].length
#define rbuf_f dev->shmem->data_buffers.receive_buf[rbuf_n].endflag

/* Shortcuts for command-buffer-access */
#define cmd_o (dev->shmem->comm_control.pcio_o)
#define cmd_i (dev->shmem->comm_control.pcio_i)

/* Return free space in command-buffer */
#define cmd_free ((cmd_i>=cmd_o)?0x100-cmd_i+cmd_o:cmd_o-cmd_i)

/* Shortcuts for message-buffer-access */
#define msg_o (dev->shmem->comm_control.iopc_o)
#define msg_i (dev->shmem->comm_control.iopc_i)

/* Return length of Message, if avail. */
#define msg_avail ((msg_o>msg_i)?0x100-msg_o+msg_i:msg_i-msg_o)

#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)

/* Hopefully, a separate resource-registration-scheme for shared-memory
 * will be introduced into the kernel. Until then, we use the normal
 * routines, designed for port-registration.
 */
#define check_shmem   check_region
#define release_shmem release_region
#define request_shmem request_region

#endif				/* defined(__KERNEL__) || defined(__DEBUGVAR__) */
#endif				/* icn_h */
