/*
 * av7110.c: driver for the SAA7146 based AV110 cards (like the Fujitsu-Siemens DVB)
 *           and Nova/Budget DVB cards 
 *
 * Copyright (C) 1999-2002 Ralph  Metzler 
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#define NEW_CI 1

#define __KERNEL_SYSCALLS__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/unistd.h>
#include <linux/byteorder/swabb.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/semaphore.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/dvb/frontend.h>

#include "../dvb-core/dvb_i2c.h"
#include "../dvb-core/dvb_frontend.h"
#include "av7110.h"

#include "saa7146_core.h"
#include "saa7146_v4l.h"
#include "saa7146_defs.h"


static int AV_StartPlay(av7110_t *av7110, int av);
static void restart_feeds(av7110_t *av7110);
static int bootarm(av7110_t *av7110);
static inline int i2c_writereg(av7110_t *av7110, u8 id, u8 reg, u8 val);
static inline u8 i2c_readreg(av7110_t *av7110, u8 id, u8 reg);
static int  outcom(av7110_t *av7110, int type, int com, int num, ...);
static void SetMode(av7110_t *av7110, int mode);

void pes_to_ts(u8 const *buf, long int length, u16 pid, p2t_t *p);
void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter, struct dvb_demux_feed *feed);

static u32 vidmem	= 0;
static u32 vidlow	= 0;

static int av7110_debug = 0;
#define dprintk	if (av7110_debug) printk

static int vidmode=CVBS_RGB_OUT;
static int init_vpid;
static int init_apid;
static int pids_off;
static int adac=DVB_ADAC_TI;

#define saacomm(x,y) av7110->saa->command(av7110->saa->i2c_bus, (x), (y))


/****************************************************************************
 * General helper functions
 ****************************************************************************/

static inline void ddelay(int i) 
{
        current->state=TASK_INTERRUPTIBLE;
        schedule_timeout((HZ*i)/100);
}


/****************************************************************************
 * GPIO and DEBI functions
 ****************************************************************************/

#define saaread(adr)            saa7146_read(saamem,(adr))
#define saawrite(dat,adr) 	saa7146_write(saamem,(adr),(dat))

inline static void 
setgpio(av7110_t *av7110, int port, u32 data)
{
        void *saamem=av7110->saa_mem;
        u32 val;

        val=saaread(GPIO_CTRL);
        val&=~(0xff << (8*(port)));
        val|=(data)<<(8*(port));
        saawrite(val, GPIO_CTRL);
}

/* This DEBI code is based on the Stradis driver 
   by Nathan Laredo <laredo@gnu.org> */

static
int wait_for_debi_done(av7110_t *av7110)
{
        void *saamem=av7110->saa_mem;
	int start;

	/* wait for registers to be programmed */
	start = jiffies;
	while (1) {
                if (saaread(MC2) & 2)
                        break;
		if (jiffies-start > HZ/20) {
			printk ("%s: timed out while waiting for registers "
				"getting programmed\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	/* wait for transfer to complete */
	start = jiffies;
	while (1) {
		if (!(saaread(PSR) & SPCI_DEBI_S))
			break;
		saaread(MC2);
		if (jiffies-start > HZ/4) {
			printk ("%s: timed out while waiting for transfer "
				"completion\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int debiwrite(av7110_t *av7110, u32 config, 
                     int addr, u32 val, int count)
{
        void *saamem=av7110->saa_mem;
	u32 cmd;

	if (count <= 0 || count > 32764)
		return -1;
	if (wait_for_debi_done(av7110) < 0)
		return -1;
	saawrite(config, DEBI_CONFIG);
	if (count <= 4)		/* immediate transfer */
		saawrite(val, DEBI_AD);
	else			/* block transfer */
		saawrite(av7110->debi_bus, DEBI_AD);
	saawrite((cmd = (count << 17) | (addr & 0xffff)), DEBI_COMMAND);
	saawrite((2 << 16) | 2, MC2);
	return 0;
}

static u32 debiread(av7110_t *av7110, u32 config, int addr, int count)
{
        void *saamem=av7110->saa_mem;
	u32 result = 0;

	if (count > 32764 || count <= 0)
		return 0;
	if (wait_for_debi_done(av7110) < 0)
		return 0;
	saawrite(av7110->debi_bus, DEBI_AD);
	saawrite((count << 17) | 0x10000 | (addr & 0xffff),
		 DEBI_COMMAND);

	saawrite(config, DEBI_CONFIG);
	saawrite((2 << 16) | 2, MC2);
	if (count > 4)	
		return count;
	wait_for_debi_done(av7110);
	result = saaread(DEBI_AD);
        result &= (0xffffffffUL >> ((4-count)*8));
	return result;
}

/* DEBI during interrupt */

static inline void 
iwdebi(av7110_t *av7110, u32 config, int addr, u32 val, int count)
{
        if (count>4 && val)
                memcpy(av7110->debi_virt, (char *) val, count);
        debiwrite(av7110, config, addr, val, count);
}

static inline u32 
irdebi(av7110_t *av7110, u32 config, int addr, u32 val, int count)
{
        u32 res;

        res=debiread(av7110, config, addr, count);
        if (count<=4) 
                memcpy(av7110->debi_virt, (char *) &res, count);
        return res;
}

/* DEBI outside interrupts, only for count<=4! */

static inline void 
wdebi(av7110_t *av7110, u32 config, int addr, u32 val, int count)
{
        unsigned long flags;

        spin_lock_irqsave(&av7110->debilock, flags);
        debiwrite(av7110, config, addr, val, count);
        spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline u32 
rdebi(av7110_t *av7110, u32 config, int addr, u32 val, int count)
{
        unsigned long flags;
        u32 res;

        spin_lock_irqsave(&av7110->debilock, flags);
        res=debiread(av7110, config, addr, count);
        spin_unlock_irqrestore(&av7110->debilock, flags);
        return res;
}


static inline char 
chtrans(char c)
{
        if (c<32 || c>126)
                c=0x20;
        return c;
}


/* handle mailbox registers of the dual ported RAM */

static inline void 
ARM_ResetMailBox(av7110_t *av7110)
{
        unsigned long flags;

        spin_lock_irqsave(&av7110->debilock, flags);
        debiread(av7110, DEBINOSWAP, IRQ_RX, 2);
        //printk("dvb: IRQ_RX=%d\n", debiread(av7110, DEBINOSWAP, IRQ_RX, 2));
        debiwrite(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
        spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline void 
ARM_ClearMailBox(av7110_t *av7110)
{
        iwdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

static inline void 
ARM_ClearIrq(av7110_t *av7110)
{
	irdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

static void 
reset_arm(av7110_t *av7110)
{
        setgpio(av7110, RESET_LINE, GPIO_OUTLO);

        /* Disable DEBI and GPIO irq */
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) & ~(MASK_19 | MASK_03));
        saa7146_write(av7110->saa_mem, ISR, (MASK_19 | MASK_03));

        mdelay(800);
        setgpio(av7110, RESET_LINE, GPIO_OUTHI);
        mdelay(800);

        ARM_ResetMailBox(av7110); 

        saa7146_write(av7110->saa_mem, ISR, (MASK_19 | MASK_03));
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) | MASK_03 );

        av7110->arm_ready=1;
        printk("av7110: ARM RESET\n");
}

static void 
recover_arm(av7110_t *av7110)
{
        if (current->files)
                bootarm(av7110);
        else {
                printk("OOPS, no current->files\n");
                reset_arm(av7110);
        }
        ddelay(10); 
        restart_feeds(av7110);
}

static void 
arm_error(av7110_t *av7110)
{
        av7110->arm_errors++;
        av7110->arm_ready=0;
        recover_arm(av7110);
}

static int arm_thread(void *data)
{
	av7110_t *av7110 = data;
        u16 newloops;
    
	lock_kernel();
#if 0
	daemonize();
#else
        exit_mm(current);
        current->session=current->pgrp=1;
#endif
	sigfillset(&current->blocked);
	strcpy(current->comm, "arm_mon");
	av7110->arm_thread = current;
	unlock_kernel();

	while (!av7110->arm_rmmod && !signal_pending(current)) {
                interruptible_sleep_on_timeout(&av7110->arm_wait, 5*HZ);

                if (!av7110->arm_ready)
                        continue;

                if (down_interruptible(&av7110->dcomlock))
                        break;

                newloops=rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2);
                up(&av7110->dcomlock);

                if (newloops==av7110->arm_loops) {
                        printk("av7110%d: ARM crashed!\n",
				av7110->saa->dvb_adapter->num);

			arm_error(av7110);

                        if (down_interruptible(&av7110->dcomlock))
                                break;

                        newloops=rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2)-1;
                        up(&av7110->dcomlock);
                }
                av7110->arm_loops=newloops;
	}

	av7110->arm_thread = NULL;
	return 0;
}


static int
record_cb(dvb_filter_pes2ts_t *p2t, u8 *buf, size_t len)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) p2t->priv;

        if (!(dvbdmxfeed->ts_type & TS_PACKET)) 
                return 0;
	if (buf[3]==0xe0)        // video PES do not have a length in TS
                buf[4]=buf[5]=0;
        if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY) 
                return dvbdmxfeed->cb.ts(buf, len, 0, 0, 
                                         &dvbdmxfeed->feed.ts, DMX_OK); 
        else
                return dvb_filter_pes2ts(p2t, buf, len);
}

static int 
dvb_filter_pes2ts_cb(void *priv, unsigned char *data)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) priv;
        
        dvbdmxfeed->cb.ts(data, 188, 0, 0,
                          &dvbdmxfeed->feed.ts,
                          DMX_OK); 
        return 0;
}

static int 
AV_StartRecord(av7110_t *av7110, int av,
               struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
  
        if (av7110->playing||(av7110->rec_mode&av))
                return -EBUSY;
        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
        dvbdmx->recording=1;
        av7110->rec_mode|=av;

        switch (av7110->rec_mode) {
        case RP_AUDIO:
                dvb_filter_pes2ts_init (&av7110->p2t[0],
					dvbdmx->pesfilter[0]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[0]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
                break;

        case RP_VIDEO:
                dvb_filter_pes2ts_init (&av7110->p2t[1],
					dvbdmx->pesfilter[1]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[1]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
                break;

        case RP_AV:
                dvb_filter_pes2ts_init (&av7110->p2t[0],
					dvbdmx->pesfilter[0]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[0]);
                dvb_filter_pes2ts_init (&av7110->p2t[1],
					dvbdmx->pesfilter[1]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[1]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AV_PES, 0);
                break;
        }
        return 0;
}

static int 
AV_StartPlay(av7110_t *av7110, int av)
{
        if (av7110->rec_mode)
                return -EBUSY;
        if (av7110->playing&av)
                return -EBUSY;

        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);

        if (av7110->playing == RP_NONE) {
                dvb_filter_ipack_reset(&av7110->ipack[0]);
                dvb_filter_ipack_reset(&av7110->ipack[1]);
        }

        av7110->playing|=av;
        switch (av7110->playing) {
        case RP_AUDIO:
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
                break;
        case RP_VIDEO:
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
                av7110->sinfo=0;
                break;
        case RP_AV:
                av7110->sinfo=0;
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AV_PES, 0);
                break;
        }
        return av7110->playing;
}

static void 
AV_Stop(av7110_t *av7110, int av)
{
        if (!(av7110->playing&av) && !(av7110->rec_mode&av))
                return;

        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
        if (av7110->playing) {
                av7110->playing&=~av;
                switch (av7110->playing) {
                case RP_AUDIO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
                        break;
                case RP_VIDEO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
                        break;
                case RP_NONE:
                        SetMode(av7110, av7110->vidmode);
                        break;
                }
        } else {
                av7110->rec_mode&=~av;
                switch (av7110->rec_mode) {
                case RP_AUDIO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
                        break;
                case RP_VIDEO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
                        break;
                case RP_NONE:
                        break;
                }
        }
}

/****************************************************************************
 * Buffer handling
 ****************************************************************************/

static inline void 
ring_buffer_flush(ring_buffer_t *rbuf)
{
        spin_lock_irq(&rbuf->lock);
        rbuf->pwrite=rbuf->pread;
        spin_unlock_irq(&rbuf->lock);
        wake_up(&rbuf->queue);
}

static inline void 
ring_buffer_init(ring_buffer_t *rbuf, u8 *data, int len)
{
        rbuf->pread=rbuf->pwrite=0;
        rbuf->data=data;
        rbuf->size=len;
        init_waitqueue_head(&rbuf->queue);
        spin_lock_init(&(rbuf->lock));
        rbuf->lock=SPIN_LOCK_UNLOCKED;
        sema_init(&(rbuf->sema), 1);
}

static inline 
int ring_buffer_empty(ring_buffer_t *rbuf)
{
        return (rbuf->pread==rbuf->pwrite);
}

static inline 
int ring_buffer_free(ring_buffer_t *rbuf)
{
        int free;
  
        free=rbuf->pread - rbuf->pwrite;
        if (free<=0)
                free+=rbuf->size;
        return free;
}

static inline 
int ring_buffer_avail(ring_buffer_t *rbuf)
{
        int avail;
  
        avail=rbuf->pwrite - rbuf->pread;
        if (avail<0)
                avail+=rbuf->size;
        return avail;
}

#if 0
static void
ring_buffer_block(ring_buffer_t *rbuf, unsigned long count)
{
        if (ring_buffer_free(rbuf)>=count)
                return;
        while (!wait_event_interruptible(rbuf->queue,
                                         (ring_buffer_free(rbuf)>=count)));
}
#endif

static long 
ring_buffer_write(ring_buffer_t *rbuf, 
              const char *buf, unsigned long count, 
              int nonblock, int usermem)
{
        unsigned long todo = count;
        int free, split;
    
        while (todo > 0) {
                if (ring_buffer_free(rbuf)<=2048) {
                        if (nonblock)
                                return count-todo;
                        if (wait_event_interruptible(rbuf->queue,
                                                     (ring_buffer_free(rbuf)>2048)))
                        	return count-todo;
                }   
                dprintk ("function: %s pread=%08x pwrite=%08x\n", __FUNCTION__,
                        rbuf->pread, rbuf->pwrite);
                //mdelay(2);
                free = rbuf->pread - rbuf->pwrite;
                split=rbuf->size;
                if (free<=0) {
                        free+=rbuf->size;
                        split-=rbuf->pwrite;
                }
                if (free > todo)
                        free = todo;
                        
                if (split < free) {
                        if (!usermem) 
                                memcpy(rbuf->data+rbuf->pwrite, buf, split);
                        else
                                if (copy_from_user(rbuf->data+rbuf->pwrite,
                                                   buf, split)) 
                                        return -EFAULT;
                        buf += split;
                        todo -= split;
                        free -= split;
                        rbuf->pwrite = 0;
                }
                if (!usermem) 
                        memcpy(rbuf->data+rbuf->pwrite, buf, free);
                else
                        if (copy_from_user(rbuf->data+rbuf->pwrite, buf, free)) 
                                return -EFAULT;
                rbuf->pwrite = (rbuf->pwrite + free)%rbuf->size;
                todo -= free;
                buf += free;
        }

	return count-todo;
}

#if 0
static void
ring_buffer_put(ring_buffer_t *db, u8 *buf, int len)
{
        int split, fsize;

        fsize=db->pread - db->pwrite;
        if (fsize <= 0) {
                fsize+=db->size;
                split=db->size-db->pwrite;
        } else 
                split=0;
        if (len>=fsize) {
                dprintk("buffer overflow\n");
                return;
        }
        if (split>=len)
                split=0;
        if (split) {
                memcpy(db->data + db->pwrite, buf, split);
                len-=split;
                db->pwrite=0;
        }
        memcpy(db->data + db->pwrite, split + buf, len);
        db->pwrite=(db->pwrite+len)%db->size;
}
#endif


/****************************************************************************
 * TT budget / WinTV Nova
 ****************************************************************************/

static int 
TTBStop(av7110_t *av7110)
{
        if (--av7110->feeding)
                return av7110->feeding;
        saa7146_write(av7110->saa_mem, MC1, MASK_20); // DMA3 off
        saa7146_write(av7110->saa_mem, MC1, MASK_28); // RPS0 off
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) & ~MASK_10 );
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER)& ~MASK_07);
        return 0;
}

#define TS_WIDTH  (4*188)
#define TS_HEIGHT (1024/4)
static int
TTBStart(av7110_t *av7110)
{
        struct saa7146 *saa=av7110->saa;

        //printk ("function : %s\n", __FUNCTION__);
        if (av7110->feeding) 
                return ++av7110->feeding;

      	saa7146_write(saa->mem, MC1, MASK_20); // DMA3 off

        memset(saa->grabbing, 0x00, TS_HEIGHT*TS_WIDTH);

        saa7146_write(saa->mem, PCI_BT_V1, 0x001c0000);

        av7110->tsf=0;
        av7110->ttbp=0;
        saa7146_write(saa->mem, DD1_INIT, 0x02000680);
        saa7146_write(saa->mem, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

        saa7146_write(saa->mem, BRS_CTRL, 0x60000000);	
      	saa7146_write(saa->mem, MC2, (MASK_08 | MASK_24));
        mdelay(10);

        saa7146_write(saa->mem, BASE_ODD3, 0);
        saa7146_write(saa->mem, BASE_EVEN3, TS_WIDTH*TS_HEIGHT/2);
        saa7146_write(saa->mem, PROT_ADDR3, TS_WIDTH*TS_HEIGHT);	
        saa7146_write(saa->mem, BASE_PAGE3, virt_to_bus(saa->page_table[0])|ME1|0xb0);
        saa7146_write(saa->mem, PITCH3, TS_WIDTH);	

        saa7146_write(saa->mem, NUM_LINE_BYTE3, ((TS_HEIGHT/2)<<16)|TS_WIDTH);
      	saa7146_write(saa->mem, MC2, (MASK_04 | MASK_20));

        // VPE
        saa7146_write(saa->mem, IER, saa7146_read(saa->mem, IER)|MASK_10);

     	saa7146_write(saa->mem, MC1, (MASK_04 | MASK_20)); // DMA3 on

        // FIDB
        saa7146_write(saa->mem, IER, saa7146_read(saa->mem, IER)|MASK_07);

        return ++av7110->feeding;
}

/**
 *  Hack! we save the last av7110 ptr. This should be ok, since
 *  you rarely will use more then one IR control. 
 *
 *  If we want to support multiple controls we would have to do much more...
 */
void av7110_setup_irc_config (av7110_t *av7110, u32 ir_config)
{
	static av7110_t *last;

	if (!av7110)
		av7110 = last;
	else
		last = av7110;

	outcom(av7110, COMTYPE_PIDFILTER, SetIR, 1, ir_config);
}

static void (*irc_handler)(u32);

void av7110_register_irc_handler(void (*func)(u32)) 
{
        //dprintk("registering %08x\n",func);
        irc_handler = func;
}

void av7110_unregister_irc_handler(void (*func)(u32)) 
{
        //dprintk("unregistering %08x\n",func);
        irc_handler = NULL;
}

void run_handlers(unsigned long ircom) 
{
        if (irc_handler != NULL)
                (*irc_handler)((u32) ircom);
}

DECLARE_TASKLET(irtask,run_handlers,0);

void IR_handle(av7110_t *av7110, u32 ircom)
{
        dprintk("av7110: ircommand = %08x\n", ircom);
        irtask.data = (unsigned long) ircom;
        tasklet_schedule(&irtask);
}

/****************************************************************************
 * IRQ handling
 ****************************************************************************/

void CI_handle(av7110_t *av7110, u8 *data, u16 len) 
{
        //CI_out(av7110, data, len);

        if (len<3)
                return;
        switch (data[0]) {
        case CI_MSG_CI_INFO:
                if (data[2]!=1 && data[2]!=2)
                        break;
                switch (data[1]) {
                case 0:
                        av7110->ci_slot[data[2]-1].flags=0;
                        break;
                case 1:
                        av7110->ci_slot[data[2]-1].flags|=CA_CI_MODULE_PRESENT;
                        break;
                case 2:
                        av7110->ci_slot[data[2]-1].flags|=CA_CI_MODULE_READY;
                        break;
                }
                break;
        case CI_SWITCH_PRG_REPLY:
                //av7110->ci_stat=data[1];
                break;
        default:
                break;
        }

}

static inline int
DvbDmxFilterCallback(u8 * buffer1, size_t buffer1_len,
                     u8 * buffer2, size_t buffer2_len,
                     struct dvb_demux_filter *dvbdmxfilter,
                     dmx_success_t success,
                     av7110_t *av7110)
{
        if (!dvbdmxfilter->feed->demux->dmx.frontend)
                return 0;
        if (dvbdmxfilter->feed->demux->dmx.frontend->source==DMX_MEMORY_FE)
                return 0;
        
        switch(dvbdmxfilter->type) {
        case DMX_TYPE_SEC:
                if ((((buffer1[1]<<8)|buffer1[2])&0xfff)+3!=buffer1_len)
                        return 0;
                if (dvbdmxfilter->doneq) {
                        dmx_section_filter_t *filter=&dvbdmxfilter->filter;
                        int i;
                        u8 xor, neq=0;
                        
                        for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
                                xor=filter->filter_value[i]^buffer1[i];
                                neq|=dvbdmxfilter->maskandnotmode[i]&xor;
                        }
                        if (!neq)
                                return 0;
                }
                return dvbdmxfilter->feed->cb.sec(buffer1, buffer1_len,
						  buffer2, buffer2_len,
						  &dvbdmxfilter->filter,
						  DMX_OK); 
        case DMX_TYPE_TS:
                if (!(dvbdmxfilter->feed->ts_type & TS_PACKET)) 
                        return 0;
                if (dvbdmxfilter->feed->ts_type & TS_PAYLOAD_ONLY) 
                        return dvbdmxfilter->feed->cb.ts(buffer1, buffer1_len,
                                                         buffer2, buffer2_len,
                                                         &dvbdmxfilter->feed->feed.ts,
                                                         DMX_OK); 
                else
                        pes_to_ts(buffer1, buffer1_len, 
                                  dvbdmxfilter->feed->pid, 
                                  &av7110->p2t_filter[dvbdmxfilter->index]);
	default:
	        return 0;
        }
}


u8 pshead[0x26] = {
        0x00, 0x00, 0x01, 0xba, 0x5f, 0xff, 0xfe, 0xe6, 
        0xc4, 0x01, 0x01, 0x89, 0xc3, 0xf8, 0x00, 0x00,
        0x01, 0xbb, 0x00, 0x12, 0x80, 0xc4, 0xe1, 0x00,
        0xe1, 0xff, 0xb9, 0xe0, 0xe8, 0xb8, 0xc0, 0x20,
        0xbd, 0xe0, 0x44, 0xbf, 0xe0, 0x02,
};								


static void vpeirq (unsigned long data)
{
        //printk("vpeirq %08x\n", saa7146_read(av7110->saa_mem, PCI_VDP3));
}

#if 0
static void fidbirq(struct saa7146* saa, void *data)
{
        av7110_t *av7110=(av7110_t *) data;
        u8 *mem;
  
        mem=(av7110->tsf ? TS_HEIGHT*TS_WIDTH/2 :0)+(u8 *)av7110->saa->grabbing;
        
        // FIXME: think of something better without busy waiting
        if (av7110->tsf) 
                while (saa7146_read(av7110->saa_mem, PCI_VDP3)>0x20000);
        else
                while (saa7146_read(av7110->saa_mem, PCI_VDP3)<0x17800);

        av7110->tsf^=1;
        saa7146_write(av7110->saa_mem, DD1_INIT, 0x02000600|(av7110->tsf ? 0x40:0x80));
        saa7146_write(av7110->saa_mem, MC2, 
                      (MASK_09 | MASK_25 | MASK_10 | MASK_26));

        // FIXME: use bottom half or tasklet
        if (av7110->feeding && mem[0]==0x47)
                dvb_dmx_swfilter_packets(&av7110->demux, mem, 512);
}
#else
static
void fidbirq (unsigned long data)
{
	struct av7110_s *av7110 = (struct av7110_s*) data;
        u8 *mem=(u8 *)(av7110->saa->grabbing);
        int num;
        u32 dmapos;

        dmapos=saa7146_read(av7110->saa_mem, PCI_VDP3);
        dmapos-=(dmapos%188);

        if (av7110->tsf) {
                mem+=av7110->ttbp;
                if (dmapos<0x20000) {
                        num=1024-av7110->ttbp/188;
                        av7110->ttbp=0;
                } else {
                        num=(dmapos - av7110->ttbp)/188;
                        av7110->ttbp=dmapos;
                }
        } else {
                if (av7110->ttbp>1000*188 && av7110->ttbp<1024*188) {
                        if (av7110->feeding)
                                dvb_dmx_swfilter_packets(&av7110->demux, 
                                                      mem+av7110->ttbp,
                                                      1024- av7110->ttbp / 188);
                }
                num=dmapos/188;
                av7110->ttbp=dmapos;
        }

        av7110->tsf^=1;
        saa7146_write(av7110->saa_mem, DD1_INIT, 0x02000600|(av7110->tsf ? 0x40:0x80));
        saa7146_write(av7110->saa_mem, MC2, 
                      (MASK_09 | MASK_25 | MASK_10 | MASK_26));

        // FIXME: use bottom half or tasklet
        if (av7110->feeding && mem[0]==0x47)
                dvb_dmx_swfilter_packets(&av7110->demux, mem, num);
}
#endif

//#define DEBUG_TIMING
inline static void 
print_time(char *s)
{
#ifdef DEBUG_TIMING
        struct timeval tv;
        do_gettimeofday(&tv);
        printk("%s: %d.%d\n", s, (int)tv.tv_sec, (int)tv.tv_usec);
#endif
}

static void 
ci_get_data(ring_buffer_t *cibuf, u8 *data, int len)
{
        int free, split=0, pread=cibuf->pread;
        
        free=pread-cibuf->pwrite;
        if (free<=0)  
                free+=cibuf->size;
        if (free<=len+2) 
                return;
        cibuf->data[cibuf->pwrite]=(len>>8);   
        cibuf->data[(cibuf->pwrite+1)%cibuf->size]=(len&0xff); 
        cibuf->pwrite=(cibuf->pwrite+2)%cibuf->size;

        if (pread<=cibuf->pwrite)
                split=cibuf->size-cibuf->pwrite;
        if (split && split<len) {
                memcpy(cibuf->data + cibuf->pwrite, data, split);
                memcpy(cibuf->data, data+split, len-split);
        } else 
                memcpy(cibuf->data + cibuf->pwrite, data, len);
        cibuf->pwrite=(cibuf->pwrite+len)%cibuf->size;
        
        wake_up_interruptible(&cibuf->queue);
}

static
void debiirq (unsigned long data)
{
	struct av7110_s *av7110 = (struct av7110_s*) data;
        int type=av7110->debitype;
        int handle=(type>>8)&0x1f;
        
        print_time("debi");
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) & ~MASK_19 );
        saa7146_write(av7110->saa_mem, ISR, MASK_19 );

        if (type==-1) {
                printk("DEBI irq oops\n");
                ARM_ClearMailBox(av7110);
                ARM_ClearIrq(av7110);
                return;
        }
        av7110->debitype=-1;

        switch (type&0xff) {

        case DATA_TS_RECORD:
                dvb_dmx_swfilter_packets(&av7110->demux, 
                                      (const u8 *)av7110->debi_virt, 
                                      av7110->debilen/188);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                break;

        case DATA_PES_RECORD:
                if (av7110->demux.recording) 
                        record_cb(&av7110->p2t[handle], 
                                  (u8 *)av7110->debi_virt,
                                  av7110->debilen);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_IPMPE:
        case DATA_FSECTION:
        case DATA_PIPING:
                if (av7110->handle2filter[handle]) 
                        DvbDmxFilterCallback((u8 *)av7110->debi_virt, 
                                             av7110->debilen, 0, 0, 
                                             av7110->handle2filter[handle], 
                                             DMX_OK, av7110); 
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_GET:
        {
                u8 *data=av7110->debi_virt;

                if ((data[0]<2) && data[2]==0xff) {
                        int flags=0;
                        if (data[5]>0) 
                                flags|=CA_CI_MODULE_PRESENT;
                        if (data[5]>5) 
                                flags|=CA_CI_MODULE_READY;
                        av7110->ci_slot[data[0]].flags=flags;
                } else
                        ci_get_data(&av7110->ci_rbuffer, 
                                    av7110->debi_virt, 
                                    av7110->debilen);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;
        }

        case DATA_COMMON_INTERFACE:
                CI_handle(av7110, (u8 *)av7110->debi_virt, av7110->debilen);
#if 0
        {
                int i;

                printk("av7110%d: ", av7110->num);
                printk("%02x ", *(u8 *)av7110->debi_virt);
                printk("%02x ", *(1+(u8 *)av7110->debi_virt));
                for (i=2; i<av7110->debilen; i++)
                  printk("%02x ", (*(i+(unsigned char *)av7110->debi_virt)));
                for (i=2; i<av7110->debilen; i++)
                  printk("%c", chtrans(*(i+(unsigned char *)av7110->debi_virt)));

                printk("\n");
        }
#endif
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
                ((s8*)av7110->debi_virt)[Reserved_SIZE-1]=0;
                printk("%s\n", (s8 *)av7110->debi_virt);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_PUT:
        case DATA_MPEG_PLAY:
        case DATA_BMP_LOAD:
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;
        default:
                break;
        }
        spin_lock(&av7110->debilock);
        ARM_ClearMailBox(av7110);
        spin_unlock(&av7110->debilock);
}

static int
pes_play(void *dest, ring_buffer_t *buf, int dlen)
{
        int len, split=0;
        u32 sync;
        u16 blen;

        dprintk ("function : %s\n", __FUNCTION__);
        if (!dlen) {
                wake_up(&buf->queue);
                return -1;
        }
        while (1) {
                if ((len=ring_buffer_avail(buf)) < 6)
                        return -1;
                sync=(buf->data[buf->pread])<<24;
                sync|=(buf->data[(buf->pread+1)%buf->size]<<16);
                sync|=(buf->data[(buf->pread+2)%buf->size]<<8);
                sync|=buf->data[(buf->pread+3)%buf->size];
                
                if (((sync&~0x1f)==0x000001e0) ||
                    ((sync&~0x1f)==0x000001c0) ||
                    (sync==0x000001bd))
                        break;
                printk("resync\n");
                buf->pread=(buf->pread+1)%buf->size;
        }
        blen=(buf->data[(buf->pread+4)%buf->size]<<8);
        blen|=buf->data[(buf->pread+5)%buf->size];
        blen+=6;
        if (len<blen || blen > dlen) {
                printk("buffer empty\n");
                wake_up(&buf->queue);
                return -1;
        }
/*        if (blen>2048) {
                buf->pread=(buf->pread+blen)%buf->size;
                printk("packet too large\n");
                return -1;
        }
*/
        len=blen;
        if (buf->pread + len > buf->size)
                split=buf->size-buf->pread;
        if (split>0) {
                memcpy(dest, buf->data+buf->pread, split);
                buf->pread=0;
                len-=split;
        }
        memcpy(split + dest, 
               buf->data + buf->pread, len);
        buf->pread = (buf->pread +len)%buf->size;
        
        dprintk ("function: %s pread=%08x pwrite=%08x\n", __FUNCTION__,
                buf->pread, buf->pwrite);
        wake_up(&buf->queue);
        return blen;
}

static
void gpioirq (unsigned long data)
{
	struct av7110_s *av7110 = (struct av7110_s*) data;
        u32 rxbuf, txbuf;
        int len;
        
        //printk("GPIO0 irq\n");        

        if (av7110->debitype !=-1)
                printk("GPIO0 irq oops\n");
       
        spin_lock(&av7110->debilock);

	ARM_ClearIrq(av7110);

        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) & ~MASK_19 );
        saa7146_write(av7110->saa_mem, ISR, MASK_19 );

        av7110->debitype = irdebi(av7110, DEBINOSWAP, IRQ_STATE, 0, 2);
        av7110->debilen  = irdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
        av7110->debibuf  = 0;
        rxbuf=irdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
        txbuf=irdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
        len=(av7110->debilen+3)&(~3);

        dprintk("GPIO0 irq %d %d\n", av7110->debitype, av7110->debilen);
        print_time("gpio");

        dprintk("GPIO0 irq %02x\n", av7110->debitype&0xff);        
        switch (av7110->debitype&0xff) {

        case DATA_TS_PLAY:
        case DATA_PES_PLAY:
                break;

        case DATA_CI_PUT:
        {
                int avail, split=0, pwrite;
                ring_buffer_t *cibuf=&av7110->ci_wbuffer;

                pwrite=cibuf->pwrite;
                avail=pwrite-cibuf->pread;
                if (avail<0) 
                        avail+=cibuf->size;
                if (avail<=2) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                len=(cibuf->data[cibuf->pread]<<8);
                len|=cibuf->data[(cibuf->pread+1)%cibuf->size];
                if (avail<len+2) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                cibuf->pread=(cibuf->pread+2)%cibuf->size;
                
                if (pwrite<cibuf->pread)
                        split=cibuf->size-cibuf->pread;
                if (split && split<len) {
                        int todo=len-split;
                        memcpy(av7110->debi_virt, cibuf->data+cibuf->pread, split);
                        memcpy(av7110->debi_virt+split, cibuf->data, todo);
                } else
                        memcpy(av7110->debi_virt, cibuf->data+cibuf->pread, len);
                cibuf->pread=(cibuf->pread+len)%cibuf->size;
                wake_up(&cibuf->queue);
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                wait_for_debi_done(av7110);
                saa7146_write(av7110->saa_mem, IER, 
                              saa7146_read(av7110->saa_mem, IER) | MASK_19 );
                if (len<5) len=5; /* we want a real DEBI DMA */
                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;
        }

        case DATA_MPEG_PLAY:
                if (!av7110->playing) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                }
                len=0;
                if (av7110->debitype&0x100) {
                        spin_lock(&av7110->aout.lock);
                        len=pes_play(av7110->debi_virt, &av7110->aout, 2048);
                        spin_unlock(&av7110->aout.lock);
                }
                if (len<=0 && (av7110->debitype&0x200)
                        &&av7110->videostate.play_state!=VIDEO_FREEZED) {
                        spin_lock(&av7110->avout.lock);
                        len=pes_play(av7110->debi_virt, &av7110->avout, 2048);
                        spin_unlock(&av7110->avout.lock);
                }
                if (len<=0) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                dprintk("GPIO0 PES_PLAY len=%04x\n", len);        
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                wait_for_debi_done(av7110);
                saa7146_write(av7110->saa_mem, IER, 
                              saa7146_read(av7110->saa_mem, IER) | MASK_19 );

                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_BMP_LOAD:
                len=av7110->debilen;
                if (!len) {
                        av7110->bmp_state=BMP_LOADED;
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        wake_up(&av7110->bmpq);
                        break;
                }
                if (len>av7110->bmplen)
                        len=av7110->bmplen;
                if (len>2*1024)
                        len=2*1024;
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                memcpy(av7110->debi_virt, av7110->bmpbuf+av7110->bmpp, len);
                av7110->bmpp+=len;
                av7110->bmplen-=len;
                wait_for_debi_done(av7110);
                saa7146_write(av7110->saa_mem, IER, 
                              saa7146_read(av7110->saa_mem, IER) | MASK_19 );
                if (len<5) len=5; /* we want a real DEBI DMA */
                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_GET:
        case DATA_COMMON_INTERFACE:
        case DATA_FSECTION:
        case DATA_IPMPE:
        case DATA_PIPING:
                if (!len || len>4*1024) {
                        iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                        break;
                }                  /* yes, fall through */
        case DATA_TS_RECORD:
        case DATA_PES_RECORD:
                saa7146_write(av7110->saa_mem, IER, 
                              saa7146_read(av7110->saa_mem, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, DPRAM_BASE+rxbuf, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
                if (!len || len>0xff) {
                        iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                        break;
                }
                saa7146_write(av7110->saa_mem, IER, 
                              saa7146_read(av7110->saa_mem, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, Reserved, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_IRCOMMAND: 
                IR_handle(av7110, 
                          swahw32(irdebi(av7110, DEBINOSWAP, Reserved, 0, 4)));
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                break;

        default:
                printk("gpioirq unknown type=%d len=%d\n", 
                       av7110->debitype, av7110->debilen);
                break;
        }      
        ARM_ClearMailBox(av7110);
        av7110->debitype=-1;
        spin_unlock(&av7110->debilock);
        dprintk("GPIO0 irq exit 0\n");        
}


/****************************************************************************
 * DEBI command polling 
 ****************************************************************************/


static int OutCommand(av7110_t *av7110, u16* buf, int length)
{
        int i;
        u32	start;

        if (!av7110->arm_ready)
                return -1;

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2 ) )
        {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_FREE) {
                        printk("outcommand error 1\n");
                        //arm_error(av7110);
                        return -1;
                }
        }

#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) ) 
        {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
                        printk("outcommand error 2\n");
                        //arm_error(av7110);
                        return -1;
                }
        }
#endif

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2) & OSDQFull )
        {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_OSD)
                {
                        printk("outcommand error 3\n");
                        //arm_error(av7110);
                        return -1;
                }
        }
        for (i=2; i<length; i++)
                wdebi(av7110, DEBINOSWAP, COMMAND + 2*i, (u32) buf[i], 2);

        if (length)
                wdebi(av7110, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
        else
                wdebi(av7110, DEBINOSWAP, COMMAND + 2, 0, 2);

        wdebi(av7110, DEBINOSWAP, COMMAND, (u32) buf[0], 2);

        return 0;
}

inline static int 
SOutCommand(av7110_t *av7110, u16* buf, int length)
{
        int ret;
        
        if (!av7110->arm_ready)
                return -1;

        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        ret=OutCommand(av7110, buf, length);
        up(&av7110->dcomlock);
        return ret;
}


static int outcom(av7110_t *av7110, int type, int com, int num, ...)
{
	va_list args;
        u16 buf[num+2];
        int i;

        buf[0]=(( type << 8 ) | com);
        buf[1]=num;

        if (num) {
                va_start(args, num);
                for (i=0; i<num; i++)
                        buf[i+2]=va_arg(args, u32);
                va_end(args);
        }

        return SOutCommand(av7110, buf, num+2);
}

int SendCICommand(av7110_t *av7110, u8 subcom, u8 *Params, u8 ParamLen)
{
        int i;
        u16 CommandBuffer[18] = { ((COMTYPE_COMMON_IF << 8) + subcom),
                                  16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        
	for(i=0; (i<ParamLen)&&(i<32); i++)	
	{
		if(i%2 == 0)
			CommandBuffer[(i/2)+2] = (u16)(Params[i]) << 8;
		else
			CommandBuffer[(i/2)+2] |= Params[i];
	}

        return SOutCommand(av7110, CommandBuffer, 18);
}


static int CommandRequest(av7110_t *av7110, u16 *Buff, int length, u16 *buf, int n)
{
	int err;
        s16 i;
        u32 start;
       
        if (!av7110->arm_ready)
                return -1;

        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        if ((err = OutCommand(av7110, Buff, length)) < 0) {
		up(&av7110->dcomlock);
		return err;
	}
        
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2) ) 
        {
#ifdef _NOHANDSHAKE
                ddelay(1);
#endif
                if ((jiffies - start) > ARM_WAIT_FREE) {
                        printk("commandrequest error 1\n");
                        up(&av7110->dcomlock);
                        //arm_error(av7110);
                        return -1;
                }
        }
        
#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) ) {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
                        printk("commandrequest error 2\n");
                        up(&av7110->dcomlock);
                        //arm_error(av7110);
                        return -1;
                }
        }
#endif

        for (i=0; i<n; i++)
                buf[i] = rdebi(av7110, DEBINOSWAP, COM_BUFF + 2*i, 0, 2);
        
	up(&av7110->dcomlock);
        return 0;
}


static inline int 
RequestParameter(av7110_t *av7110, u16 tag, u16* Buff, s16 length)
{
        return CommandRequest(av7110, &tag, 0, Buff, length);
}


/****************************************************************************
 * Firmware commands 
 ****************************************************************************/


inline static int 
SendDAC(av7110_t *av7110, u8 addr, u8 data)
{
        return outcom(av7110, COMTYPE_AUDIODAC, AudioDAC, 2, addr, data);
}

static int
SetVolume(av7110_t *av7110, int volleft, int volright)
{
        int err;
        
        switch (av7110->adac_type) {
        case DVB_ADAC_TI:
                volleft=(volleft*256)/946;
                volright=(volright*256)/946;
                if (volleft > 0x45)
                        volleft=0x45;
                if (volright > 0x45)
                        volright=0x45;
                err=SendDAC(av7110, 3, 0x80 + volleft);
                if (err)
                        return err;
                return SendDAC(av7110, 4, volright);
                
        case DVB_ADAC_CRYSTAL:
                volleft=127-volleft/2;
                volright=127-volright/2;
                i2c_writereg(av7110, 0x20, 0x03, volleft);
                i2c_writereg(av7110, 0x20, 0x04, volright);
                return 0;
        }
        return 0;
}

#ifdef CONFIG_DVB_AV7110_OSD

inline static int ResetBlend(av7110_t *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, SetNonBlend, 1, windownr);
}

inline static int SetColorBlend(av7110_t *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, SetCBlend, 1, windownr); 
}

inline static int SetWindowBlend(av7110_t *av7110, u8 windownr, u8 blending)
{
        return outcom(av7110, COMTYPE_OSD, SetWBlend, 2, windownr, blending); 
}

inline static int SetBlend_(av7110_t *av7110, u8 windownr,
                     OSDPALTYPE colordepth, u16 index, u8 blending)
{
        return outcom(av7110, COMTYPE_OSD, SetBlend, 4,
                      windownr, colordepth, index, blending);
} 

inline static int SetColor_(av7110_t *av7110, u8 windownr,
                     OSDPALTYPE colordepth, u16 index, u16 colorhi, u16 colorlo)
{
        return outcom(av7110, COMTYPE_OSD, SetColor, 5,
                      windownr, colordepth, index, colorhi, colorlo);
} 

inline static int BringToTop(av7110_t *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WTop, 1, windownr);
} 

inline static int SetFont(av7110_t *av7110, u8 windownr, u8 fontsize,
                   u16 colorfg, u16 colorbg)
{
        return outcom(av7110, COMTYPE_OSD, Set_Font, 4,
                      windownr, fontsize, colorfg, colorbg);
} 

static int FlushText(av7110_t *av7110)
{
        u32 start;

        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2 ) ) {
                ddelay(1); 
                if ((jiffies - start) > ARM_WAIT_OSD) {
                        printk("outtext error\n");
                        up(&av7110->dcomlock);
                        //arm_error(av7110);
                        return -1;
                }
        }
        up(&av7110->dcomlock);
        return 0;
}

static int WriteText(av7110_t *av7110, u8 win, u16 x, u16 y, u8* buf)
{
        int i, ret;
        u32 start;
        int length=strlen(buf)+1;
        u16 cbuf[5] = { (COMTYPE_OSD<<8) + DText, 3, win, x, y };
        
        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2 ) ) {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_OSD) {
                        printk("outtext error\n");
                        up(&av7110->dcomlock);
                        //arm_error(av7110);
                        return -1;
                }
        }
#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) ) {
                ddelay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
                        printk("outtext error\n");
                        up(&av7110->dcomlock);
                        //arm_error(av7110);
                        return -1;
                }
        }
#endif
        for (i=0; i<length/2; i++)
                wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i*2, 
                      swab16(*(u16 *)(buf+2*i)), 2);
        if (length&1)
                wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i*2, 0, 2);
        ret=OutCommand(av7110, cbuf, 5);
        up(&av7110->dcomlock);
        return ret;
}

inline static int DrawLine(av7110_t *av7110, u8 windownr, 
                    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
        return outcom(av7110, COMTYPE_OSD, DLine, 6,
                      windownr, x, y, dx, dy, color);
} 

inline static int DrawBlock(av7110_t *av7110, u8 windownr, 
                    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
        return outcom(av7110, COMTYPE_OSD, DBox, 6,
                      windownr, x, y, dx, dy, color);
} 

inline static int HideWindow(av7110_t *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WHide, 1, windownr);
} 

inline static int MoveWindowRel(av7110_t *av7110, u8 windownr, u16 x, u16 y)
{
        return outcom(av7110, COMTYPE_OSD, WMoveD, 3, windownr, x, y);
} 

inline static int MoveWindowAbs(av7110_t *av7110, u8 windownr, u16 x, u16 y)
{
        return outcom(av7110, COMTYPE_OSD, WMoveA, 3, windownr, x, y);
} 

inline static int DestroyOSDWindow(av7110_t *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WDestroy, 1, windownr);
} 

#if 0
static void DestroyOSDWindows(av7110_t *av7110)
{
        int i;

        for (i=1; i<7; i++)
                outcom(av7110, COMTYPE_OSD, WDestroy, 1, i);
} 
#endif

static inline int 
CreateOSDWindow(av7110_t *av7110, u8 windownr,
                           DISPTYPE disptype, u16 width, u16 height)
{
        return outcom(av7110, COMTYPE_OSD, WCreate, 4,
                      windownr, disptype, width, height);
} 


static OSDPALTYPE bpp2pal[8]={Pal1Bit, Pal2Bit, 0, Pal4Bit, 0, 0, 0, Pal8Bit}; 
static DISPTYPE   bpp2bit[8]={BITMAP1, BITMAP2, 0, BITMAP4, 0, 0, 0, BITMAP8}; 

static inline int 
LoadBitmap(av7110_t *av7110, u16 format, u16 dx, u16 dy, int inc, u8* data)
{
        int bpp;
        int i;
        int d, delta; 
        u8 c;
        DECLARE_WAITQUEUE(wait, current);
        
        if (av7110->bmp_state==BMP_LOADING) {
                add_wait_queue(&av7110->bmpq, &wait);
                while (1) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        if (av7110->bmp_state!=BMP_LOADING
                            || signal_pending(current))
                                break;
                        schedule();
                }
                current->state=TASK_RUNNING;
                remove_wait_queue(&av7110->bmpq, &wait);
        }
        if (av7110->bmp_state==BMP_LOADING)
                return -1;
        av7110->bmp_state=BMP_LOADING;
        if      (format==BITMAP8) { bpp=8; delta = 1; } 
        else if (format==BITMAP4) { bpp=4; delta = 2; }
        else if (format==BITMAP2) { bpp=2; delta = 4; }
        else if (format==BITMAP1) { bpp=1; delta = 8; }
        else {
                av7110->bmp_state=BMP_NONE;
                return -1;
        }
        av7110->bmplen= ((dx*dy*bpp+7)&~7)/8; 
        av7110->bmpp=0;
        if (av7110->bmplen>32768) {
                av7110->bmp_state=BMP_NONE;
                return -1;
        }
        for (i=0; i<dy; i++) {
                if (copy_from_user(av7110->bmpbuf+1024+i*dx, data+i*inc, dx)) { 
                        av7110->bmp_state=BMP_NONE;
                        return -1;
                }
        }
        if (format != BITMAP8) {
                for (i=0; i<dx*dy/delta; i++) {
                        c = ((u8 *)av7110->bmpbuf)[1024+i*delta+delta-1];
                        for (d=delta-2; d>=0; d--) {
                                c |= (((u8 *)av7110->bmpbuf)[1024+i*delta+d] 
                                      << ((delta-d-1)*bpp));
                                ((u8 *)av7110->bmpbuf)[1024+i] = c;
                        }
                }
        }
        av7110->bmplen+=1024;
        return outcom(av7110, COMTYPE_OSD, LoadBmp, 3, format, dx, dy);
} 

static int 
BlitBitmap(av7110_t *av7110, u16 win, u16 x, u16 y, u16 trans)
{
        DECLARE_WAITQUEUE(wait, current);
        
        if (av7110->bmp_state==BMP_NONE)
                return -1;
        if (av7110->bmp_state==BMP_LOADING) {
                add_wait_queue(&av7110->bmpq, &wait);
                while (1) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        if (av7110->bmp_state!=BMP_LOADING
                            || signal_pending(current))
                                break;
                        schedule();
                }
                current->state=TASK_RUNNING;
                remove_wait_queue(&av7110->bmpq, &wait);
        }
        if (av7110->bmp_state==BMP_LOADED)
                return outcom(av7110, COMTYPE_OSD, BlitBmp, 4, win, x, y, trans);
        return -1;
} 

static inline int 
ReleaseBitmap(av7110_t *av7110)
{
        if (av7110->bmp_state!=BMP_LOADED)
                return -1;
        av7110->bmp_state=BMP_NONE;
        return outcom(av7110, COMTYPE_OSD, ReleaseBmp, 0);
} 

static u32 RGB2YUV(u16 R, u16 G, u16 B)
{
        u16 y, u, v;
        u16 Y, Cr, Cb;

        y = R * 77 + G * 150 + B * 29;  // Luma=0.299R+0.587G+0.114B 0..65535
        u = 2048+B * 8 -(y>>5);    // Cr 0..4095
        v = 2048+R * 8 -(y>>5);    // Cb 0..4095

        Y=y/256;
        Cb=u/16;
        Cr=v/16;

        return Cr|(Cb<<16)|(Y<<8);
}

static void
OSDSetColor(av7110_t *av7110, u8 color, u8 r, u8 g, u8 b, u8 blend)
{
        u16 ch, cl;
        u32 yuv;

        yuv=blend ? RGB2YUV(r,g,b) : 0;
        cl=(yuv&0xffff);
        ch=((yuv>>16)&0xffff);
        SetColor_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
                  color, ch, cl);
        SetBlend_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
                  color, ((blend>>4)&0x0f));
}

static int
OSDSetBlock(av7110_t *av7110, int x0, int y0, int x1, int y1, int inc, u8 *data)
{
        uint w, h, bpp, bpl, size, lpb, bnum, brest;
        int i;

        w=x1-x0+1; h=y1-y0+1;
        if (inc<=0)
                inc=w; 
        if (w<=0 || w>720 || h<=0 || h>576) 
                return -1;
        bpp=av7110->osdbpp[av7110->osdwin]+1; 
        bpl=((w*bpp+7)&~7)/8; 
        size=h*bpl;
        lpb=(32*1024)/bpl; 
        bnum=size/(lpb*bpl);
        brest=size-bnum*lpb*bpl;

        for (i=0; i<bnum; i++) {
                LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]], w, lpb, inc, data); 
                BlitBitmap(av7110, av7110->osdwin, x0, y0+i*lpb, 0);
                data+=lpb*inc; 
        }
        if (brest) {
                LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]], w, brest/bpl, inc, data); 
                BlitBitmap(av7110, av7110->osdwin, x0, y0+bnum*lpb, 0);
        }
        ReleaseBitmap(av7110);
        return 0;
}

static int 
OSD_DrawCommand(av7110_t *av7110, osd_cmd_t *dc)
{
        switch (dc->cmd) {
        case OSD_Close:
                DestroyOSDWindow(av7110, av7110->osdwin);
                return 0;
        case OSD_Open:
                av7110->osdbpp[av7110->osdwin]=(dc->color-1)&7;
                CreateOSDWindow(av7110, av7110->osdwin, bpp2bit[av7110->osdbpp[av7110->osdwin]],
                                dc->x1-dc->x0+1, dc->y1-dc->y0+1);
                if (!dc->data) {
                        MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
                        SetColorBlend(av7110, av7110->osdwin);
                }
                return 0;
        case OSD_Show:
                MoveWindowRel(av7110, av7110->osdwin, 0, 0);
                return 0;
        case OSD_Hide:
                HideWindow(av7110, av7110->osdwin);
                return 0;
        case OSD_Clear:
                DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, 0);
                return 0;
        case OSD_Fill:
                DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, dc->color);
                return 0;
        case OSD_SetColor:
                OSDSetColor(av7110, dc->color, dc->x0, dc->y0, dc->x1, dc->y1); 
                return 0;
        case OSD_SetPalette:
        {      
                int i, len=dc->x0-dc->color+1;
                u8 *colors=(u8 *)dc->data;

                for (i=0; i<len; i++)
                        OSDSetColor(av7110, dc->color+i,
                                    colors[i*4]  , colors[i*4+1],
                                    colors[i*4+2], colors[i*4+3]);
                return 0;
        }
        case OSD_SetTrans: 
                return 0;
        case OSD_SetPixel:
                DrawLine(av7110, av7110->osdwin,
                         dc->x0, dc->y0, 0, 0,
                         dc->color);
                return 0;
        case OSD_GetPixel: 
                return 0;

        case OSD_SetRow:   
                dc->y1=dc->y0;
        case OSD_SetBlock:
                OSDSetBlock(av7110, dc->x0, dc->y0, dc->x1, dc->y1, dc->color, dc->data);
                return 0;

        case OSD_FillRow:
                DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
                          dc->x1-dc->x0+1, dc->y1,
                          dc->color);
                return 0;
        case OSD_FillBlock:
                DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
                          dc->x1-dc->x0+1, dc->y1-dc->y0+1,
                          dc->color);
                return 0;
        case OSD_Line:
                DrawLine(av7110, av7110->osdwin,
                         dc->x0, dc->y0, dc->x1-dc->x0, dc->y1-dc->y0,
                         dc->color);
                return 0;
        case OSD_Query: 
                return 0;
        case OSD_Test:
                return 0;
        case OSD_Text:
        {
                char textbuf[240];
                
                if (strncpy_from_user(textbuf, dc->data, 240)<0)
                        return -EFAULT;
                textbuf[239]=0;
                if (dc->x1>3) 
                        dc->x1=3;
                SetFont(av7110, av7110->osdwin, dc->x1,
                        (u16) (dc->color&0xffff), (u16) (dc->color>>16));
                FlushText(av7110);
                WriteText(av7110, av7110->osdwin, dc->x0, dc->y0, textbuf);
                return 0;
        }
        case OSD_SetWindow:
                if (dc->x0<1 || dc->x0>7)
                        return -EINVAL;
                av7110->osdwin=dc->x0;
                return 0;
        case OSD_MoveWindow:
                MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
                SetColorBlend(av7110, av7110->osdwin);
                return 0;
        default:
                return -EINVAL;
        }
}
#endif /* CONFIG_DVB_AV7110_OSD */


/* get version of the firmware ROM, RTSL, video ucode and ARM application  */

static void 
firmversion(av7110_t *av7110)
{
        u16 buf[20];

        u16 tag = ((COMTYPE_REQUEST << 8) + ReqVersion);
        
        RequestParameter(av7110, tag, buf, 16);
        
        av7110->arm_fw=(buf[0] << 16) + buf[1];
        av7110->arm_rtsl=(buf[2] << 16) + buf[3];
        av7110->arm_vid=(buf[4] << 16) + buf[5];
        av7110->arm_app=(buf[6] << 16) + buf[7];
        av7110->avtype=(buf[8] << 16) + buf[9];

        printk ("DVB: AV711%d(%d) - firm %08x, rtsl %08x, vid %08x, app %08x\n",
		av7110->avtype, av7110->saa->dvb_adapter->num, av7110->arm_fw, 
                av7110->arm_rtsl, av7110->arm_vid, av7110->arm_app);

        return;
}

static int 
waitdebi(av7110_t *av7110, int adr, int state)
{
        int k;
        
        for (k=0; k<100; k++, udelay(500)) {
                if (irdebi(av7110, DEBINOSWAP, adr, 0, 2) == state) 
                        return 0;
        }
        return -1;
}


static int 
load_dram(av7110_t *av7110, u32 *data, int len)
{
        int i;
        int blocks, rest;
        u32 base, bootblock=BOOT_BLOCK;
        
        blocks=len/BOOT_MAX_SIZE;
        rest=len % BOOT_MAX_SIZE;
        base=DRAM_START_CODE;
        
        for (i=0; i<blocks; i++) {
                if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                        return -1;
                dprintk("Writing DRAM block %d\n",i);
                iwdebi(av7110, DEBISWAB, bootblock,
                       i*(BOOT_MAX_SIZE)+(u32)data,
                       BOOT_MAX_SIZE);
                bootblock^=0x1400;
                iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
                iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, BOOT_MAX_SIZE, 2);
                iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
                base+=BOOT_MAX_SIZE;
        }
        
        if (rest > 0) {
                if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                        return -1;
                if (rest>4)
                        iwdebi(av7110, DEBISWAB, bootblock, i*(BOOT_MAX_SIZE)+(u32)data, rest);
                else
                        iwdebi(av7110, DEBISWAB, bootblock, i*(BOOT_MAX_SIZE)-4+(u32)data, rest+4);
                
                iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
                iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, rest, 2);
                iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        }
        if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                return -1;
        iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, 0, 2);
        iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BOOT_COMPLETE) < 0)
                return -1;
        return 0;
}


static u8 
bootcode[] = {
        0xea, 0x00, 0x00, 0x0e, 0xe1, 0xb0, 0xf0, 0x0e, /* 0x0000 */
        0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x04,
        0xe2, 0x5e, 0xf0, 0x08, 0xe2, 0x5e, 0xf0, 0x04,
        0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x04,
        0x2c, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x0c,
        0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x34,
        0x00, 0x00, 0x00, 0x00, 0xa5, 0xa5, 0x5a, 0x5a,
        0x00, 0x1f, 0x15, 0x55, 0x00, 0x00, 0x00, 0x09,
        0xe5, 0x9f, 0xd0, 0x5c, 0xe5, 0x9f, 0x40, 0x54, /* 0x0040 */
        0xe3, 0xa0, 0x00, 0x00, 0xe5, 0x84, 0x00, 0x00,
        0xe5, 0x84, 0x00, 0x04, 0xe1, 0xd4, 0x10, 0xb0,
        0xe3, 0x51, 0x00, 0x00, 0x0a, 0xff, 0xff, 0xfc,
        0xe1, 0xa0, 0x10, 0x0d, 0xe5, 0x94, 0x30, 0x04,
        0xe1, 0xd4, 0x20, 0xb2, 0xe2, 0x82, 0x20, 0x3f,
        0xe1, 0xb0, 0x23, 0x22, 0x03, 0xa0, 0x00, 0x02,
        0xe1, 0xc4, 0x00, 0xb0, 0x0a, 0xff, 0xff, 0xf4,
        0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0, /* 0x0080 */
        0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0,
        0xe2, 0x52, 0x20, 0x01, 0x1a, 0xff, 0xff, 0xf9,
        0xe2, 0x2d, 0xdb, 0x05, 0xea, 0xff, 0xff, 0xec,
        0x2c, 0x00, 0x03, 0xf8, 0x2c, 0x00, 0x04, 0x00,
};

#include "av7110_firm.h"

static int 
bootarm(av7110_t *av7110)
{
        u32 ret;
        int i;

        setgpio(av7110, RESET_LINE, GPIO_OUTLO);

        /* Disable DEBI and GPIO irq */
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) & 
                      ~(MASK_19 | MASK_03));
        saa7146_write(av7110->saa_mem, ISR, (MASK_19 | MASK_03));

        /* enable DEBI */
        saa7146_write(av7110->saa_mem, MC1, 0x08800880);
        saa7146_write(av7110->saa_mem, DD1_STREAM_B, 0x00000000);
        saa7146_write(av7110->saa_mem, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
        
        /* test DEBI */
        iwdebi(av7110, DEBISWAP, DPRAM_BASE, 0x76543210, 4);
        if ((ret=irdebi(av7110, DEBINOSWAP, DPRAM_BASE, 0, 4))!=0x10325476) {
                printk("dvb: debi test in bootarm() failed: "
                       "%08x != %08x\n", ret, 0x10325476);;
                return -1;
        }
        for (i=0; i<8192; i+=4) 
                iwdebi(av7110, DEBISWAP, DPRAM_BASE+i, 0x00, 4);
        dprintk("bootarm: debi test OK\n");;

        /* boot */
        dprintk("bootarm: load boot code\n");

        setgpio(av7110, ARM_IRQ_LINE, GPIO_IRQLO);
        //setgpio(av7110, DEBI_DONE_LINE, GPIO_INPUT);
        //setgpio(av7110, 3, GPIO_INPUT);

        iwdebi(av7110, DEBISWAB, DPRAM_BASE, (u32) bootcode, sizeof(bootcode));
        iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        
        wait_for_debi_done(av7110);
        setgpio(av7110, RESET_LINE, GPIO_OUTHI);
        current->state=TASK_INTERRUPTIBLE;
        schedule_timeout(HZ);
        
        dprintk("bootarm: load dram code\n");

	if (load_dram(av7110, (u32 *)Root, sizeof(Root))<0)
		return -1;

	setgpio(av7110, RESET_LINE, GPIO_OUTLO);
        mdelay(1);
        
        dprintk("bootarm: load dpram code\n");

	iwdebi(av7110, DEBISWAB, DPRAM_BASE, (u32) Dpram, sizeof(Dpram));

	wait_for_debi_done(av7110);

        setgpio(av7110, RESET_LINE, GPIO_OUTHI);
        mdelay(800);

        //ARM_ClearIrq(av7110); 
        ARM_ResetMailBox(av7110); 
        saa7146_write(av7110->saa_mem, ISR, (MASK_19 | MASK_03));
        saa7146_write(av7110->saa_mem, IER, 
                      saa7146_read(av7110->saa_mem, IER) | MASK_03 );

        av7110->arm_errors=0;
        av7110->arm_ready=1;
        return 0;
}

static inline int
SetPIDs(av7110_t *av7110, u16 vpid, u16 apid, u16 ttpid, 
        u16 subpid, u16 pcrpid)
{
	if (vpid == 0x1fff || apid == 0x1fff ||
	    ttpid == 0x1fff || subpid == 0x1fff || pcrpid == 0x1fff)
		vpid = apid = ttpid = subpid = pcrpid = 0;

        return outcom(av7110, COMTYPE_PIDFILTER, MultiPID, 5, 
                      pcrpid, vpid, apid, ttpid, subpid);
}

static void
ChangePIDs(av7110_t *av7110, u16 vpid, u16 apid, u16 ttpid, 
        u16 subpid, u16 pcrpid)
{
        if (down_interruptible(&av7110->pid_mutex))
		return;

        if (!(vpid&0x8000))  av7110->pids[DMX_PES_VIDEO]=vpid;
        if (!(apid&0x8000))  av7110->pids[DMX_PES_AUDIO]=apid;
        if (!(ttpid&0x8000)) av7110->pids[DMX_PES_TELETEXT]=ttpid;
        if (!(pcrpid&0x8000)) av7110->pids[DMX_PES_PCR]=pcrpid;

        av7110->pids[DMX_PES_SUBTITLE]=0;

        if (av7110->fe_synced) 
                SetPIDs(av7110, vpid, apid, ttpid, subpid, pcrpid);

        up(&av7110->pid_mutex);
}


static void 
SetMode(av7110_t *av7110, int mode)
{
        outcom(av7110, COMTYPE_ENCODER, LoadVidCode, 1, mode);
        
        if (!av7110->playing) {
                ChangePIDs(av7110, av7110->pids[DMX_PES_VIDEO], 
                           av7110->pids[DMX_PES_AUDIO], 
                           av7110->pids[DMX_PES_TELETEXT],  
                           0, av7110->pids[DMX_PES_PCR]);
                outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);
        }
}

inline static void 
TestMode(av7110_t *av7110, int mode)
{
        outcom(av7110, COMTYPE_ENCODER, SetTestMode, 1, mode);
}

inline static void 
VidMode(av7110_t *av7110, int mode)
{
        outcom(av7110, COMTYPE_ENCODER, SetVidMode, 1, mode);
}
           

static int inline
vidcom(av7110_t *av7110, u32 com, u32 arg)
{
        return outcom(av7110, 0x80, 0x02, 4, 
                      (com>>16), (com&0xffff), 
                      (arg>>16), (arg&0xffff));
}

static int inline
audcom(av7110_t *av7110, u32 com)
{
        return outcom(av7110, 0x80, 0x03, 4, 
                      (com>>16), (com&0xffff));
}

inline static void 
Set22K(av7110_t *av7110, int state)
{
        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS)
                outcom(av7110, COMTYPE_AUDIODAC, (state ? ON22K : OFF22K), 0);
        if (av7110->saa->card_type==DVB_CARD_TT_BUDGET)
                setgpio(av7110, 3, (state ? GPIO_OUTHI : GPIO_OUTLO));
}


/* Diseqc functions only for TT Budget card */
/* taken from the Skyvision DVB driver by 
   Ralph Metzler <rjkm@metzlerbros.de> */


inline static void 
DiseqcSendBit(av7110_t *av7110, int data)
{
        setgpio(av7110, 3, GPIO_OUTHI);
        udelay(data ? 500 : 1000);
        setgpio(av7110, 3, GPIO_OUTLO);
        udelay(data ? 1000 : 500);
}

static void 
DiseqcSendByte(av7110_t *av7110, int data)
{
        int i, par=1, d;

        for (i=7; i>=0; i--) 
        {
                d=(data>>i)&1;
                par^=d;
                DiseqcSendBit(av7110, d);
        }
        DiseqcSendBit(av7110, par);
}

inline static int
SendDiSEqCMsg(av7110_t *av7110, int len, u8 *msg, int burst)
{
        int i;

        switch (av7110->saa->card_type) {
        case DVB_CARD_TT_SIEMENS:
        {
                u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) + SendDiSEqC),
                                16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                
                if (len>10)
                        len=10;
                buf[1] = len+2;
                buf[2] = len;
                
                if (burst!=-1)
                  buf[3]=burst ? 0x01 : 0x00;

                else
                  buf[3]=0xffff;
                
                for (i=0; i<len; i++)
                  buf[i+4]=msg[i];
                
                SOutCommand(av7110, buf, 18);
                break;
        }

        case DVB_CARD_TT_BUDGET:
                setgpio(av7110, 3, GPIO_OUTLO);
                
                mdelay(16);
                
                for (i=0; i<len; i++)
                        DiseqcSendByte(av7110, msg[i]);

                mdelay(16);
                
                if (burst!=-1) {
                        if (burst) 
                                DiseqcSendByte(av7110, 0xff);
                        else {
                                setgpio(av7110, 3, GPIO_OUTHI);
                                udelay(12500);
                                setgpio(av7110, 3, GPIO_OUTLO);
                        }
                        
                        ddelay(2);
                }

                break;

        default:
                return -1;
        }
        return 0;
}

/****************************************************************************
 * I2C client commands
 ****************************************************************************/

static inline int
i2c_writereg(av7110_t *av7110, u8 id, u8 reg, u8 val)
{
        u8 msg[2]={ reg, val }; 
        struct dvb_i2c_bus *i2c = av7110->saa->i2c_bus;
        struct i2c_msg msgs;

        msgs.flags=0;
        msgs.addr=id/2;
        msgs.len=2;
        msgs.buf=msg;
        return i2c->xfer (i2c, &msgs, 1);
}

static inline int
msp_writereg(av7110_t *av7110, u8 dev, u16 reg, u16 val)
{
        u8 msg[5]={ dev, reg>>8, reg&0xff, val>>8 , val&0xff }; 
        struct dvb_i2c_bus *i2c = av7110->saa->i2c_bus;
        struct i2c_msg msgs;

        msgs.flags=0;
        msgs.addr=0x40;
        msgs.len=5;
        msgs.buf=msg;
        return i2c->xfer(i2c, &msgs, 1);
}

static inline u8
i2c_readreg(av7110_t *av7110, u8 id, u8 reg)
{
        struct dvb_i2c_bus *i2c = av7110->saa->i2c_bus;
        u8 mm1[] = {0x00};
        u8 mm2[] = {0x00};
        struct i2c_msg msgs[2];

        msgs[0].flags=0;
        msgs[1].flags=I2C_M_RD;
        msgs[0].addr=msgs[1].addr=id/2;
        mm1[0]=reg;
        msgs[0].len=1; msgs[1].len=1;
        msgs[0].buf=mm1; msgs[1].buf=mm2;
        i2c->xfer(i2c, msgs, 2);

        return mm2[0];
}


/****************************************************************************
 * I/O buffer management and control
 ****************************************************************************/

static int sw2mode[16] = {
        VIDEO_MODE_PAL, VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL,
        VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL, VIDEO_MODE_NTSC,
        VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
        VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
};

static void
get_video_format(av7110_t *av7110, u8 *buf, int count)
{
        int i;
	int hsize,vsize;
        int sw;
        u8 *p;

        if (av7110->sinfo)
                return;
        for (i=7; i<count-10; i++) {
                p=buf+i;
                if (p[0] || p[1] || p[2]!=0x01 || p[3]!=0xb3)
                        continue;
                p+=4;
                hsize = ((p[1] &0xF0) >> 4) | (p[0] << 4);
                vsize = ((p[1] &0x0F) << 8) | (p[2]);
                sw = (p[3]&0x0F);
                SetMode(av7110, sw2mode[sw]);
                dprintk("dvb: playback %dx%d fr=%d\n", hsize, vsize, sw);
                av7110->sinfo=1;
                break;
        }
}

static void
play_video_cb(u8 *buf, int count, void *priv)
{
        av7110_t *av7110=(av7110_t *) priv;

        if ((buf[3]&0xe0)==0xe0) {
                get_video_format(av7110, buf, count);
                ring_buffer_write(&av7110->avout, buf, count, 0, 0);
        } else
                ring_buffer_write(&av7110->aout, buf, count, 0, 0);
}

static void
play_audio_cb(u8 *buf, int count, void *priv)
{
        av7110_t *av7110=(av7110_t *) priv;
        
        ring_buffer_write(&av7110->aout, buf, count, 0, 0);
}

#define FREE_COND (ring_buffer_free(&av7110->avout)>=20*1024 && ring_buffer_free(&av7110->aout)>=20*1024)

static ssize_t 
dvb_play(av7110_t *av7110, const u8 *buf,
         unsigned long count, int nonblock, int type, int umem)
{
        unsigned long todo = count, n;

        if (!av7110->kbuf[type])
                return -ENOBUFS;

	if (nonblock && !FREE_COND)
                return -EWOULDBLOCK;
                
        while (todo>0) {
                if (!FREE_COND) {
                        if (nonblock)
                                return count-todo;
                        if (wait_event_interruptible(av7110->avout.queue,
                                                     FREE_COND))
                        	return count-todo;
                } 
		n=todo;
                if (n>IPACKS*2)
                        n=IPACKS*2;
                if (umem) {
                        if (copy_from_user(av7110->kbuf[type], buf, n)) 
                                return -EFAULT;
                        dvb_filter_instant_repack(av7110->kbuf[type], n,
						  &av7110->ipack[type]);
                } else {
                        dvb_filter_instant_repack((u8 *)buf, n,
						  &av7110->ipack[type]);
		}
                todo -= n;
                buf += n;
        }
	return count-todo;
}

static ssize_t 
dvb_aplay(av7110_t *av7110, const u8 *buf,
         unsigned long count, int nonblock, int type)
{
        unsigned long todo = count, n;

        if (!av7110->kbuf[type])
                return -ENOBUFS;
        if (nonblock && ring_buffer_free(&av7110->aout)<20*1024)
                return -EWOULDBLOCK;
                
        while (todo>0) {
                if (ring_buffer_free(&av7110->aout)<20*1024) {
                        if (nonblock)
                                return count-todo;
                        if (wait_event_interruptible(av7110->aout.queue,
                                                     (ring_buffer_free(&av7110->aout)>=
                                                      20*1024)))
                        	return count-todo;
                } 
		n=todo;
                if (n>IPACKS*2)
                        n=IPACKS*2;
                if (copy_from_user(av7110->kbuf[type], buf, n)) 
                        return -EFAULT;
                dvb_filter_instant_repack(av7110->kbuf[type], n,
					  &av7110->ipack[type]);
//                        memcpy(dvb->kbuf[type], buf, n); 
                todo -= n;
                buf += n;
        }
	return count-todo;
}

void init_p2t(p2t_t *p, struct dvb_demux_feed *feed)
{
	memset(p->pes,0,TS_SIZE);
	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
	if (feed) p->feed = feed;
}

void clear_p2t(p2t_t *p)
{
	memset(p->pes,0,TS_SIZE);
//	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
}


long int find_pes_header(u8 const *buf, long int length, int *frags)
{
	int c = 0;
	int found = 0;

	*frags = 0;

	while (c < length-3 && !found) {
		if (buf[c] == 0x00 && buf[c+1] == 0x00 && 
		    buf[c+2] == 0x01) {
			switch ( buf[c+3] ) {
				
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				found = 1;
				break;
				
			default:
				c++;
				break;
			}	
		} else c++;
	}
	if (c == length-3 && !found){
		if (buf[length-1] == 0x00) *frags = 1;
		if (buf[length-2] == 0x00 &&
		    buf[length-1] == 0x00) *frags = 2;
		if (buf[length-3] == 0x00 &&
		    buf[length-2] == 0x00 &&
		    buf[length-1] == 0x01) *frags = 3;
		return -1;
	}

	return c;
}

void pes_to_ts( u8 const *buf, long int length, u16 pid, p2t_t *p)
{
	int c,c2,l,add;
	int check,rest;

	c = 0;
	c2 = 0;
	if (p->frags){
		check = 0;
		switch(p->frags){
		case 1:
			if ( buf[c] == 0x00 && buf[c+1] == 0x01 ){
				check = 1;
				c += 2;
			}
			break;
		case 2:
			if ( buf[c] == 0x01 ){
				check = 1;
				c++;
			}
			break;
		case 3:
			check = 1;
		}
		if(check){
			switch ( buf[c] ) {
				
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				p->pes[0] = 0x00;
				p->pes[1] = 0x00;
				p->pes[2] = 0x01;
				p->pes[3] = buf[c];
				p->pos=4;
				memcpy(p->pes+p->pos,buf+c,(TS_SIZE-4)-p->pos);
				c += (TS_SIZE-4)-p->pos;
				p_to_t(p->pes,(TS_SIZE-4),pid,&p->counter,
				       p->feed);
				clear_p2t(p);
				break;
				
			default:
				c=0;
				break;
			}
		}
		p->frags = 0;
	}
		
	if (p->pos){
		c2 = find_pes_header(buf+c,length-c,&p->frags);
		if (c2 >= 0 && c2 < (TS_SIZE-4)-p->pos){
			l = c2+c;
		} else l = (TS_SIZE-4)-p->pos;
		memcpy(p->pes+p->pos,buf,l);
		c += l;
		p->pos += l;
		p_to_t(p->pes,p->pos,pid,&p->counter, p->feed);
		clear_p2t(p);
	}
			
	add = 0;
	while (c < length){
		c2 = find_pes_header(buf+c+add,length-c-add,&p->frags);
		if (c2 >= 0) {
			c2 += c+add;
			if (c2 > c){
				p_to_t(buf+c,c2-c,pid,&p->counter,
				       p->feed);
				c = c2;
				clear_p2t(p);
				add = 0;
			} else add = 1;
		} else {
			l = length-c;
			rest = l % (TS_SIZE-4);
			l -= rest;
			p_to_t(buf+c,l,pid,&p->counter,
			       p->feed);
			memcpy(p->pes,buf+c+l,rest);
			p->pos = rest;
			c = length;
		}
	}
}


int write_ts_header2(u16 pid, u8 *counter, int pes_start, u8 *buf, u8 length)
{
	int i;
	int c = 0;
	int fill;
	u8 tshead[4] = { 0x47, 0x00, 0x00, 0x10}; 
        

	fill = (TS_SIZE-4)-length;
        if (pes_start) tshead[1] = 0x40;
	if (fill) tshead[3] = 0x30;
        tshead[1] |= (u8)((pid & 0x1F00) >> 8);
        tshead[2] |= (u8)(pid & 0x00FF);
        tshead[3] |= ((*counter)++ & 0x0F) ;
        memcpy(buf,tshead,4);
	c+=4;


	if (fill){
		buf[4] = fill-1;
		c++;
		if (fill >1){
			buf[5] = 0x00;
			c++;
		}
		for ( i = 6; i < fill+4; i++){
			buf[i] = 0xFF;
			c++;
		}
	}

        return c;
}


void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter, 
            struct dvb_demux_feed *feed)
{
  
	int l, pes_start;
	u8 obuf[TS_SIZE];
	long int c = 0;

	pes_start = 0;
	if ( length > 3 && 
             buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 )
		switch (buf[3]){
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				pes_start = 1;
				break;
				
			default:
				break;
		}			

	while ( c < length ){
		memset(obuf,0,TS_SIZE);
		if (length - c >= (TS_SIZE-4)){
			l = write_ts_header2(pid, counter, pes_start
					     , obuf, (TS_SIZE-4));
			memcpy(obuf+l, buf+c, TS_SIZE-l);
			c += TS_SIZE-l;
		} else { 
			l = write_ts_header2(pid, counter, pes_start
					     , obuf, length-c);
			memcpy(obuf+l, buf+c, TS_SIZE-l);
			c = length;
		}
                feed->cb.ts(obuf, 188, 0, 0, &feed->feed.ts, DMX_OK); 
		pes_start = 0;
	}
}


/****************************************************************************
 * V4L SECTION
 ****************************************************************************/

static int dvb_do_ioctl (struct inode *inode, struct file *file,
			 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata (file);
	av7110_t *av7110 = dev->priv;

	switch (cmd) {
        case VIDIOCGCAP:
        {
                struct video_capability *b = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCGCAP called\n");

                strcpy(b->name, &dev->name[0]);
			
                b->type 	= av7110->video.type;
						
                b->channels	= 1;	
                b->audios	= 2;
                b->maxwidth	= 768;
                b->maxheight	= 576;
                b->minwidth	= 32;
                b->minheight	= 32;

		return 0;
        }

	case VIDIOCGCHAN:
        {
		static const
		struct video_channel dvb_chan = { 0, "DVB", 1, 3, 1, 1 };
                struct video_channel *v = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCGCHAN called\n");
                
                memcpy(v, &dvb_chan, sizeof(struct video_channel));
                
                return 0;
                
        }

	case VIDIOCSCHAN:
        {
                struct video_channel *v = arg;

		dprintk(KERN_ERR "dvb: VIDIOCSCHAN called\n");

		if (v->channel>0)
			return -EINVAL;

		if (v->norm > 1)
			return -EOPNOTSUPP;

                av7110->vidmode = v->norm;
                SetMode(av7110, v->norm);
                av7110->saa->mode = v->norm;
		return 0;
        }

	case VIDIOCGTUNER:
        {
                struct video_tuner *v = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCGTUNER called\n");
                
                /* only channel 0 has a tuner */	
                if(!v->tuner)
                        return -EINVAL;
                
                /* fill the structure */
                strcpy(v->name, "DVB");
                v->rangelow  = 0x00000000;
                v->rangehigh = 0xffffffff;
                
                v->flags= VIDEO_TUNER_PAL | VIDEO_TUNER_NTSC;
                v->mode = av7110->vidmode;
		
                /* fixme: fill in signal strength here */
                v->signal = 0xffff;
                
                return 0;
        }

        case VIDIOCSTUNER:
        {
                struct video_tuner *v = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCSTUNER called\n");
                
                /* only channel 0 has a tuner */
                if (!v->tuner)
                        return -EINVAL;
                
                /* check if format is supported */
                if(v->mode != VIDEO_MODE_PAL && v->mode != VIDEO_MODE_NTSC
                    /* && v->mode != VIDEO_MODE_SECAM */ )
                       return -EOPNOTSUPP;
                
                av7110->vidmode = v->mode;
                SetMode(av7110, v->mode);
                
                return 0;
        }

        case VIDIOCGPICT:
        {
                struct video_picture *p = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCGPICT called\n");

		saacomm(SAA7146_V4L_GPICT, p);
                
                dprintk("dvb: VIDIOCGPICT called: b:%d c:%d s:%d d:%d p:%d\n",
			p->brightness, p->contrast, p->colour,
			p->depth, p->palette);
                
                return 0;
        }

        case VIDIOCSPICT:
        {
                struct video_picture *p = arg;
                
                dprintk("dvb: VIDIOCSPICT called: b:%d c:%d s:%d d:%d p:%d\n",
			p->brightness, p->contrast, p->colour,
			p->depth, p->palette);

		switch (p->palette) {
		case VIDEO_PALETTE_RGB555:
		case VIDEO_PALETTE_RGB565:
		case VIDEO_PALETTE_RGB24:
		case VIDEO_PALETTE_RGB32:
		case VIDEO_PALETTE_UYVY:
		case VIDEO_PALETTE_YUV422P:
		case VIDEO_PALETTE_YUV420P:
		case VIDEO_PALETTE_YUV411P:
			break;
		default:
			return -EINVAL;
		}

                saacomm(SAA7146_V4L_SPICT, p);
                
                return 0;
                
        }

        case VIDIOCSWIN:
        {
                struct video_window *w = arg;

                dprintk("dvb: VIDIOCSWIN called: "
			"clips: %d, x:%d, y:%d, h:%d, w:%d\n",
			w->clipcount, w->x, w->y, w->height, w->width);
                
                saacomm(SAA7146_V4L_SWIN, w);
                
                return 0;
        }

        case VIDIOCGWIN:
        {
                dprintk(KERN_ERR "dvb: VIDIOCGWIN called\n");
                return 0;
        }

        case VIDIOCCAPTURE:
        {
                int *v = arg;
                
                dprintk("dvb: VIDIOCCAPTURE called, mode:%d (0=disable)\n", *v);
                
                saacomm(SAA7146_V4L_CCAPTURE, v);

                return 0;
        }

        case VIDIOCGFBUF:
        {
                struct video_buffer *b = arg;
                
                dprintk(KERN_ERR "dvb: VIDIOCGFBUF called\n");
                
                saacomm(SAA7146_V4L_GFBUF, b);
                
                return 0;
                
        }

        case VIDIOCSFBUF:
        {
                struct video_buffer *b = arg;
                u32 vid = (vidmem << 16) | vidlow;

                /* see if vidmem-override is requested */
                if (vidmem) {
                        printk ("dvb: video-memory-override. (0x%08x)\n", vid);
                        b->base = (void*) vid;
                }
                
                saacomm(SAA7146_V4L_SFBUF, b);
                
                dprintk(KERN_ERR "dvb: VIDIOCSFBUF called\n");
                
                return 0;		
        }

	/* Video key event - to dev 255 is to all - 
	 * cuts capture on all DMA windows with this key (0xFFFFFFFF == all)
	 */
	case VIDIOCKEY:
        {
                dprintk(KERN_ERR "dvb: VIDIOCKEY called\n");
                return 0;
        }

	case VIDIOCGAUDIO:
        {
                struct video_audio *v = arg;
                
                v->flags = VIDEO_AUDIO_MUTABLE;
                /* let's auto-detect */
                return 0;
        }

	case VIDIOCSAUDIO:
        {
        	//struct video_audio *v;
                return 0;
        }

        case VIDIOCSYNC:
        {
                int i = 0;
		int *frame = (int*) arg;
                
                dprintk ("dvb: VIDIOCSYNC called - frame: %d\n", *frame);
                
                /* simply pass the requested frame-number to the corresponding
                   saa7146-function ... */
                i = saacomm(SAA7146_V4L_CSYNC, frame);

		dprintk ("dvb: VIDIOCSYNC done - frame: %d\n", *frame);
                
                return i;
        }

        case VIDIOCMCAPTURE:
        {
                struct video_mmap *vm = arg;
                int i = 0;

                dprintk(KERN_ERR "dvb: VIDIOCMCAPTURE called: fr:%d,"
                        "fmt:%d, w:%d, h:%d\n",
                        vm->frame, vm->format, vm->width, vm->height);

		switch (vm->format) {
		case VIDEO_PALETTE_YUV422P:
		case VIDEO_PALETTE_YUV420P:
		case VIDEO_PALETTE_YUV411P:
			return -EINVAL;
		}

                /* simply pass the structure for the requested frame-number
                   to the corresponding saa7146-function ... */
                i = saacomm(SAA7146_V4L_CMCAPTURE, vm);
                
                return i;
        }

        case VIDIOCGMBUF:
        {
                struct video_mbuf *mbuf = arg;
                dprintk(KERN_ERR "dvb: VIDIOCGMBUF called\n");
                saacomm(SAA7146_V4L_GMBUF, mbuf);
                return 0;
                
        }

        case VIDIOCGUNIT:
        {
                /*struct video_unit vu;*/
                dprintk(KERN_ERR "dvb: VIDIOCGUNIT called\n");
                return 0;
        }
		
        case VIDIOCGCAPTURE:
        {
                dprintk(KERN_ERR "dvb: VIDIOCGCAPTURE called\n");
                return 0;
        }
        case VIDIOCSCAPTURE:
        {
                dprintk(KERN_ERR "dvb: VIDIOCSCAPTURE called\n");
                return 0;
        }

        default:
                return -ENOIOCTLCMD;
	}

	return 0;
}


static int dvb_ioctl (struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, dvb_do_ioctl);
}


static int dvb_mmap(struct file* file, struct vm_area_struct *vma)
{
	struct video_device *dev = video_devdata (file);
	av7110_t *av7110 = dev->priv;

	dprintk(KERN_ERR "av7110: dvb_mmap called, adr:%08lx, size:0x%08lx\n",
		vma->vm_start, vma->vm_end - vma->vm_start);

	if (saacomm(SAA7146_DO_MMAP, vma)) {
		printk(KERN_ERR "av7110: dvb_mmap failed!\n");
		return -1;
	}

	return 0;
}


static unsigned int dvb_audio_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        unsigned int mask=0;

        if (av7110->playing) {
                if (ring_buffer_free(&av7110->aout)>20*1024)
                        return (POLLOUT | POLLWRNORM);

                poll_wait(file, &av7110->aout.queue, wait);
          
                if (ring_buffer_free(&av7110->aout)>20*1024)
                        mask |= (POLLOUT | POLLWRNORM);
        } else /* if not playing: may play if asked for */
                mask = (POLLOUT | POLLWRNORM);
                
        return mask;
}


static struct file_operations dvb_fops = {
	.ioctl		= dvb_ioctl,
	.mmap		= dvb_mmap,
	.llseek		= no_llseek    
};


/* template for video_device-structure */
static struct video_device dvb_template = {
	.owner		= THIS_MODULE,
	.name		= "DVB Board",
	.type		= VID_TYPE_TUNER	|
        	        VID_TYPE_CAPTURE	|
	                VID_TYPE_OVERLAY	|
        	        VID_TYPE_CLIPPING	|
                	VID_TYPE_FRAMERAM	|
	                VID_TYPE_SCALES,
	.hardware	= VID_HARDWARE_SAA7146,
	.fops		= &dvb_fops
};


static int vid_register(av7110_t *av7110)
{
        memcpy( &av7110->video, &dvb_template, sizeof(struct video_device));

	av7110->video.priv = av7110;

        if (video_register_device(&av7110->video, VFL_TYPE_GRABBER, -1)) {
                printk(KERN_ERR "dvb: can't register videodevice\n");
                return -1;
        }

        return 0;
}

static inline int vid_unregister(av7110_t *av7110)
{
        if (av7110->video.minor != -1)
                video_unregister_device(&av7110->video);

	return 0;
}

/****************************************************************************
 * END OF V4L SECTION
 ****************************************************************************/


/****************************************************************************
 * DVB API SECTION
 ****************************************************************************/


/******************************************************************************
 * hardware filter functions
 ******************************************************************************/

static int 
StartHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
        struct dvb_demux_feed *dvbdmxfeed=dvbdmxfilter->feed;
        av7110_t *av7110=(av7110_t *) dvbdmxfeed->demux->priv;
        u16 buf[20];
        int ret, i;
        u16 handle;
//        u16 mode=0x0320;
        u16 mode=0xb96a;
        
        if (dvbdmxfilter->type==DMX_TYPE_SEC) {
                buf[4]=(dvbdmxfilter->filter.filter_value[0]<<8)|
                        dvbdmxfilter->maskandmode[0];
                for (i=3; i<18; i++)
                        buf[i+4-2]=(dvbdmxfilter->filter.filter_value[i]<<8)|
                                dvbdmxfilter->maskandmode[i];
                mode=4;
        } else
        if ((dvbdmxfeed->ts_type & TS_PACKET) &&
            !(dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)) 
                init_p2t(&av7110->p2t_filter[dvbdmxfilter->index], dvbdmxfeed);

        buf[0] = (COMTYPE_PID_FILTER << 8) + AddPIDFilter; 
        buf[1] = 16;	
        buf[2] = dvbdmxfeed->pid;
        buf[3] = mode;

        ret=CommandRequest(av7110, buf, 20, &handle, 1);          
        if (ret<0)
                return ret;

        av7110->handle2filter[handle]=dvbdmxfilter;
        dvbdmxfilter->hw_handle=handle;

        return ret;
}

static int 
StopHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
        av7110_t *av7110=(av7110_t *) dvbdmxfilter->feed->demux->priv;
        u16 buf[3];
        u16 answ[2];
        int ret;
        u16 handle;
                
        handle=dvbdmxfilter->hw_handle;
        if (handle>32) {
                dprintk("dvb: StopHWFilter tried to stop invalid filter %d.\n",
                       handle);
                dprintk("dvb: filter type = %d\n",  dvbdmxfilter->type);
                return 0;
        }

        av7110->handle2filter[handle]=NULL;

        buf[0] = (COMTYPE_PID_FILTER << 8) + DelPIDFilter; 
        buf[1] = 1;	
        buf[2] = handle;
        ret=CommandRequest(av7110, buf, 3, answ, 2);          

        if (answ[1] != handle) {
                dprintk("dvb: filter %d shutdown error :%d\n", handle, answ[1]);
                ret=-1;
        }
        return ret;
}


static int 
dvb_write_to_decoder(struct dvb_demux_feed *dvbdmxfeed, u8 *buf, size_t count)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        av7110_t *av7110=(av7110_t *) dvbdmx->priv;
        ipack *ipack=&av7110->ipack[dvbdmxfeed->pes_type];

        switch (dvbdmxfeed->pes_type) {
        case 0:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY) {
                        return -EINVAL;
                }
                break;
        case 1:
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY) {
                        return -EINVAL;
                }
                break;
        default:
                return -1;
        }

        if (!(buf[3]&0x10)) { // no payload?
                return -1;
        }
        if (buf[1]&0x40)
                dvb_filter_ipack_flush(ipack);

        if (buf[3]&0x20) {  // adaptation field?
                count-=buf[4]+1;
                buf+=buf[4]+1;
                if (!count) {
                        return 0;
                }
        }
        
        dvb_filter_instant_repack(buf+4, count-4,
				  &av7110->ipack[dvbdmxfeed->pes_type]);
        return 0;
}

static void
dvb_feed_start_pid(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        av7110_t *av7110=(av7110_t *) dvbdmx->priv;
        u16 *pid=dvbdmx->pids, npids[5];
        int i;
        
        npids[0]=npids[1]=npids[2]=npids[3]=0xffff;
        npids[4]=0xffff;
        i=dvbdmxfeed->pes_type;
        npids[i]=(pid[i]&0x8000) ? 0 : pid[i];
        if ((i==2) && npids[i] && (dvbdmxfeed->ts_type & TS_PACKET)) {
                npids[i]=0;
                ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
                StartHWFilter(dvbdmxfeed->filter); 
                return;
        }
        if (dvbdmxfeed->pes_type<=2)
                ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);

        if (dvbdmxfeed->pes_type<2 && npids[0])
                if (av7110->fe_synced) 
                        outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);

        if ((dvbdmxfeed->ts_type & TS_PACKET)) { 
                if (dvbdmxfeed->pes_type == 0 && 
                    !(dvbdmx->pids[0]&0x8000))
                        AV_StartRecord(av7110, RP_AUDIO, 
                                       dvbdmxfeed);
                if (dvbdmxfeed->pes_type == 1 && 
                    !(dvbdmx->pids[1]&0x8000))
                        AV_StartRecord(av7110, RP_VIDEO, 
                                       dvbdmxfeed);
        }
}

static void
dvb_feed_stop_pid(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        av7110_t *av7110=(av7110_t *) dvbdmx->priv;
        u16 *pid=dvbdmx->pids, npids[5];
        int i;
        
        if (dvbdmxfeed->pes_type<=1) {
                AV_Stop(av7110, dvbdmxfeed->pes_type ? 
                        RP_VIDEO : RP_AUDIO);
                if (!av7110->rec_mode)
                        dvbdmx->recording=0;
                if (!av7110->playing)
                        dvbdmx->playing=0;
        }
        npids[0]=npids[1]=npids[2]=npids[3]=0xffff;
        npids[4]=0xffff;
        i=dvbdmxfeed->pes_type;
        switch (i) {
        case 2: //teletext
                if (dvbdmxfeed->ts_type & TS_PACKET)
                        StopHWFilter(dvbdmxfeed->filter); 
                npids[2]=0;
                break;
        case 0:
        case 1:
        case 4:
                if (!pids_off) 
                        return;
                npids[i]=(pid[i]&0x8000) ? 0 : pid[i];
                break;
        }
        ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
}

static int 
dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        av7110_t *av7110=(av7110_t *) dvbdmx->priv;

        if (!dvbdmx->dmx.frontend)
                return -EINVAL;

        if (av7110->saa->card_type>=DVB_CARD_TT_BUDGET)
                return TTBStart(av7110); 

        if (dvbdmxfeed->pid>0x1fff) 
                return -1;

        if (dvbdmxfeed->type == DMX_TYPE_TS) {
	        if ((dvbdmxfeed->ts_type & TS_DECODER) 
		    && (dvbdmxfeed->pes_type<DMX_TS_PES_OTHER)) {

		        switch (dvbdmx->dmx.frontend->source) {
			case DMX_MEMORY_FE: 
			        if (dvbdmxfeed->ts_type & TS_DECODER)
				       if (dvbdmxfeed->pes_type<2 && 
                                           !(dvbdmx->pids[0]&0x8000) &&
					   !(dvbdmx->pids[1]&0x8000)) {
                                               ring_buffer_flush(&av7110->avout);
                                               ring_buffer_flush(&av7110->aout);
                                               AV_StartPlay(av7110,RP_AV);
                                               dvbdmx->playing=1;
					}
				break;
			default:
                                dvb_feed_start_pid(dvbdmxfeed);
				break;
			}
		} else
		        if ((dvbdmxfeed->ts_type & TS_PACKET) &&
                            (dvbdmx->dmx.frontend->source!=DMX_MEMORY_FE))
                                StartHWFilter(dvbdmxfeed->filter); 
        }
        
        if (dvbdmxfeed->type == DMX_TYPE_SEC) {
                int i;

	        for (i=0; i<dvbdmx->filternum; i++) {
		        if (dvbdmx->filter[i].state!=DMX_STATE_READY)
			        continue;
			if (dvbdmx->filter[i].type!=DMX_TYPE_SEC)
			        continue;
			if (dvbdmx->filter[i].filter.parent!=&dvbdmxfeed->feed.sec)
			        continue;
			dvbdmx->filter[i].state=DMX_STATE_GO;
                        if (dvbdmx->dmx.frontend->source!=DMX_MEMORY_FE)
                                StartHWFilter(&dvbdmx->filter[i]); 
                }
	}
        return 0;
}


static int 
dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        av7110_t *av7110=(av7110_t *) dvbdmx->priv;

        if (av7110->saa->card_type>=DVB_CARD_TT_BUDGET) 
                return TTBStop(av7110); 

        if (dvbdmxfeed->type == DMX_TYPE_TS) {
                if (dvbdmxfeed->ts_type & TS_DECODER) {
                        if (dvbdmxfeed->pes_type>=DMX_TS_PES_OTHER ||
                            !dvbdmx->pesfilter[dvbdmxfeed->pes_type]) 
                                return -EINVAL;
                        dvbdmx->pids[dvbdmxfeed->pes_type]|=0x8000;
                        dvbdmx->pesfilter[dvbdmxfeed->pes_type]=0;
                }
                if (dvbdmxfeed->ts_type & TS_DECODER &&
		    (dvbdmxfeed->pes_type<DMX_TS_PES_OTHER)) {
                        dvb_feed_stop_pid(dvbdmxfeed);
                } else 
                        if ((dvbdmxfeed->ts_type & TS_PACKET) &&
                            (dvbdmx->dmx.frontend->source!=DMX_MEMORY_FE))
                                StopHWFilter(dvbdmxfeed->filter); 
        }
        
        if (dvbdmxfeed->type == DMX_TYPE_SEC) {
                int i;

	        for (i=0; i<dvbdmx->filternum; i++)
		        if (dvbdmx->filter[i].state==DMX_STATE_GO && 
			    dvbdmx->filter[i].filter.parent==&dvbdmxfeed->feed.sec) {
			        dvbdmx->filter[i].state=DMX_STATE_READY;
                                if (dvbdmx->dmx.frontend->source!=DMX_MEMORY_FE)
                                        StopHWFilter(&dvbdmx->filter[i]); 
                }
	}
        return 0;
}


static void 
restart_feeds(av7110_t *av7110)
{
        struct dvb_demux *dvbdmx=&av7110->demux;
        struct dvb_demux_feed *feed;
        int mode;
        int i;

        mode=av7110->playing;
        av7110->playing=0;
        av7110->rec_mode=0;

        for (i=0; i<dvbdmx->filternum; i++) {
                feed=&dvbdmx->feed[i];
                if (feed->state==DMX_STATE_GO)
                        dvb_start_feed(feed);
        }

        if (mode)
                AV_StartPlay(av7110, mode);
}

/******************************************************************************
 * SEC device file operations
 ******************************************************************************/

static
int av7110_diseqc_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	av7110_t *av7110 = fe->before_after_data;

	switch (cmd) {
	case FE_SET_TONE:
		switch ((fe_sec_tone_mode_t) arg) {
		case SEC_TONE_ON:
			Set22K (av7110, 1);
			break;
		case SEC_TONE_OFF:
			Set22K (av7110, 0);
			break;
		default:
			return -EINVAL;
		};
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
	{
		struct dvb_diseqc_master_cmd *cmd = arg;

		SendDiSEqCMsg (av7110, cmd->msg_len, cmd->msg, 0);
		break;
	}

	case FE_DISEQC_SEND_BURST:
		SendDiSEqCMsg (av7110, 0, NULL, (int) arg);
		break;

	default:
		return -EOPNOTSUPP;
	};

	return 0;
}

/******************************************************************************
 * CI link layer file ops (FIXME: move this to separate module later)
 ******************************************************************************/

int ci_ll_init(ring_buffer_t *cirbuf, ring_buffer_t *ciwbuf, int size)
{
        ring_buffer_init(cirbuf, vmalloc(size), size);
        ring_buffer_init(ciwbuf, vmalloc(size), size);
        return 0;
}

void ci_ll_flush(ring_buffer_t *cirbuf, ring_buffer_t *ciwbuf)
{
        ring_buffer_flush(cirbuf);
        ring_buffer_flush(ciwbuf);
}

void ci_ll_release(ring_buffer_t *cirbuf, ring_buffer_t *ciwbuf)
{
        vfree(cirbuf->data);
        cirbuf->data=0;
        vfree(ciwbuf->data);
        ciwbuf->data=0;
}


int ci_ll_reset(ring_buffer_t *cibuf, struct file *file, 
                int slots, ca_slot_info_t *slot)
{
	int i;

        if (ring_buffer_free(cibuf)<8)
                return -EBUSY;
	for (i=0; i<2; i++) 
		if (slots&(1<<i)) {
                        cibuf->data[cibuf->pwrite]=0x00;   
                        cibuf->data[(cibuf->pwrite+1)%cibuf->size]=0x06;
                        cibuf->data[(cibuf->pwrite+2)%cibuf->size]=i;
                        cibuf->data[(cibuf->pwrite+3)%cibuf->size]=0x00;
                        cibuf->data[(cibuf->pwrite+4)%cibuf->size]=0xff;
                        cibuf->data[(cibuf->pwrite+5)%cibuf->size]=0x02;
                        cibuf->data[(cibuf->pwrite+6)%cibuf->size]=0x00;
                        cibuf->data[(cibuf->pwrite+7)%cibuf->size]=0x00;
                        cibuf->pwrite=(cibuf->pwrite+8)%cibuf->size;
                        slot[i].flags=0;
                }
        return 0;
}

static ssize_t 
ci_ll_write(ring_buffer_t *cibuf, struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int free, split;
        int32_t pread;
        int non_blocking=file->f_flags&O_NONBLOCK;
	
        if (count>2048)
                return -EINVAL;
	pread=cibuf->pread; 
	free=pread-cibuf->pwrite;
	if (free<=0) 
                free+=cibuf->size;
	if (count+2>=free) { 
                if (non_blocking)
                        return -EWOULDBLOCK;
                if (wait_event_interruptible(cibuf->queue,
                                             (ring_buffer_free(cibuf)>count+2)))
                        return 0;
        }
        cibuf->data[cibuf->pwrite]=(count>>8);   
	cibuf->data[(cibuf->pwrite+1)%cibuf->size]=(count&0xff); 
        cibuf->pwrite=(cibuf->pwrite+2)%cibuf->size;

	if (pread>cibuf->pwrite)
		split=0;
	else
		split=cibuf->size-cibuf->pwrite;
        if (split && split<count) {
                if (copy_from_user(cibuf->data + cibuf->pwrite, buf, split))
                        return -EFAULT;
		if (copy_from_user(cibuf->data, buf+split, count-split))
                        return -EFAULT;
        } else 
		if (copy_from_user(cibuf->data + cibuf->pwrite, buf, count))
                        return -EFAULT;
        cibuf->pwrite=(cibuf->pwrite+count)%cibuf->size;
	return count;
}

static ssize_t 
ci_ll_read(ring_buffer_t *cibuf, struct file *file, char *buf, size_t count, loff_t *ppos)
{
        int split=0, avail, pwrite;
        int non_blocking=file->f_flags&O_NONBLOCK;
	
	if (!cibuf->data || !count)
	        return 0;
	if (non_blocking && (ring_buffer_empty(cibuf)))
	        return -EWOULDBLOCK;
	if (wait_event_interruptible(cibuf->queue,
				     !ring_buffer_empty(cibuf)))
		return 0;
        pwrite=cibuf->pwrite;
	avail=pwrite - cibuf->pread;
	if (avail<0) 
                avail+=cibuf->size;
	if (avail<4) 
                return 0;
	count=(cibuf->data[cibuf->pread]<<8);  
	count|=cibuf->data[(cibuf->pread+1)%cibuf->size];
	if (avail<count+2) 
                return -EINVAL;
        cibuf->pread=(cibuf->pread+2)%cibuf->size;	

        if (pwrite<cibuf->pread)
                split=cibuf->size-cibuf->pread;
	if (split && split<count) {
		if (copy_to_user(buf, cibuf->data+cibuf->pread, split))
			return -EFAULT;
		if (copy_to_user(buf+split, cibuf->data, count-split))
			return -EFAULT;
	} else 
		if (copy_to_user(buf, cibuf->data+cibuf->pread, count))
			return -EFAULT;
	cibuf->pread=(cibuf->pread + count)%cibuf->size;
	return count;
}

static int 
dvb_ca_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        int err=dvb_generic_open(inode, file);

        if (err<0)
                return err;
        ci_ll_flush(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
        return 0;
}

static unsigned 
int dvb_ca_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

        unsigned int mask=0;

        ring_buffer_t *rbuf=&av7110->ci_rbuffer;
        ring_buffer_t *wbuf=&av7110->ci_wbuffer;

        if (!ring_buffer_empty(rbuf))
                mask|=POLLIN;
        if (ring_buffer_avail(wbuf)>1024)
                mask|=POLLOUT;
        if (mask)
                return mask;
        
        poll_wait(file, &rbuf->queue, wait);
        
        if (!ring_buffer_empty(rbuf))
                mask|=POLLIN;
        if (ring_buffer_avail(wbuf)>1024)
                mask|=POLLOUT;
        
        return mask;
}

static int 
dvb_ca_ioctl(struct inode *inode, struct file *file, 
             unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;

        switch (cmd) {
        case CA_RESET:
#ifdef NEW_CI
                
                return ci_ll_reset(&av7110->ci_wbuffer, file, arg, &av7110->ci_slot[0]);
#endif                        
                break;
                
        case CA_GET_CAP:
        {
                ca_caps_t cap;
                
                cap.slot_num=2;
#ifdef NEW_CI
                cap.slot_type=CA_CI_LINK;
#else
                cap.slot_type=CA_CI;
#endif
                cap.descr_num=16;
                cap.descr_type=CA_ECD;
                memcpy(parg, &cap, sizeof(cap));
        }
        break;

        case CA_GET_SLOT_INFO:
        {
                ca_slot_info_t *info=(ca_slot_info_t *)parg;

                if (info->num>1)
                        return -EINVAL;
#ifdef NEW_CI
                av7110->ci_slot[info->num].type = CA_CI_LINK;
#else
                av7110->ci_slot[info->num].type = CA_CI;
#endif
                memcpy(info, &av7110->ci_slot[info->num], sizeof(ca_slot_info_t));
        }
        break;

        case CA_GET_MSG:
                break;

        case CA_SEND_MSG:
                break;
                
        case CA_GET_DESCR_INFO:
        {
                ca_descr_info_t info;

                info.num=16;
                info.type=CA_ECD;
                memcpy (parg, &info, sizeof (info));
        }
        break;

        case CA_SET_DESCR:
        {
                ca_descr_t *descr=(ca_descr_t*) parg;
                
                if (descr->index>=16)
                        return -EINVAL;
                if (descr->parity>1)
                        return -EINVAL;
                outcom(av7110, COMTYPE_PIDFILTER, SetDescr, 5, 
                       (descr->index<<8)|descr->parity,
                       (descr->cw[0]<<8)|descr->cw[1],
                       (descr->cw[2]<<8)|descr->cw[3],
                       (descr->cw[4]<<8)|descr->cw[5],
                       (descr->cw[6]<<8)|descr->cw[7]);
        }
        break;

        default:
                return -EINVAL;
        }
        return 0;
}

static ssize_t 
dvb_ca_write(struct file *file, const char *buf, 
             size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        
        return ci_ll_write(&av7110->ci_wbuffer, file, buf, count, ppos);
}

static ssize_t 
dvb_ca_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

        return ci_ll_read(&av7110->ci_rbuffer, file, buf, count, ppos);

}



/******************************************************************************
 * DVB device file operations
 ******************************************************************************/

static unsigned int dvb_video_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        unsigned int mask=0;

        if (av7110->playing) {
                if (FREE_COND)
                        return (POLLOUT | POLLWRNORM);

                poll_wait(file, &av7110->avout.queue, wait);
          
                if (FREE_COND)
                        mask |= (POLLOUT | POLLWRNORM);
        } else /* if not playing: may play if asked for */
                mask = (POLLOUT | POLLWRNORM);
                
        return mask;
}

static ssize_t 
dvb_video_write(struct file *file, const char *buf, 
                size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

        if (av7110->videostate.stream_source!=VIDEO_SOURCE_MEMORY) 
                return -EPERM;

	return dvb_play(av7110, buf, count, file->f_flags&O_NONBLOCK, 1, 1);
}

static ssize_t 
dvb_audio_write(struct file *file, const char *buf, 
                size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

        if (av7110->audiostate.stream_source!=AUDIO_SOURCE_MEMORY) {
                printk(KERN_ERR "not audio source memory\n");
                return -EPERM;
        }
        return dvb_aplay(av7110, buf, count, file->f_flags&O_NONBLOCK, 0);
}

u8 iframe_header[] = { 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x00, 0x00 };

#define MIN_IFRAME 400000

static void 
play_iframe(av7110_t *av7110, u8 *buf, unsigned int len, int nonblock)
{
        int i, n=1;
       
        if (!(av7110->playing&RP_VIDEO)) {
                AV_StartPlay(av7110, RP_VIDEO);
                n=MIN_IFRAME/len+1;
        }

	dvb_play(av7110, iframe_header, sizeof(iframe_header), 0, 1, 0);

	for (i=0; i<n; i++) 
                dvb_play(av7110, buf, len, 0, 1, 1);

	dvb_filter_ipack_flush(&av7110->ipack[1]);
}


static int 
dvb_video_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;
        int ret=0;
        
        if (((file->f_flags&O_ACCMODE)==O_RDONLY) &&
            (cmd!=VIDEO_GET_STATUS))
                return -EPERM;
        
        switch (cmd) {
        case VIDEO_STOP:
                av7110->videostate.play_state=VIDEO_STOPPED;
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY)
                        AV_Stop(av7110, RP_VIDEO);
                else
                        vidcom(av7110, 0x000e, 
                               av7110->videostate.video_blank ? 0 : 1);
                av7110->trickmode=TRICK_NONE;
                break; 
                
        case VIDEO_PLAY:
                av7110->trickmode=TRICK_NONE;
                if (av7110->videostate.play_state==VIDEO_FREEZED) {
                        av7110->videostate.play_state=VIDEO_PLAYING;
                        vidcom(av7110, 0x000d, 0);
                } 
                
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY) {
                        if (av7110->playing==RP_AV) {
                                outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
                                av7110->playing&=~RP_VIDEO;
                        }
                        AV_StartPlay(av7110,RP_VIDEO);
                        vidcom(av7110, 0x000d, 0);
                } else {
                        //AV_Stop(av7110, RP_VIDEO);
                        vidcom(av7110, 0x000d, 0);
                }
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_FREEZE:
                av7110->videostate.play_state=VIDEO_FREEZED;
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, __Pause, 0);
                else
                        vidcom(av7110, 0x0102, 1);
                av7110->trickmode=TRICK_FREEZE;
                break;
                
        case VIDEO_CONTINUE:
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, __Continue, 0);
                vidcom(av7110, 0x000d, 0);
                av7110->videostate.play_state=VIDEO_PLAYING;
                av7110->trickmode=TRICK_NONE;
                break;
                
        case VIDEO_SELECT_SOURCE:
                av7110->videostate.stream_source=(video_stream_source_t) arg;
                break;
                
        case VIDEO_SET_BLANK:
                av7110->videostate.video_blank=(int) arg;
		break;
                
        case VIDEO_GET_STATUS:
                memcpy(parg, &av7110->videostate, sizeof(struct video_status));
                break;
                
        case VIDEO_GET_EVENT:
                //FIXME: write firmware support for this
                ret=-EOPNOTSUPP;
                
        case VIDEO_SET_DISPLAY_FORMAT:
        {
                video_displayformat_t format=(video_displayformat_t) arg;
                u16 val=0;
                
                switch(format) {
                case VIDEO_PAN_SCAN:
                        val=VID_PAN_SCAN_PREF;
                        break;
                        
                case VIDEO_LETTER_BOX:
                        val=VID_VC_AND_PS_PREF;
                        break;
                        
                case VIDEO_CENTER_CUT_OUT:
                        val=VID_CENTRE_CUT_PREF;
                        break;
                        
                default:
                        ret=-EINVAL;
                        break;
                }
                if (ret<0)
                        break;
                av7110->videostate.video_format=format;
                ret=outcom(av7110, COMTYPE_ENCODER, SetPanScanType, 
                           1, (u16) val);
                break;
        }
        
        case VIDEO_SET_FORMAT:
                if (arg>1) {
                        ret=-EINVAL;
                        break;
                }
                av7110->display_ar=arg;
                ret=outcom(av7110, COMTYPE_ENCODER, SetMonitorType, 
                           1, (u16) arg);
                break;
                
        case VIDEO_STILLPICTURE:
        { 
                struct video_still_picture *pic=
                        (struct video_still_picture *) parg;
                ring_buffer_flush(&av7110->avout);
                play_iframe(av7110, pic->iFrame, pic->size, 
                            file->f_flags&O_NONBLOCK);
                break;
        }
        
        case VIDEO_FAST_FORWARD:
                //note: arg is ignored by firmware
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Scan_I, 2, AV_PES, 0);
                else 
                        vidcom(av7110, 0x16, arg); 
                av7110->trickmode=TRICK_FAST;
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_SLOWMOTION:
                if (av7110->playing&RP_VIDEO) {
                        outcom(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
                        vidcom(av7110, 0x22, arg);
                } else {
                        vidcom(av7110, 0x0d, 0);
                        vidcom(av7110, 0x0e, 0);
                        vidcom(av7110, 0x22, arg);
                }
                av7110->trickmode=TRICK_SLOW;
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_GET_CAPABILITIES:
                *(int *)parg=VIDEO_CAP_MPEG1|
                        VIDEO_CAP_MPEG2|
                        VIDEO_CAP_SYS|
                        VIDEO_CAP_PROG;
                break;
        
        case VIDEO_CLEAR_BUFFER:
                ring_buffer_flush(&av7110->avout);
                dvb_filter_ipack_reset(&av7110->ipack[1]);
                
                if (av7110->playing==RP_AV) {
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Play, 2, AV_PES, 0);
                        if (av7110->trickmode==TRICK_FAST)
                                outcom(av7110, COMTYPE_REC_PLAY, 
                                       __Scan_I, 2, AV_PES, 0);
                        if (av7110->trickmode==TRICK_SLOW) {
                                outcom(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
                                vidcom(av7110, 0x22, arg);
                        }
                        if (av7110->trickmode==TRICK_FREEZE)
                                vidcom(av7110, 0x000e, 1);
                }
                break;
                
        case VIDEO_SET_STREAMTYPE:
                
                break;
                
        default:
                ret=-ENOIOCTLCMD;
                break;
        }
        return ret;
}

static int 
dvb_audio_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;
        int ret=0;

        if (((file->f_flags&O_ACCMODE)==O_RDONLY) &&
            (cmd!=AUDIO_GET_STATUS))
                return -EPERM;
        
        switch (cmd) {
        case AUDIO_STOP:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY)
                        AV_Stop(av7110, RP_AUDIO);
                else
                        audcom(av7110, 1);
                av7110->audiostate.play_state=AUDIO_STOPPED;
                break;
                
        case AUDIO_PLAY:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY)
                        AV_StartPlay(av7110, RP_AUDIO);
                audcom(av7110, 2);
                av7110->audiostate.play_state=AUDIO_PLAYING;
                break;
                
        case AUDIO_PAUSE:
                audcom(av7110, 1);
                av7110->audiostate.play_state=AUDIO_PAUSED;
                break;
                
        case AUDIO_CONTINUE:
                if (av7110->audiostate.play_state==AUDIO_PAUSED) {
                        av7110->audiostate.play_state=AUDIO_PLAYING;
                        audcom(av7110, 0x12);
                } 
                break;
                
        case AUDIO_SELECT_SOURCE:
                av7110->audiostate.stream_source=(audio_stream_source_t) arg;
                break;
                
        case AUDIO_SET_MUTE:
        {
                audcom(av7110, arg ? 1 : 2);
                av7110->audiostate.mute_state=(int) arg;
                break;
        }
        
        case AUDIO_SET_AV_SYNC:
                av7110->audiostate.AV_sync_state=(int) arg;
                audcom(av7110, arg ? 0x0f : 0x0e);
                break;
                
        case AUDIO_SET_BYPASS_MODE:
                ret=-EINVAL;
                break;
                
        case AUDIO_CHANNEL_SELECT:
                av7110->audiostate.channel_select=(audio_channel_select_t) arg;
                
                switch(av7110->audiostate.channel_select) {
                case AUDIO_STEREO:
                        audcom(av7110, 0x80);
                        break;
                        
                case AUDIO_MONO_LEFT:
                        audcom(av7110, 0x100);
                        break;
                        
                case AUDIO_MONO_RIGHT:
                        audcom(av7110, 0x200);
                        break;
                        
                default:
                        ret=-EINVAL;
                        break;
                }
                break;
                
        case AUDIO_GET_STATUS:
                memcpy(parg, &av7110->audiostate, sizeof(struct audio_status));
                break;
                
        case AUDIO_GET_CAPABILITIES:
                *(int *)parg=AUDIO_CAP_LPCM|
                        AUDIO_CAP_MP1|
                        AUDIO_CAP_MP2;
                break;

        case AUDIO_CLEAR_BUFFER:
                ring_buffer_flush(&av7110->aout);
                dvb_filter_ipack_reset(&av7110->ipack[0]);
                if (av7110->playing==RP_AV)
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Play, 2, AV_PES, 0);
                break;
        case AUDIO_SET_ID:
                
                break;
        case AUDIO_SET_MIXER:
        {
                struct audio_mixer *amix=(struct audio_mixer *)parg;

                SetVolume(av7110, amix->volume_left, amix->volume_right);
                break;
        }
        case AUDIO_SET_STREAMTYPE:
                break;
        default:
                ret=-ENOIOCTLCMD;
                break;
        }
        return ret;
}

static int 
dvb_osd_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

#ifdef CONFIG_DVB_AV7110_OSD
        if (cmd==OSD_SEND_CMD)
                return OSD_DrawCommand(av7110, (osd_cmd_t *)parg);
#endif
        return -EINVAL;
}

static int dvb_video_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        int err;

        if ((err=dvb_generic_open(inode, file))<0)
                return err;
        ring_buffer_flush(&av7110->aout);
        ring_buffer_flush(&av7110->avout);
        av7110->video_blank=1;
        av7110->audiostate.AV_sync_state=1;
        av7110->videostate.stream_source=VIDEO_SOURCE_DEMUX;
        return 0;
}

static int dvb_video_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;

        AV_Stop(av7110, RP_VIDEO);
        return dvb_generic_release(inode, file);
}

static int dvb_audio_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        int err=dvb_generic_open(inode, file);

        if (err<0)
                return err;
        ring_buffer_flush(&av7110->aout);
        av7110->audiostate.stream_source=AUDIO_SOURCE_DEMUX;
        return 0;
}

static int dvb_audio_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        av7110_t *av7110=(av7110_t *) dvbdev->priv;
        
        AV_Stop(av7110, RP_AUDIO);
        return dvb_generic_release(inode, file);
}



/******************************************************************************
 * driver registration 
 ******************************************************************************/

static struct file_operations dvb_video_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_video_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_video_open,
	.release	= dvb_video_release,
	.poll		= dvb_video_poll,
};

static struct dvb_device dvbdev_video = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_video_fops,
	.kernel_ioctl	= dvb_video_ioctl,
};

static struct file_operations dvb_audio_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_audio_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_audio_open,
	.release	= dvb_audio_release,
	.poll		= dvb_audio_poll,
};

static struct dvb_device dvbdev_audio = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_audio_fops,
	.kernel_ioctl	= dvb_audio_ioctl,
};

static struct file_operations dvb_ca_fops = {
	.owner		= THIS_MODULE,
	.read		= dvb_ca_read,
	.write		= dvb_ca_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_ca_open,
	.release	= dvb_generic_release,
	.poll		= dvb_ca_poll,
};

static struct dvb_device dvbdev_ca = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_ca_fops,
	.kernel_ioctl	= dvb_ca_ioctl,
};

static struct file_operations dvb_osd_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
};

static struct dvb_device dvbdev_osd = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_osd_fops,
	.kernel_ioctl	= dvb_osd_ioctl,
};


static
void av7110_before_after_tune (fe_status_t s, void *data)
{
	struct av7110_s *av7110 = data;

        av7110->fe_synced = (s & FE_HAS_LOCK) ? 1 : 0;

        if (av7110->playing)
                return;

        if (down_interruptible(&av7110->pid_mutex))
                return;

	if (av7110->fe_synced) {
                SetPIDs(av7110, av7110->pids[DMX_PES_VIDEO], 
                        av7110->pids[DMX_PES_AUDIO], 
                        av7110->pids[DMX_PES_TELETEXT], 0, 
                        av7110->pids[DMX_PES_PCR]);
        	outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);
	} else 
		SetPIDs(av7110, 0, 0, 0, 0, 0);

        up(&av7110->pid_mutex);
}


static int 
dvb_register(av7110_t *av7110)
{
        int ret, i;
        dmx_frontend_t *dvbfront=&av7110->hw_frontend;
        struct dvb_demux *dvbdemux=&av7110->demux;

        if (av7110->registered)
                return -1;

        av7110->registered=1;

	av7110->dvb_adapter = av7110->saa->dvb_adapter;

	if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) 
                dvb_add_frontend_notifier (av7110->dvb_adapter,
                                           av7110_before_after_tune, av7110);
	/**
	 *   init DiSEqC stuff
	 */
	if (av7110->saa->card_type==DVB_CARD_TT_BUDGET ||
            av7110->saa->card_type==DVB_CARD_TT_SIEMENS) 
		dvb_add_frontend_ioctls (av7110->dvb_adapter,
					 av7110_diseqc_ioctl, NULL, av7110);

	av7110->audiostate.AV_sync_state=0;
	av7110->audiostate.mute_state=0;
	av7110->audiostate.play_state=AUDIO_STOPPED;
	av7110->audiostate.stream_source=AUDIO_SOURCE_DEMUX;
	av7110->audiostate.channel_select=AUDIO_STEREO;
	av7110->audiostate.bypass_mode=0;

	av7110->videostate.video_blank=0;
	av7110->videostate.play_state=VIDEO_STOPPED;
	av7110->videostate.stream_source=VIDEO_SOURCE_DEMUX;
	av7110->videostate.video_format=VIDEO_FORMAT_4_3;
	av7110->videostate.display_format=VIDEO_CENTER_CUT_OUT;
        av7110->display_ar=VIDEO_FORMAT_4_3;

        memcpy(av7110->demux_id, "demux0_0", 9);
        av7110->demux_id[7]=av7110->saa->dvb_adapter->num+0x30;
        dvbdemux->priv=(void *) av7110;

        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) {
                for (i=0; i<32; i++)
                        av7110->handle2filter[i]=NULL;

                dvbdemux->filternum=32;
                dvbdemux->feednum=32;
                dvbdemux->start_feed=dvb_start_feed;
                dvbdemux->stop_feed=dvb_stop_feed;
                dvbdemux->write_to_decoder=dvb_write_to_decoder;
                
                dvbdemux->dmx.vendor="TI";
                dvbdemux->dmx.model="AV7110";
                dvbdemux->dmx.id=av7110->demux_id;
                dvbdemux->dmx.capabilities=(DMX_TS_FILTERING|
                                             DMX_SECTION_FILTERING|
                                             DMX_MEMORY_BASED_FILTERING);

                dvb_dmx_init(&av7110->demux);


                dvbfront->id="hw_frontend";
                dvbfront->vendor="VLSI";
                dvbfront->model="DVB Frontend";
                dvbfront->source=DMX_FRONTEND_0;

                av7110->dmxdev.filternum=32;
                av7110->dmxdev.demux=&dvbdemux->dmx;
                av7110->dmxdev.capabilities=0;

                dvb_dmxdev_init(&av7110->dmxdev, av7110->dvb_adapter);
        }

        if (av7110->saa->card_type>=DVB_CARD_TT_BUDGET) {
                dvbdemux->filternum=256;
                dvbdemux->feednum=256;
                dvbdemux->start_feed=dvb_start_feed;
                dvbdemux->stop_feed=dvb_stop_feed;
                dvbdemux->write_to_decoder=0;

                dvbdemux->dmx.vendor="CIM";
                dvbdemux->dmx.model="sw";
                dvbdemux->dmx.id=av7110->demux_id;
                dvbdemux->dmx.capabilities=(DMX_TS_FILTERING|
                                             DMX_SECTION_FILTERING|
                                             DMX_MEMORY_BASED_FILTERING);

                dvb_dmx_init(&av7110->demux);

                dvbfront->id="hw_frontend";
                dvbfront->vendor="VLSI";
                dvbfront->model="DVB Frontend";
                dvbfront->source=DMX_FRONTEND_0;

                av7110->dmxdev.filternum=256;
                av7110->dmxdev.demux=&dvbdemux->dmx;
                av7110->dmxdev.capabilities=0;

                dvb_dmxdev_init(&av7110->dmxdev, av7110->dvb_adapter);
        }

        ret=dvbdemux->dmx.add_frontend(&dvbdemux->dmx, 
                                        &av7110->hw_frontend);
        if (ret<0)
                return ret;
        
        av7110->mem_frontend.id="mem_frontend";
        av7110->mem_frontend.vendor="memory";
        av7110->mem_frontend.model="sw";
        av7110->mem_frontend.source=DMX_MEMORY_FE;
        ret=dvbdemux->dmx.add_frontend(&dvbdemux->dmx, 
                                        &av7110->mem_frontend);
        if (ret<0)
                return ret;
        
        ret=dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, 
                                            &av7110->hw_frontend);
        if (ret<0)
                return ret;

        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) {
                dvb_register_device(av7110->dvb_adapter, &av7110->video_dev,
                                    &dvbdev_video, av7110, DVB_DEVICE_VIDEO);
                dvb_register_device(av7110->dvb_adapter, &av7110->audio_dev,
                                    &dvbdev_audio, av7110, DVB_DEVICE_AUDIO);
                dvb_register_device(av7110->dvb_adapter, &av7110->osd_dev,
                                    &dvbdev_osd, av7110, DVB_DEVICE_OSD);
                dvb_register_device(av7110->dvb_adapter, &av7110->ca_dev,  
                                    &dvbdev_ca, av7110, DVB_DEVICE_CA);
                vid_register(av7110);
#ifdef USE_DVB_DSP
                dvb->dsp_dev = dvb_register_dsp(dvb_audio_open, 
                                                dvb_audio_release, 
                                                dvb_audio_ioctl, 
                                                dvb_audio_write, 
                                                av7110->audio_dev);
#endif
        }
        
        av7110->dvb_net.card_num=av7110->saa->dvb_adapter->num;
        dvb_net_init(av7110->dvb_adapter, &av7110->dvb_net, &dvbdemux->dmx);

	return 0;
}


static void 
dvb_unregister(av7110_t *av7110)
{
        struct dvb_demux *dvbdemux=&av7110->demux;

        if (!av7110->registered)
                return;

	dvb_net_release(&av7110->dvb_net);

	dvbdemux->dmx.close(&dvbdemux->dmx);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->hw_frontend);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

        dvb_dmxdev_release(&av7110->dmxdev);
        dvb_dmx_release(&av7110->demux);

	if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) 
                dvb_remove_frontend_notifier (av7110->dvb_adapter,
                                              av7110_before_after_tune);

	dvb_remove_frontend_ioctls (av7110->dvb_adapter,
				    av7110_diseqc_ioctl, NULL);

        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) {
                vid_unregister(av7110);
                dvb_unregister_device(av7110->audio_dev);
                dvb_unregister_device(av7110->video_dev);
                dvb_unregister_device(av7110->osd_dev);
                dvb_unregister_device(av7110->ca_dev);
#ifdef USE_DVB_DSP
                dvb_unregister_dsp(av7110->dsp_dev);
#endif
        }
}

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/

static
int av7110_attach (struct saa7146 *saa, void **av7110_ptr)
{
	struct av7110_s *av7110;

	if (!(av7110 = kmalloc (sizeof (struct av7110_s), GFP_KERNEL))) {
		printk ("%s: out of memory!\n", __FUNCTION__);
		return -ENOMEM;
	}

	*av7110_ptr = av7110;

        memset(av7110, 0, sizeof(av7110_t));

	tasklet_init (&av7110->debi_tasklet, debiirq, (unsigned long) av7110);
	tasklet_init (&av7110->gpio_tasklet, gpioirq, (unsigned long) av7110);
	tasklet_init (&av7110->vpe_tasklet, vpeirq, (unsigned long) av7110);
	tasklet_init (&av7110->fidb_tasklet, fidbirq, (unsigned long) av7110);

        sema_init(&av7110->pid_mutex, 1);

        /* locks for data transfers from/to AV7110 */
        spin_lock_init (&av7110->debilock);
        sema_init(&av7110->dcomlock, 1);
        av7110->debilock=SPIN_LOCK_UNLOCKED;
        av7110->debitype=-1;

	av7110->saa=(struct saa7146 *) saa;
        av7110->saa_mem=av7110->saa->mem;

        /* default ADAC type */
        av7110->adac_type = adac;

        /* default OSD window */
        av7110->osdwin=1;

        /* ARM "watchdog" */
	init_waitqueue_head(&av7110->arm_wait);
        av7110->arm_thread=0;
     
        av7110->vidmode=VIDEO_MODE_PAL;

        dvb_filter_ipack_init(&av7110->ipack[0], IPACKS, play_audio_cb);
        av7110->ipack[0].data=(void *) av7110;
        dvb_filter_ipack_init(&av7110->ipack[1], IPACKS, play_video_cb);
        av7110->ipack[1].data=(void *) av7110;
        

        /* allocate and init buffers */

        av7110->debi_virt=pci_alloc_consistent(av7110->saa->device, 8192,
					       &av7110->debi_bus);
        if (!av7110->debi_virt) 
                return -1;

        av7110->iobuf=vmalloc(AVOUTLEN+AOUTLEN+BMPLEN+4*IPACKS);
        if (!av7110->iobuf) 
                return -1;

        ring_buffer_init(&av7110->avout, av7110->iobuf, AVOUTLEN);
        ring_buffer_init(&av7110->aout, av7110->iobuf+AVOUTLEN, AOUTLEN);

        /* init BMP buffer */
        av7110->bmpbuf=av7110->iobuf+AVOUTLEN+AOUTLEN;
        init_waitqueue_head(&av7110->bmpq);
        
        av7110->kbuf[0]=(u8 *)(av7110->iobuf+AVOUTLEN+AOUTLEN+BMPLEN);
        av7110->kbuf[1]=av7110->kbuf[0]+2*IPACKS;

        /* CI link layer buffers */
        ci_ll_init(&av7110->ci_rbuffer, &av7110->ci_wbuffer, 8192);

        /* handle different card types */

        /* load firmware into AV7110 cards */
        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) {
                bootarm(av7110);
                firmversion(av7110);
                if ((av7110->arm_app&0xffff)<0x2502) {
                        printk("av7110: Warning, firmware version is too old. System might be unstable!!!\n");
                }
                kernel_thread(arm_thread, (void *) av7110, 0);
        } else {
                saa7146_write(av7110->saa_mem, DD1_INIT, 0x02000600);
                saa7146_write(av7110->saa_mem, MC2, 
                              (MASK_09 | MASK_25 | MASK_10 | MASK_26));
                setgpio(av7110, 2, GPIO_OUTHI); /* frontend power on */
        }

	SetVolume(av7110, 0xff, 0xff);

        if (av7110->saa->card_type==DVB_CARD_TT_SIEMENS) {
                VidMode(av7110, vidmode);

	        /* remaining inits according to card and frontend type */

                if (i2c_writereg(av7110, 0x20, 0x00, 0x00)==1) {
                        dprintk("av7110%d: Crystal audio DAC detected\n",
				av7110->saa->dvb_adapter->num);
                        av7110->adac_type = DVB_ADAC_CRYSTAL;
                        i2c_writereg(av7110, 0x20, 0x01, 0xd2);
                        i2c_writereg(av7110, 0x20, 0x02, 0x49);
                        i2c_writereg(av7110, 0x20, 0x03, 0x00);
                        i2c_writereg(av7110, 0x20, 0x04, 0x00);
                }
                
		/**
		 * some special handling for the Siemens DVB-C card...
		 */
		if (av7110->saa->device->subsystem_vendor == 0x110a) {
                        if (i2c_writereg(av7110, 0x80, 0x0, 0x80)==1) {
                                i2c_writereg(av7110, 0x80, 0x0, 0);
                                printk("av7110: DVB-C analog module detected, "
                                       "initializing MSP3400\n");
                                ddelay(10);
                                msp_writereg(av7110, 0x12, 0x0013, 0x0c00);
                                msp_writereg(av7110, 0x12, 0x0000, 0x7f00); // loudspeaker + headphone
                                msp_writereg(av7110, 0x12, 0x0008, 0x0220); // loudspeaker source
                                msp_writereg(av7110, 0x12, 0x0004, 0x7f00); // loudspeaker volume
                                msp_writereg(av7110, 0x12, 0x000a, 0x0220); // SCART 1 source
                                msp_writereg(av7110, 0x12, 0x0007, 0x7f00); // SCART 1 volume
                                msp_writereg(av7110, 0x12, 0x000d, 0x4800); // prescale SCART
                        }

                        // switch DVB SCART on
                        outcom(av7110, COMTYPE_AUDIODAC, MainSwitch, 1, 0);
                        outcom(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 1);
                        
                        //setgpio(av7110, 1, GPIO_OUTHI); // RGB on, SCART pin 16
                        //setgpio(av7110, 3, GPIO_OUTLO); // SCARTpin 8
                        av7110->adac_type = DVB_ADAC_NONE;
                }
        }

	if (init_vpid != 0 || init_apid != 0)
		ChangePIDs(av7110, init_vpid, init_apid, 0, 0, 0);

	av7110_setup_irc_config (av7110, 0);
        dvb_register(av7110);

        return 0;
}


static
int av7110_detach (struct saa7146 *saa, void** av7110_ptr)
{
	struct av7110_s *av7110 = *av7110_ptr;

	av7110->arm_rmmod=1;
	wake_up_interruptible(&av7110->arm_wait);

	while (av7110->arm_thread)
		ddelay(1);

	dvb_unregister(av7110);

	saa7146_write (av7110->saa_mem, IER, 
		     saa7146_read(av7110->saa_mem, IER) & ~(MASK_19 | MASK_03));
	
	saa7146_write(av7110->saa_mem, ISR,(MASK_19 | MASK_03));

	ci_ll_release(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
	dvb_filter_ipack_free(&av7110->ipack[0]);
	dvb_filter_ipack_free(&av7110->ipack[1]);
	vfree(av7110->iobuf);
	pci_free_consistent(av7110->saa->device, 8192, av7110->debi_virt,
			    av7110->debi_bus);

	kfree (av7110);
	*av7110_ptr = NULL;

	return 0;
}


static
void av7110_irq(struct saa7146 *saa, u32 isr, void *data) 
{
	struct av7110_s *av7110 = (struct av7110_s*) data;

	if (isr & MASK_19)
		tasklet_schedule (&av7110->debi_tasklet);

	if (isr & MASK_03)
		tasklet_schedule (&av7110->gpio_tasklet);

	if (isr & MASK_10)
		tasklet_schedule (&av7110->vpe_tasklet);

	if (isr & MASK_07)
		tasklet_schedule (&av7110->fidb_tasklet);
}


static
int av7110_command(struct saa7146 *saa, void *p, unsigned int cmd, void *arg)
{
        switch(cmd) {
        case SAA7146_SUSPEND:
                printk("dvb_suspend()\n");
                break;
        case SAA7146_RESUME:
                printk("dvb_resume()\n");
                break;
        default:
                return -ENOIOCTLCMD;
        }
        return 0;
}


static
void av7110_inc_use(struct saa7146* adap)
{
	MOD_INC_USE_COUNT;
}


static
void av7110_dec_use(struct saa7146* adap)
{
	MOD_DEC_USE_COUNT;
}


static struct saa7146_extension av7110_extension = {
	"dvb extension\0",
	MASK_07|MASK_10|MASK_19|MASK_03|MASK_27,
	av7110_irq,
	av7110_command,
	av7110_attach,
	av7110_detach,
	av7110_inc_use,
	av7110_dec_use
};	


int __init av7110_init(void) 
{
	int result = 0;

	if ((result = saa7146_add_extension(&av7110_extension))) {
		printk("%s: saa7146_add_extension() failed!\n",
			__FUNCTION__);
		return result;
	}

        return result;
}


void __exit av7110_exit(void)
{
	if (saa7146_del_extension(&av7110_extension)) 
                printk(KERN_ERR "dvb: extension deregistration failed.\n");
}

//MODULE_DESCRIPTION("driver for the SAA7146 based AV110 PCI DVB cards by "
//		   "Siemens, Technotrend, Hauppauge");
//MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, others");
//MODULE_LICENSE("GPL");

MODULE_PARM(av7110_debug,"i");
MODULE_PARM(vidmem,"l");
MODULE_PARM(vidlow,"l");
MODULE_PARM(vidmode,"i");
MODULE_PARM(init_vpid,"i");
MODULE_PARM(init_apid,"i");
MODULE_PARM(pids_off,"i");
MODULE_PARM(adac,"i");

/*
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

