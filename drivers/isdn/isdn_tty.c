/* $Id: isdn_tty.c,v 1.21 1996/06/24 17:40:28 fritz Exp $
 *
 * Linux ISDN subsystem, tty functions and AT-command emulator (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
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
 * $Log: isdn_tty.c,v $
 * Revision 1.21  1996/06/24 17:40:28  fritz
 * Bugfix: Did not compile without CONFIG_ISDN_AUDIO
 *
 * Revision 1.20  1996/06/15 14:59:39  fritz
 * Fixed isdn_tty_tint() to handle partially sent
 * sk_buffs.
 *
 * Revision 1.19  1996/06/12 15:53:56  fritz
 * Bugfix: AT+VTX and AT+VRX could be executed without
 *         having a connection.
 *         Missing check for NULL tty in isdn_tty_flush_buffer().
 *
 * Revision 1.18  1996/06/07 11:17:33  tsbogend
 * added missing #ifdef CONFIG_ISDN_AUDIO to make compiling without
 * audio support possible
 *
 * Revision 1.17  1996/06/06 14:55:47  fritz
 * Changed to support DTMF decoding on audio playback also.
 * Bugfix: Added check for invalid info->isdn_driver in
 *         isdn_tty_senddown().
 * Clear ncarrier flag on last close() of a tty.
 *
 * Revision 1.16  1996/06/05 02:24:12  fritz
 * Added DTMF decoder for audio mode.
 *
 * Revision 1.15  1996/06/03 20:35:01  fritz
 * Fixed typos.
 *
 * Revision 1.14  1996/06/03 20:12:19  fritz
 * Fixed typos.
 * Added call to write_wakeup via isdn_tty_flush_buffer()
 * in isdn_tty_modem_hup().
 *
 * Revision 1.13  1996/05/31 01:33:29  fritz
 * Changed buffering due to bad performance with mgetty.
 * Now sk_buff is delayed allocated in isdn_tty_senddown
 * using xmit_buff like in standard serial driver.
 * Fixed module locking.
 * Added DLE-DC4 handling in voice mode.
 *
 * Revision 1.12  1996/05/19 01:34:40  fritz
 * Bugfix: ATS returned error.
 *         Register 20 made readonly.
 *
 * Revision 1.11  1996/05/18 01:37:03  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.10  1996/05/17 03:51:49  fritz
 * Changed DLE handling for audio receive.
 *
 * Revision 1.9  1996/05/11 21:52:07  fritz
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.8  1996/05/10 08:49:43  fritz
 * Checkin before major changes of tty-code.
 *
 * Revision 1.7  1996/05/07 09:15:09  fritz
 * Reorganized and general cleanup.
 * Bugfixes:
 *  - Audio-transmit working now.
 *  - "NO CARRIER" now reported, when hanging up with DTR low.
 *  - Corrected CTS handling.
 *
 * Revision 1.6  1996/05/02 03:59:25  fritz
 * Bugfixes:
 *  - On dialout, layer-2 setup had been incomplete
 *    when using new auto-layer2 feature.
 *  - On hangup, "NO CARRIER" message sometimes missing.
 *
 * Revision 1.5  1996/04/30 21:05:25  fritz
 * Test commit
 *
 * Revision 1.4  1996/04/20 16:39:54  fritz
 * Changed all io to go through generic routines in isdn_common.c
 * Fixed a real ugly bug in modem-emulator: 'ATA' had been accepted
 * even when a call has been cancelled from the remote machine.
 *
 * Revision 1.3  1996/02/11 02:12:32  fritz
 * Bugfixes according to similar fixes in standard serial.c of kernel.
 *
 * Revision 1.2  1996/01/22 05:12:25  fritz
 * replaced my_atoi by simple_strtoul
 *
 * Revision 1.1  1996/01/09 04:13:18  fritz
 * Initial revision
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"
#ifdef CONFIG_ISDN_AUDIO
#include "isdn_audio.h"
#define VBUF 0x3e0
#define VBUFX (VBUF/16)
#endif

/* Prototypes */

static int  isdn_tty_edit_at(const char *, int, modem_info *, int);
static void isdn_tty_check_esc(const u_char *, u_char, int, int *, int *, int);
static void isdn_tty_modem_reset_regs(atemu *, int);
static void isdn_tty_cmd_ATA(modem_info *);
static void isdn_tty_at_cout(char *, modem_info *);
static void isdn_tty_flush_buffer(struct tty_struct *);

/* Leave this unchanged unless you know what you do! */
#define MODEM_PARANOIA_CHECK
#define MODEM_DO_RESTART

static char *isdn_ttyname_ttyI = "ttyI";
static char *isdn_ttyname_cui  = "cui";
static int bit2si[8] = {1,5,7,7,7,7,7,7};
static int si2bit[8] = {4,1,4,4,4,4,4,4};
                                
char *isdn_tty_revision        = "$Revision: 1.21 $";

#define DLE 0x10
#define ETX 0x03
#define DC4 0x14

/* isdn_tty_try_read() is called from within isdn_receive_callback()
 * to stuff incoming data directly into a tty's flip-buffer. This
 * is done to speed up tty-receiving if the receive-queue is empty.
 * This routine MUST be called with interrupts off.
 * Return:
 *  1 = Success
 *  0 = Failure, data has to be buffered and later processed by
 *      isdn_tty_readmodem().
 */
int isdn_tty_try_read(modem_info *info, struct sk_buff *skb)
{
        int c;
        int len;
        struct tty_struct *tty;

	if (info->online) {
		if ((tty = info->tty)) {
			if (info->mcr & UART_MCR_RTS) {
				c = TTY_FLIPBUF_SIZE - tty->flip.count;
                                len = skb->len + skb->users;
				if (c >= len) {
                                        if (skb->users)
                                                while (skb->len--) {
                                                        if (*skb->data == DLE)
                                                                tty_insert_flip_char(tty, DLE, 0);
                                                        tty_insert_flip_char(tty, *skb->data++, 0);
                                                }
                                        else {
                                                memcpy(tty->flip.char_buf_ptr,
                                                       skb->data, len);
                                                tty->flip.count += len;
                                                tty->flip.char_buf_ptr += len;
                                                memset(tty->flip.flag_buf_ptr, 0, len);
                                                tty->flip.flag_buf_ptr += len;
                                        }
                                        if (info->emu.mdmreg[12] & 128)
                                                tty->flip.flag_buf_ptr[len - 1] = 0xff;
					queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
                                        skb->free = 1;
                                        kfree_skb(skb, FREE_READ);
					return 1;
				}
			}
		}
	}
	return 0;
}

/* isdn_tty_readmodem() is called periodically from within timer-interrupt.
 * It tries getting received data from the receive queue an stuff it into
 * the tty's flip-buffer.
 */
void isdn_tty_readmodem(void)
{
	int resched = 0;
	int midx;
	int i;
	int c;
	int r;
	ulong flags;
	struct tty_struct *tty;
	modem_info *info;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if ((midx = dev->m_idx[i]) >= 0) {
                        info = &dev->mdm.info[midx];
			if (info->online) {
				r = 0;
#ifdef CONFIG_ISDN_AUDIO
				isdn_audio_eval_dtmf(info);
#endif
				if ((tty = info->tty)) {
					if (info->mcr & UART_MCR_RTS) {
						c = TTY_FLIPBUF_SIZE - tty->flip.count;
						if (c > 0) {
                                                        save_flags(flags);
                                                        cli();
							r = isdn_readbchan(info->isdn_driver, info->isdn_channel,
								      tty->flip.char_buf_ptr,
								      tty->flip.flag_buf_ptr, c, 0);
                                                        /* CISCO AsyncPPP Hack */
							if (!(info->emu.mdmreg[12] & 128))
								memset(tty->flip.flag_buf_ptr, 0, r);
							tty->flip.count += r;
							tty->flip.flag_buf_ptr += r;
							tty->flip.char_buf_ptr += r;
							if (r)
								queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
                                                        restore_flags(flags);
						}
					} else
						r = 1;
				} else
					r = 1;
				if (r) {
					info->rcvsched = 0;
					resched = 1;
				} else
					info->rcvsched = 1;
			}
                }
	}
	if (!resched)
		isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 0);
}

void isdn_tty_cleanup_xmit(modem_info *info)
{
        struct sk_buff *skb;
        unsigned long flags;

        save_flags(flags);
        cli();
        if (skb_queue_len(&info->xmit_queue))
                while ((skb = skb_dequeue(&info->xmit_queue))) {
                        skb->free = 1;
                        kfree_skb(skb, FREE_WRITE);
                }
        if (skb_queue_len(&info->dtmf_queue))
                while ((skb = skb_dequeue(&info->dtmf_queue))) {
                        skb->free = 1;
                        kfree_skb(skb, FREE_WRITE);
                }
        restore_flags(flags);
}

static void isdn_tty_tint(modem_info *info)
{
        struct sk_buff *skb = skb_dequeue(&info->xmit_queue);
        int len, slen;

        if (!skb)
                return;
        len = skb->len;
        if ((slen = isdn_writebuf_skb_stub(info->isdn_driver,
                                         info->isdn_channel, skb)) == len) {
                struct tty_struct *tty = info->tty;
                info->send_outstanding++;
                info->msr |= UART_MSR_CTS;
                info->lsr |= UART_LSR_TEMT;
                if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
                    tty->ldisc.write_wakeup)
                        (tty->ldisc.write_wakeup) (tty);
                wake_up_interruptible(&tty->write_wait);
                return;
        }
        if (slen > 0)
                skb_pull(skb,slen);
        skb_queue_head(&info->xmit_queue, skb);
}

#ifdef CONFIG_ISDN_AUDIO
int isdn_tty_countDLE(unsigned char *buf, int len)
{
        int count = 0;

        while (len--)
                if (*buf++ == DLE)
                        count++;
        return count;
}

/* This routine is called from within isdn_tty_write() to perform
 * DLE-decoding when sending audio-data.
 */
static int isdn_tty_handleDLEdown(modem_info *info, atemu *m, int len)
{
        unsigned char *p = &info->xmit_buf[info->xmit_count];
        int count = 0;

        while (len>0) {
                if (m->lastDLE) {
                        m->lastDLE = 0;
                        switch (*p) {
                                case DLE:
                                        /* Escape code */
                                        if (len>1)
                                                memmove(p,p+1,len-1);
                                        p--;
                                        count++;
                                        break;
                                case ETX:
                                        /* End of data */
                                        info->vonline |= 4;
                                        return count;
                                case DC4:
                                        /* Abort RX */
                                        info->vonline &= ~1;
                                        isdn_tty_at_cout("\020\003", info);
                                        if (!info->vonline)
                                                isdn_tty_at_cout("\r\nVCON\r\n", info);
                                        /* Fall through */
                                case 'q':
                                case 's':
                                        /* Silence */
                                        if (len>1)
                                                memmove(p,p+1,len-1);
                                        p--;
                                        break;
                        }
                } else {
                        if (*p == DLE)
                                m->lastDLE = 1;
                        else
                                count++;
                }
                p++;
                len--;
        }
        if (len<0) {
                printk(KERN_WARNING "isdn_tty: len<0 in DLEdown\n");
                return 0;
        }
        return count;
}

/* This routine is called from within isdn_tty_write() when receiving
 * audio-data. It interrupts receiving, if an character other than
 * ^S or ^Q is sent.
 */
static int isdn_tty_end_vrx(const char *buf, int c, int from_user)
{
	char tmpbuf[VBUF];
        char *p;

        if (c > VBUF) {
                printk(KERN_ERR "isdn_tty: (end_vrx) BUFFER OVERFLOW!!!\n");
                return 1;
        }
	if (from_user) {
		memcpy_fromfs(tmpbuf, buf, c);
                p = tmpbuf;
        } else
                p = (char *)buf;
        while (c--) {
                if ((*p != 0x11) && (*p != 0x13))
                        return 1;
                p++;
        }
        return 0;
}

static int voice_cf[7] = { 1, 1, 4, 3, 2, 1, 1 };

#endif        /* CONFIG_ISDN_AUDIO */

/* isdn_tty_senddown() is called either directly from within isdn_tty_write()
 * or via timer-interrupt from within isdn_tty_modem_xmit(). It pulls
 * outgoing data from the tty's xmit-buffer, handles voice-decompression or
 * T.70 if necessary, and finally queues it up for sending via isdn_tty_tint.
 */
static void isdn_tty_senddown(modem_info * info)
{
        unsigned char *buf = info->xmit_buf;
        int buflen;
        int skb_res;
        struct sk_buff *skb;
        unsigned long flags;

        save_flags(flags);
        cli();
        if (!(buflen = info->xmit_count)) {
                restore_flags(flags);
                return;
        }
        if (info->isdn_driver < 0) {
                info->xmit_count = 0;
                restore_flags(flags);
                return;
        }
        skb_res = dev->drv[info->isdn_driver]->interface->hl_hdrlen + 4;
#ifdef CONFIG_ISDN_AUDIO
        if (info->vonline & 2) {
                /* For now, ifmt is fixed to 1 (alaw), since this
                 * is used with ISDN everywhere in the world, except
                 * US, Canada and Japan.
                 * Later, when US-ISDN protocols are implemented,
                 * this setting will depend on the D-channel protocol.
                 */
                int ifmt = 1;
                int skb_len;
                unsigned char hbuf[VBUF];

                memcpy(hbuf,info->xmit_buf,buflen);
                info->xmit_count = 0;
                restore_flags(flags);
                /* voice conversion/decompression */
                skb_len = buflen * voice_cf[info->emu.vpar[3]];
                skb = dev_alloc_skb(skb_len + skb_res);
                if (!skb) {
                        printk(KERN_WARNING
                               "isdn_tty: Out of memory in ttyI%d senddown\n", info->line);
                        return;
                }
                skb_reserve(skb, skb_res);
                switch (info->emu.vpar[3]) {
                        case 2:
                        case 3:
                        case 4:
                                /* adpcm, compatible to ZyXel 1496 modem
                                 * with ROM revision 6.01
                                 */
                                buflen = isdn_audio_adpcm2xlaw(info->adpcms,
                                                               ifmt,
                                                               hbuf,
                                                               skb_put(skb,skb_len),
                                                               buflen);
                                skb_trim(skb, buflen);
                                break;
                        case 5:
                                /* a-law */
                                if (!ifmt)
                                        isdn_audio_alaw2ulaw(hbuf,buflen);
                                memcpy(skb_put(skb,buflen),hbuf,buflen);
                                break;
                        case 6:
                                /* u-law */
                                if (ifmt)
                                        isdn_audio_ulaw2alaw(hbuf,buflen);
                                memcpy(skb_put(skb,buflen),hbuf,buflen);
                                break;
                }
                if (info->vonline & 4) {
                        info->vonline &= ~6;
                        if (!info->vonline)
                                isdn_tty_at_cout("\r\nVCON\r\n",info);
                }
        } else {
#endif        /* CONFIG_ISDN_AUDIO */
                skb = dev_alloc_skb(buflen + skb_res);
                if (!skb) {
                        printk(KERN_WARNING
                               "isdn_tty: Out of memory in ttyI%d senddown\n", info->line);
                        restore_flags(flags);
                        return;
                }
                skb_reserve(skb, skb_res);
                memcpy(skb_put(skb,buflen),buf,buflen);
                info->xmit_count = 0;
                restore_flags(flags);
#ifdef CONFIG_ISDN_AUDIO
        }
#endif
        skb->free = 1;
        if (info->emu.mdmreg[13] & 2)
                /* Add T.70 simplified header */
                memcpy(skb_push(skb, 4), "\1\0\1\0", 4);
        skb_queue_tail(&info->xmit_queue, skb);
        if ((info->emu.mdmreg[12] & 0x10) != 0)
                info->msr &= UART_MSR_CTS;
        info->lsr &= UART_LSR_TEMT;
}

/************************************************************
 *
 * Modem-functions
 *
 * mostly "stolen" from original Linux-serial.c and friends.
 *
 ************************************************************/

/* The next routine is called once from within timer-interrupt
 * triggered within isdn_tty_modem_ncarrier(). It calls
 * isdn_tty_modem_result() to stuff a "NO CARRIER" Message
 * into the tty's flip-buffer.
 */
static void isdn_tty_modem_do_ncarrier(unsigned long data)
{
        modem_info * info = (modem_info *)data;
        isdn_tty_modem_result(3, info);
}

/* Next routine is called, whenever the DTR-signal is raised.
 * It checks the ncarrier-flag, and triggers the above routine
 * when necessary. The ncarrier-flag is set, whenever DTR goes
 * low.
 */      
static void isdn_tty_modem_ncarrier(modem_info * info)
{
        if (info->ncarrier) {
                info->ncarrier = 0;
                info->nc_timer.expires = jiffies + HZ;
                info->nc_timer.function = isdn_tty_modem_do_ncarrier;
                info->nc_timer.data = (unsigned long)info;
                add_timer(&info->nc_timer);
        }
}

/* isdn_tty_dial() performs dialing of a tty an the necessary
 * setup of the lower levels before that.
 */
static void isdn_tty_dial(char *n, modem_info * info, atemu * m)
{
        int usg = ISDN_USAGE_MODEM;
        int si = 7;
        int l2 = m->mdmreg[14];
	isdn_ctrl cmd;
	ulong flags;
	int i;
        int j;

        for (j=7;j>=0;j--)
                if (m->mdmreg[18] & (1<<j)) {
                        si = bit2si[j];
                        break;
                }
#ifdef CONFIG_ISDN_AUDIO
                if (si == 1) {
                        l2 = 4;
                        usg = ISDN_USAGE_VOICE;
                }
#endif
        m->mdmreg[20] = si2bit[si];
	save_flags(flags);
	cli();
	i = isdn_get_free_channel(usg, l2, m->mdmreg[15], -1, -1);
	if (i < 0) {
		restore_flags(flags);
		isdn_tty_modem_result(6, info);
	} else {
                info->isdn_driver = dev->drvmap[i];
                info->isdn_channel = dev->chanmap[i];
                info->drv_index = i;
                dev->m_idx[i] = info->line;
                dev->usage[i] |= ISDN_USAGE_OUTGOING;
                isdn_info_update();
                restore_flags(flags);
                cmd.driver = info->isdn_driver;
                cmd.arg = info->isdn_channel;
                cmd.command = ISDN_CMD_CLREAZ;
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                strcpy(cmd.num, isdn_map_eaz2msn(m->msn, info->isdn_driver));
                cmd.driver = info->isdn_driver;
                cmd.command = ISDN_CMD_SETEAZ;
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                cmd.driver = info->isdn_driver;
                cmd.command = ISDN_CMD_SETL2;
                cmd.arg = info->isdn_channel + (l2 << 8);
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                cmd.driver = info->isdn_driver;
                cmd.command = ISDN_CMD_SETL3;
                cmd.arg = info->isdn_channel + (m->mdmreg[15] << 8);
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                cmd.driver = info->isdn_driver;
                cmd.arg = info->isdn_channel;
                sprintf(cmd.num, "%s,%s,%d,%d", n, isdn_map_eaz2msn(m->msn, info->isdn_driver),
                        si, m->mdmreg[19]);
                cmd.command = ISDN_CMD_DIAL;
                info->dialing = 1;
                strcpy(dev->num[i], n);
                isdn_info_update();
                dev->drv[info->isdn_driver]->interface->command(&cmd);
	}
}

/* isdn_tty_hangup() disassociates a tty from the real
 * ISDN-line (hangup). The usage-status is cleared
 * and some cleanup is done also.
 */
void isdn_tty_modem_hup(modem_info * info)
{
	isdn_ctrl cmd;
        int usage;

        if (!info)
                return;
#ifdef ISDN_DEBUG_MODEM_HUP
        printk(KERN_DEBUG "Mhup ttyI%d\n", info->line);
#endif
        info->rcvsched = 0;
        info->online = 0;
        isdn_tty_flush_buffer(info->tty);
        if (info->vonline & 1) {
                /* voice-recording, add DLE-ETX */
                isdn_tty_at_cout("\020\003", info);
        }
        if (info->vonline & 2) {
                /* voice-playing, add DLE-DC4 */
                isdn_tty_at_cout("\020\024", info);
        }
        info->vonline = 0;
#ifdef CONFIG_ISDN_AUDIO
        if (info->dtmf_state) {
                kfree(info->dtmf_state);
                info->dtmf_state = NULL;
        }
        if (info->adpcms) {
                kfree(info->adpcms);
                info->adpcms = NULL;
        }
        if (info->adpcmr) {
                kfree(info->adpcmr);
                info->adpcmr = NULL;
        }
#endif
        info->msr &= ~(UART_MSR_DCD | UART_MSR_RI);
        info->lsr |= UART_LSR_TEMT;
	if (info->isdn_driver >= 0) {
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_HANGUP;
		cmd.arg = info->isdn_channel;
		dev->drv[info->isdn_driver]->interface->command(&cmd);
		isdn_all_eaz(info->isdn_driver, info->isdn_channel);
                usage = (info->emu.mdmreg[20] == 1)?
                        ISDN_USAGE_VOICE:ISDN_USAGE_MODEM;
		isdn_free_channel(info->isdn_driver, info->isdn_channel,
                                  usage);
	}
	info->isdn_driver = -1;
	info->isdn_channel = -1;
        if (info->drv_index >= 0) {
                dev->m_idx[info->drv_index] = -1;
                info->drv_index = -1;
        }
}

static inline int isdn_tty_paranoia_check(modem_info * info, kdev_t device, const char *routine)
{
#ifdef MODEM_PARANOIA_CHECK
	if (!info) {
		printk(KERN_WARNING "isdn_tty: null info_struct for (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (info->magic != ISDN_ASYNC_MAGIC) {
		printk(KERN_WARNING "isdn_tty: bad magic for modem struct (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void isdn_tty_change_speed(modem_info * info)
{
	uint cflag, cval, fcr, quot;
	int i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	quot = i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (quot) {
		info->mcr |= UART_MCR_DTR;
                isdn_tty_modem_ncarrier(info);                
	} else {
		info->mcr &= ~UART_MCR_DTR;
                if (info->emu.mdmreg[13] & 4) {
#ifdef ISDN_DEBUG_MODEM_HUP
                        printk(KERN_DEBUG "Mhup in changespeed\n");
#endif
                        if (info->online)
                                info->ncarrier = 1;
                        isdn_tty_modem_reset_regs(&info->emu, 0);
                        isdn_tty_modem_hup(info);
                }
                return;
	}
	/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	fcr = 0;

	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS) {
		info->flags |= ISDN_ASYNC_CTS_FLOW;
	} else
		info->flags &= ~ISDN_ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ISDN_ASYNC_CHECK_CD;
	else {
		info->flags |= ISDN_ASYNC_CHECK_CD;
	}
}

static int isdn_tty_startup(modem_info * info)
{
	ulong flags;

	if (info->flags & ISDN_ASYNC_INITIALIZED)
		return 0;
	save_flags(flags);
	cli();
        isdn_MOD_INC_USE_COUNT();
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "starting up ttyi%d ...\n", info->line);
#endif
	/*
	 * Now, initialize the UART 
	 */
	info->mcr = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	/*
	 * and set the speed of the serial port
	 */
	isdn_tty_change_speed(info);

	info->flags |= ISDN_ASYNC_INITIALIZED;
	info->msr |= (UART_MSR_DSR | UART_MSR_CTS);
	info->send_outstanding = 0;
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void isdn_tty_shutdown(modem_info * info)
{
	ulong flags;

	if (!(info->flags & ISDN_ASYNC_INITIALIZED))
		return;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "Shutting down isdnmodem port %d ....\n", info->line);
#endif
	save_flags(flags);
	cli();			/* Disable interrupts */
        isdn_MOD_DEC_USE_COUNT();
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		info->mcr &= ~(UART_MCR_DTR | UART_MCR_RTS);
                if (info->emu.mdmreg[13] & 4) {
                        isdn_tty_modem_reset_regs(&info->emu, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
                        printk(KERN_DEBUG "Mhup in isdn_tty_shutdown\n");
#endif
                        isdn_tty_modem_hup(info);
                }
	}
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ISDN_ASYNC_INITIALIZED;
	restore_flags(flags);
}

/* isdn_tty_write() is the main send-routine. It is called from the upper
 * levels within the kernel to perform sending data. Depending on the
 * online-flag it either directs output to the at-command-interpreter or
 * to the lower level. Additional tasks done here:
 *  - If online, check for escape-sequence (+++)
 *  - If sending audio-data, call isdn_tty_DLEdown() to parse DLE-codes.
 *  - If receiving audio-data, call isdn_tty_end_vrx() to abort if needed.
 *  - If dialing, abort dial.
 */
static int isdn_tty_write(struct tty_struct *tty, int from_user, const u_char * buf, int count)
{
	int c, total = 0;
	ulong flags;
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write"))
		return 0;
	if (!tty)
		return 0;
        save_flags(flags);
        cli();
	while (1) {
		c = MIN(count, info->xmit_size - info->xmit_count);
		if (info->isdn_driver >= 0)
			c = MIN(c, dev->drv[info->isdn_driver]->maxbufsize);
		if (c <= 0)
			break;
                if ((info->online > 1) ||
                    (info->vonline & 2)) {
                        atemu *m = &info->emu;

                        if (!(info->vonline & 2))
                                isdn_tty_check_esc(buf, m->mdmreg[2], c,
                                                   &(m->pluscount),
                                                   &(m->lastplus),
                                                   from_user);
                        if (from_user)
                                memcpy_fromfs(&(info->xmit_buf[info->xmit_count]), buf, c);
                        else
                                memcpy(&(info->xmit_buf[info->xmit_count]), buf, c);
#ifdef CONFIG_ISDN_AUDIO
                        if (info->vonline & 2) {
                                int cc;

                                if (!(cc = isdn_tty_handleDLEdown(info,m,c))) {
                                        /* If DLE decoding results in zero-transmit, but
                                         * c originally was non-zero, do a wakeup.
                                         */
                                        if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
                                            tty->ldisc.write_wakeup)
                                                (tty->ldisc.write_wakeup) (tty);
                                        wake_up_interruptible(&tty->write_wait);
                                        info->msr |= UART_MSR_CTS;
                                        info->lsr |= UART_LSR_TEMT;
                                }
                                info->xmit_count += cc;
                        } else
#endif
                                info->xmit_count += c;
			if (m->mdmreg[13] & 1) {
                                isdn_tty_senddown(info);
                                isdn_tty_tint(info);
                        }
		} else {
                        info->msr |= UART_MSR_CTS;
                        info->lsr |= UART_LSR_TEMT;
#ifdef CONFIG_ISDN_AUDIO
                        if (info->vonline & 1) {
                                if (isdn_tty_end_vrx(buf, c, from_user)) {
                                        info->vonline &= ~1;
                                        isdn_tty_at_cout("\020\003\r\nVCON\r\n", info);
                                }
                        } else
#endif
                                if (info->dialing) {
                                        info->dialing = 0;
#ifdef ISDN_DEBUG_MODEM_HUP
                                        printk(KERN_DEBUG "Mhup in isdn_tty_write\n");
#endif
                                        isdn_tty_modem_result(3, info);
                                        isdn_tty_modem_hup(info);
                                } else
                                        c = isdn_tty_edit_at(buf, c, info, from_user);
		}
		buf += c;
		count -= c;
		total += c;
	}
	if ((info->xmit_count) || (skb_queue_len(&info->xmit_queue)))
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
        restore_flags(flags);
	return total;
}

static int isdn_tty_write_room(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int ret;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write_room"))
		return 0;
	if (!info->online)
		return info->xmit_size;
	ret = info->xmit_size - info->xmit_count;
	return (ret < 0) ? 0 : ret;
}

static int isdn_tty_chars_in_buffer(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_chars_in_buffer"))
		return 0;
	if (!info->online)
		return 0;
	return (info->xmit_count);
}

static void isdn_tty_flush_buffer(struct tty_struct *tty)
{
	modem_info *info;
        unsigned long flags;

        save_flags(flags);
        cli();
        if (!tty) {
                restore_flags(flags);
                return;
        }
        info =  (modem_info *) tty->driver_data;
	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_buffer")) {
                restore_flags(flags);
		return;
        }
        isdn_tty_cleanup_xmit(info);
        info->xmit_count = 0;
        restore_flags(flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup) (tty);
}

static void isdn_tty_flush_chars(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_chars"))
		return;
	if ((info->xmit_count) || (skb_queue_len(&info->xmit_queue)))
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
}

/*
 * ------------------------------------------------------------
 * isdn_tty_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void isdn_tty_throttle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_throttle"))
		return;
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);
	info->mcr &= ~UART_MCR_RTS;
}

static void isdn_tty_unthrottle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_unthrottle"))
		return;
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}
	info->mcr |= UART_MCR_RTS;
}

/*
 * ------------------------------------------------------------
 * isdn_tty_ioctl() and friends
 * ------------------------------------------------------------
 */

/*
 * isdn_tty_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows RS485 driver to be written in user space. 
 */
static int isdn_tty_get_lsr_info(modem_info * info, uint * value)
{
	u_char status;
	uint result;
	ulong flags;

	save_flags(flags);
	cli();
	status = info->lsr;
	restore_flags(flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	put_user(result, (ulong *) value);
	return 0;
}


static int isdn_tty_get_modem_info(modem_info * info, uint * value)
{
	u_char control, status;
	uint result;
	ulong flags;

	control = info->mcr;
	save_flags(flags);
	cli();
	status = info->msr;
	restore_flags(flags);
	result = ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
	    | ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
	    | ((status & UART_MSR_DCD) ? TIOCM_CAR : 0)
	    | ((status & UART_MSR_RI) ? TIOCM_RNG : 0)
	    | ((status & UART_MSR_DSR) ? TIOCM_DSR : 0)
	    | ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
	put_user(result, (ulong *) value);
	return 0;
}

static int isdn_tty_set_modem_info(modem_info * info, uint cmd, uint * value)
{
	uint arg = get_user((uint *) value);
        int pre_dtr;

	switch (cmd) {
                case TIOCMBIS:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCMBIS\n", info->line);
#endif
                        if (arg & TIOCM_RTS) {
                                info->mcr |= UART_MCR_RTS;
                        }
                        if (arg & TIOCM_DTR) {
                                info->mcr |= UART_MCR_DTR;
                                isdn_tty_modem_ncarrier(info);
                        }
                        break;
                case TIOCMBIC:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCMBIC\n", info->line);
#endif
                        if (arg & TIOCM_RTS) {
                                info->mcr &= ~UART_MCR_RTS;
                        }
                        if (arg & TIOCM_DTR) {
                                info->mcr &= ~UART_MCR_DTR;
                                if (info->emu.mdmreg[13] & 4) {
                                        isdn_tty_modem_reset_regs(&info->emu, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
                                        printk(KERN_DEBUG "Mhup in TIOCMBIC\n");
#endif
                                        if (info->online)
                                                info->ncarrier = 1;
                                        isdn_tty_modem_hup(info);
                                }
                        }
                        break;
                case TIOCMSET:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCMSET\n", info->line);
#endif
                        pre_dtr = (info->mcr & UART_MCR_DTR);
                        info->mcr = ((info->mcr & ~(UART_MCR_RTS | UART_MCR_DTR))
                                     | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
                                     | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
                        if (pre_dtr |= (info->mcr & UART_MCR_DTR)) {
                                if (!(info->mcr & UART_MCR_DTR)) {
                                        if (info->emu.mdmreg[13] & 4) {
                                                isdn_tty_modem_reset_regs(&info->emu, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
                                                printk(KERN_DEBUG "Mhup in TIOCMSET\n");
#endif
                                                if (info->online)
                                                        info->ncarrier = 1;
                                                isdn_tty_modem_hup(info);
                                        }
                                } else
                                        isdn_tty_modem_ncarrier(info);
                        }
                        break;
                default:
                        return -EINVAL;
	}
	return 0;
}

static int isdn_tty_ioctl(struct tty_struct *tty, struct file *file,
		       uint cmd, ulong arg)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int error;
	int retval;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_ioctl"))
		return -ENODEV;
        if (tty->flags & (1 << TTY_IO_ERROR))
                return -EIO;
	switch (cmd) {
                case TCSBRK:		/* SVID version: non-zero arg --> no break */
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TCSBRK\n", info->line);
#endif
                        retval = tty_check_change(tty);
                        if (retval)
                                return retval;
                        tty_wait_until_sent(tty, 0);
                        return 0;
                case TCSBRKP:		/* support for POSIX tcsendbreak() */
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TCSBRKP\n", info->line);
#endif
                        retval = tty_check_change(tty);
                        if (retval)
                                return retval;
                        tty_wait_until_sent(tty, 0);
                        return 0;
                case TIOCGSOFTCAR:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCGSOFTCAR\n", info->line);
#endif
                        error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long));
                        if (error)
                                return error;
                        put_user(C_CLOCAL(tty) ? 1 : 0, (ulong *) arg);
                        return 0;
                case TIOCSSOFTCAR:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCSSOFTCAR\n", info->line);
#endif
                        error = verify_area(VERIFY_READ, (void *) arg, sizeof(long));
                        if (error)
                                return error;
                        arg = get_user((ulong *) arg);
                        tty->termios->c_cflag =
                                ((tty->termios->c_cflag & ~CLOCAL) |
                                 (arg ? CLOCAL : 0));
                        return 0;
                case TIOCMGET:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCMGET\n", info->line);
#endif
                        error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
                        if (error)
                                return error;
                        return isdn_tty_get_modem_info(info, (uint *) arg);
                case TIOCMBIS:
                case TIOCMBIC:
                case TIOCMSET:
                        error = verify_area(VERIFY_READ, (void *) arg, sizeof(uint));
                        if (error)
                                return error;
                        return isdn_tty_set_modem_info(info, cmd, (uint *) arg);
                case TIOCSERGETLSR:	/* Get line status register */
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "ttyI%d ioctl TIOCSERGETLSR\n", info->line);
#endif
                        error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
                        if (error)
                                return error;
                        else
                                return isdn_tty_get_lsr_info(info, (uint *) arg);
                        
                default:
#ifdef ISDN_DEBUG_MODEM_IOCTL
                        printk(KERN_DEBUG "UNKNOWN ioctl 0x%08x on ttyi%d\n", cmd, info->line);
#endif
                        return -ENOIOCTLCMD;
	}
	return 0;
}

static void isdn_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	modem_info *info = (modem_info *) tty->driver_data;

        if (!old_termios)
                isdn_tty_change_speed(info);
        else {
                if (tty->termios->c_cflag == old_termios->c_cflag)
                        return;
                isdn_tty_change_speed(info);
                if ((old_termios->c_cflag & CRTSCTS) &&
                    !(tty->termios->c_cflag & CRTSCTS)) {
                        tty->hw_stopped = 0;
                }
        }
}

/*
 * ------------------------------------------------------------
 * isdn_tty_open() and friends
 * ------------------------------------------------------------
 */
static int isdn_tty_block_til_ready(struct tty_struct *tty, struct file *filp, modem_info * info)
{
	struct wait_queue wait = {current, NULL};
	int do_clocal = 0;
	unsigned long flags;
	int retval;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ISDN_ASYNC_CLOSING)) {
                if (info->flags & ISDN_ASYNC_CLOSING)
                        interruptible_sleep_on(&info->close_wait);
#ifdef MODEM_DO_RESTART
		if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}
	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == ISDN_SERIAL_TYPE_CALLOUT) {
		if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ISDN_ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
            (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) {
		if (info->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * isdn_tty_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready before block: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
        save_flags(flags);
        cli();
        if (!(tty_hung_up_p(filp)))
                info->count--;
        restore_flags(flags);
	info->blocked_open++;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ISDN_ASYNC_INITIALIZED)) {
#ifdef MODEM_DO_RESTART
			if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ISDN_ASYNC_CLOSING) &&
		    (do_clocal || (info->msr & UART_MSR_DCD))) {
			break;
		}
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_block_til_ready blocking: ttyi%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready after blocking: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int isdn_tty_open(struct tty_struct *tty, struct file *filp)
{
	modem_info *info;
	int retval, line;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if (line < 0 || line > ISDN_MAX_CHANNELS)
		return -ENODEV;
	info = &dev->mdm.info[line];
	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_open"))
		return -ENODEV;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open %s%d, count = %d\n", tty->driver.name,
	       info->line, info->count);
#endif
	info->count++;
	tty->driver_data = info;
	info->tty = tty;
	/*
	 * Start up serial port
	 */
	retval = isdn_tty_startup(info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after startup\n");
#endif
		return retval;
	}
	retval = isdn_tty_block_til_ready(tty, filp, info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after isdn_tty_block_til_ready \n");
#endif
		return retval;
	}
	if ((info->count == 1) && (info->flags & ISDN_ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == ISDN_SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else
			*tty->termios = info->callout_termios;
		isdn_tty_change_speed(info);
	}
	info->session = current->session;
	info->pgrp = current->pgrp;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open ttyi%d successful...\n", info->line);
#endif
	dev->modempoll++;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open normal exit\n");
#endif
	return 0;
}

static void isdn_tty_close(struct tty_struct *tty, struct file *filp)
{
	modem_info *info = (modem_info *) tty->driver_data;
	ulong flags;
	ulong timeout;

	if (!info || isdn_tty_paranoia_check(info, tty->device, "isdn_tty_close"))
		return;
	save_flags(flags);
	cli();
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close return after tty_hung_up_p\n");
#endif
		return;
	}
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "isdn_tty_close: bad port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "isdn_tty_close: bad port count for ttyi%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close after info->count != 0\n");
#endif
		return;
	}
	info->flags |= ISDN_ASYNC_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;

        tty->closing = 1;
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	if (info->flags & ISDN_ASYNC_INITIALIZED) {
		tty_wait_until_sent(tty, 3000);		/* 30 seconds timeout */
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies + HZ;
		while (!(info->lsr & UART_LSR_TEMT)) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 20;
			schedule();
			if (jiffies > timeout)
				break;
		}
	}
	dev->modempoll--;
	isdn_tty_shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	info->tty = 0;
        info->ncarrier = 0;
	tty->closing = 0;
	if (info->blocked_open) {
                current->state = TASK_INTERRUPTIBLE;
                current->timeout = jiffies + 50;
                schedule();
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE |
			 ISDN_ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_close normal exit\n");
#endif
}

/*
 * isdn_tty_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void isdn_tty_hangup(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_hangup"))
		return;
	isdn_tty_shutdown(info);
	info->count = 0;
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/* This routine initializes all emulator-data.
 */
static void isdn_tty_reset_profile(atemu * m)
{
	m->profile[0] = 0;
	m->profile[1] = 0;
	m->profile[2] = 43;
	m->profile[3] = 13;
	m->profile[4] = 10;
	m->profile[5] = 8;
	m->profile[6] = 3;
	m->profile[7] = 60;
	m->profile[8] = 2;
	m->profile[9] = 6;
	m->profile[10] = 7;
	m->profile[11] = 70;
	m->profile[12] = 0x45;
	m->profile[13] = 4;
	m->profile[14] = ISDN_PROTO_L2_X75I;
	m->profile[15] = ISDN_PROTO_L3_TRANS;
	m->profile[16] = ISDN_SERIAL_XMIT_SIZE / 16;
	m->profile[17] = ISDN_MODEM_WINSIZE;
	m->profile[18] = 4;
	m->profile[19] = 0;
	m->profile[20] = 0;
	m->pmsn[0] = '\0';
}

static void isdn_tty_modem_reset_vpar(atemu *m)
{
        m->vpar[0] = 2;  /* Voice-device            (2 = phone line) */
        m->vpar[1] = 0;  /* Silence detection level (0 = none      ) */
        m->vpar[2] = 70; /* Silence interval        (7 sec.        ) */
        m->vpar[3] = 2;  /* Compression type        (1 = ADPCM-2   ) */
}

static void isdn_tty_modem_reset_regs(atemu * m, int force)
{
	if ((m->mdmreg[12] & 32) || force) {
		memcpy(m->mdmreg, m->profile, ISDN_MODEM_ANZREG);
		memcpy(m->msn, m->pmsn, ISDN_MSNLEN);
	}
        isdn_tty_modem_reset_vpar(m);
	m->mdmcmdl = 0;
}

static void modem_write_profile(atemu * m)
{
	memcpy(m->profile, m->mdmreg, ISDN_MODEM_ANZREG);
	memcpy(m->pmsn, m->msn, ISDN_MSNLEN);
	if (dev->profd)
		send_sig(SIGIO, dev->profd, 1);
}

int isdn_tty_modem_init(void)
{
	modem *m;
	int i;
	modem_info *info;

	m = &dev->mdm;
	memset(&m->tty_modem, 0, sizeof(struct tty_driver));
	m->tty_modem.magic = TTY_DRIVER_MAGIC;
	m->tty_modem.name = isdn_ttyname_ttyI;
	m->tty_modem.major = ISDN_TTY_MAJOR;
	m->tty_modem.minor_start = 0;
	m->tty_modem.num = ISDN_MAX_CHANNELS;
	m->tty_modem.type = TTY_DRIVER_TYPE_SERIAL;
	m->tty_modem.subtype = ISDN_SERIAL_TYPE_NORMAL;
	m->tty_modem.init_termios = tty_std_termios;
	m->tty_modem.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	m->tty_modem.flags = TTY_DRIVER_REAL_RAW;
	m->tty_modem.refcount = &m->refcount;
	m->tty_modem.table = m->modem_table;
	m->tty_modem.termios = m->modem_termios;
	m->tty_modem.termios_locked = m->modem_termios_locked;
	m->tty_modem.open = isdn_tty_open;
	m->tty_modem.close = isdn_tty_close;
	m->tty_modem.write = isdn_tty_write;
	m->tty_modem.put_char = NULL;
	m->tty_modem.flush_chars = isdn_tty_flush_chars;
	m->tty_modem.write_room = isdn_tty_write_room;
	m->tty_modem.chars_in_buffer = isdn_tty_chars_in_buffer;
	m->tty_modem.flush_buffer = isdn_tty_flush_buffer;
	m->tty_modem.ioctl = isdn_tty_ioctl;
	m->tty_modem.throttle = isdn_tty_throttle;
	m->tty_modem.unthrottle = isdn_tty_unthrottle;
	m->tty_modem.set_termios = isdn_tty_set_termios;
	m->tty_modem.stop = NULL;
	m->tty_modem.start = NULL;
	m->tty_modem.hangup = isdn_tty_hangup;
	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	m->cua_modem = m->tty_modem;
	m->cua_modem.name = isdn_ttyname_cui;
	m->cua_modem.major = ISDN_TTYAUX_MAJOR;
	m->tty_modem.minor_start = 0;
	m->cua_modem.subtype = ISDN_SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&m->tty_modem)) {
		printk(KERN_WARNING "isdn_tty: Couldn't register modem-device\n");
		return -1;
	}
	if (tty_register_driver(&m->cua_modem)) {
		printk(KERN_WARNING "isdn_tty: Couldn't register modem-callout-device\n");
		return -2;
	}
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		info = &m->info[i];
		isdn_tty_reset_profile(&info->emu);
		isdn_tty_modem_reset_regs(&info->emu, 1);
		info->magic = ISDN_ASYNC_MAGIC;
		info->line = i;
		info->tty = 0;
		info->x_char = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->callout_termios = m->cua_modem.init_termios;
		info->normal_termios = m->tty_modem.init_termios;
		info->open_wait = 0;
		info->close_wait = 0;
		info->isdn_driver = -1;
		info->isdn_channel = -1;
		info->drv_index = -1;
		info->xmit_size = ISDN_SERIAL_XMIT_SIZE;
                skb_queue_head_init(&info->xmit_queue);
                skb_queue_head_init(&info->dtmf_queue);
                if (!(info->xmit_buf = kmalloc(ISDN_SERIAL_XMIT_SIZE + 5, GFP_KERNEL))) {
                        printk(KERN_ERR "Could not allocate modem xmit-buffer\n");
                        return -3;
                }
                /* Make room for T.70 header */
                info->xmit_buf += 4;
	}
	return 0;
}

/*
 * An incoming call-request has arrived.
 * Search the tty-devices for an appropriate device and bind
 * it to the ISDN-Channel.
 * Return Index to dev->mdm or -1 if none found.
 */
int isdn_tty_find_icall(int di, int ch, char *num)
{
	char *eaz;
	int i;
	int idx;
	int si1;
	int si2;
	char *s;
	char nr[31];
	ulong flags;

	save_flags(flags);
	cli();
	if (num[0] == ',') {
		nr[0] = '0';
		strncpy(&nr[1], num, 29);
		printk(KERN_INFO "isdn_tty: Incoming call without OAD, assuming '0'\n");
	} else
		strncpy(nr, num, 30);
	s = strtok(nr, ",");
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn_tty: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return -1;
	}
	si1 = (int)simple_strtoul(s,NULL,10);
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn_tty: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return -1;
	}
	si2 = (int)simple_strtoul(s,NULL,10);
	eaz = strtok(NULL, ",");
	if (!eaz) {
		printk(KERN_WARNING "isdn_tty: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	}
#ifdef ISDN_DEBUG_MODEM_ICALL
	printk(KERN_DEBUG "m_fi: eaz=%s si1=%d si2=%d\n", eaz, si1, si2);
#endif
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                modem_info *info = &dev->mdm.info[i];
#ifdef ISDN_DEBUG_MODEM_ICALL
		printk(KERN_DEBUG "m_fi: i=%d msn=%s mmsn=%s mreg18=%d mreg19=%d\n", i,
		       info->emu.msn, isdn_map_eaz2msn(info->emu.msn, di),
		       info->emu.mdmreg[18], info->emu.mdmreg[19]);
#endif
		if ((!strcmp(isdn_map_eaz2msn(info->emu.msn, di),
                             eaz)) &&                             /* EAZ is matching      */
		    (info->emu.mdmreg[18] & si2bit[si1]) &&       /* SI1 is matching      */
		    ((info->emu.mdmreg[19] == si2) || !si2)) {    /* SI2 is matching or 0 */
			idx = isdn_dc2minor(di, ch);
#ifdef ISDN_DEBUG_MODEM_ICALL
			printk(KERN_DEBUG "m_fi: match1\n");
			printk(KERN_DEBUG "m_fi: idx=%d flags=%08lx drv=%d ch=%d usg=%d\n", idx,
			       info->flags, info->isdn_driver, info->isdn_channel,
			       dev->usage[idx]);
#endif
			if ((info->flags & ISDN_ASYNC_NORMAL_ACTIVE) &&
			    (info->isdn_driver == -1) &&
			    (info->isdn_channel == -1) &&
			    (USG_NONE(dev->usage[idx]))) {
				info->isdn_driver = di;
				info->isdn_channel = ch;
				info->drv_index = idx;
				dev->m_idx[idx] = info->line;
				dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
				dev->usage[idx] |= (si1==1)?ISDN_USAGE_VOICE:ISDN_USAGE_MODEM;
				strcpy(dev->num[idx], nr);
                                info->emu.mdmreg[20] = si2bit[si1];
				isdn_info_update();
				restore_flags(flags);
				printk(KERN_INFO "isdn_tty: call from %s, -> RING on ttyI%d\n", nr,
				       info->line);
				return info->line;
			}
		}
	}
	printk(KERN_INFO "isdn_tty: call from %s -> %s %s\n", nr, eaz,
	       dev->drv[di]->reject_bus ? "rejected" : "ignored");
	restore_flags(flags);
	return -1;
}

/*********************************************************************
 Modem-Emulator-Routines
 *********************************************************************/

#define cmdchar(c) ((c>' ')&&(c<=0x7f))

/*
 * Put a message from the AT-emulator into receive-buffer of tty,
 * convert CR, LF, and BS to values in modem-registers 3, 4 and 5.
 */
static void isdn_tty_at_cout(char *msg, modem_info * info)
{
	struct tty_struct *tty;
	atemu *m = &info->emu;
	char *p;
	char c;
	ulong flags;

	if (!msg) {
		printk(KERN_WARNING "isdn_tty: Null-Message in isdn_tty_at_cout\n");
		return;
	}
	save_flags(flags);
	cli();
	tty = info->tty;
	for (p = msg; *p; p++) {
		switch (*p) {
                        case '\r':
                                c = m->mdmreg[3];
                                break;
                        case '\n':
                                c = m->mdmreg[4];
                                break;
                        case '\b':
                                c = m->mdmreg[5];
                                break;
                        default:
                                c = *p;
		}
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!tty)) {
			restore_flags(flags);
			return;
		}
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;
		tty_insert_flip_char(tty, c, 0);
	}
	restore_flags(flags);
	queue_task(&tty->flip.tqueue, &tq_timer);
}

/*
 * Perform ATH Hangup
 */
static void isdn_tty_on_hook(modem_info * info)
{
	if (info->isdn_channel >= 0) {
#ifdef ISDN_DEBUG_MODEM_HUP
		printk(KERN_DEBUG "Mhup in isdn_tty_on_hook\n");
#endif
		isdn_tty_modem_result(3, info);
		isdn_tty_modem_hup(info);
	}
}

static void isdn_tty_off_hook(void)
{
	printk(KERN_DEBUG "isdn_tty_off_hook\n");
}

#define PLUSWAIT1 (HZ/2)	/* 0.5 sec. */
#define PLUSWAIT2 (HZ*3/2)	/* 1.5 sec */

/*
 * Check Buffer for Modem-escape-sequence, activate timer-callback to
 * isdn_tty_modem_escape() if sequence found.
 *
 * Parameters:
 *   p          pointer to databuffer
 *   plus       escape-character
 *   count      length of buffer
 *   pluscount  count of valid escape-characters so far
 *   lastplus   timestamp of last character
 */
static void isdn_tty_check_esc(const u_char * p, u_char plus, int count, int *pluscount,
			   int *lastplus, int from_user)
{
	char cbuf[3];

	if (plus > 127)
		return;
	if (count > 3) {
		p += count - 3;
		count = 3;
		*pluscount = 0;
	}
	if (from_user) {
		memcpy_fromfs(cbuf, p, count);
		p = cbuf;
	}
	while (count > 0) {
		if (*(p++) == plus) {
			if ((*pluscount)++) {
				/* Time since last '+' > 0.5 sec. ? */
				if ((jiffies - *lastplus) > PLUSWAIT1)
					*pluscount = 1;
			} else {
				/* Time since last non-'+' < 1.5 sec. ? */
				if ((jiffies - *lastplus) < PLUSWAIT2)
					*pluscount = 0;
			}
			if ((*pluscount == 3) && (count = 1))
				isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, 1);
			if (*pluscount > 3)
				*pluscount = 1;
		} else
			*pluscount = 0;
		*lastplus = jiffies;
		count--;
	}
}

/*
 * Return result of AT-emulator to tty-receive-buffer, depending on
 * modem-register 12, bit 0 and 1.
 * For CONNECT-messages also switch to online-mode.
 * For RING-message handle auto-ATA if register 0 != 0
 */
void isdn_tty_modem_result(int code, modem_info * info)
{
	atemu *m = &info->emu;
	static char *msg[] =
	{"OK", "CONNECT", "RING", "NO CARRIER", "ERROR",
	 "CONNECT 64000", "NO DIALTONE", "BUSY", "NO ANSWER",
	 "RINGING", "NO MSN/EAZ", "VCON"};
	ulong flags;
	char s[4];

	switch (code) {
                case 2:
                        m->mdmreg[1]++;	/* RING */
                        if (m->mdmreg[1] == m->mdmreg[0])
                                /* Automatically accept incoming call */
                                isdn_tty_cmd_ATA(info);
                        break;
                case 3:
                        /* NO CARRIER */
                        save_flags(flags);
                        cli();
#ifdef ISDN_DEBUG_MODEM_HUP
                        printk(KERN_DEBUG "modem_result: NO CARRIER %d %d\n",
                               (info->flags & ISDN_ASYNC_CLOSING),
                               (!info->tty));
#endif
                        if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
                                restore_flags(flags);
                                return;
                        }
                        restore_flags(flags);
                        if (info->vonline & 1) {
                                /* voice-recording, add DLE-ETX */
                                isdn_tty_at_cout("\020\003", info);
                        }
                        if (info->vonline & 2) {
                                /* voice-playing, add DLE-DC4 */
                                isdn_tty_at_cout("\020\024", info);
                        }
                        break;
                case 1:
                case 5:
                        if (!info->online)
                                info->online = 2;
                        break;
                case 11:
                        if (!info->online)
                                info->online = 1;
                        break;
	}
	if (m->mdmreg[12] & 1) {
		/* Show results */
		isdn_tty_at_cout("\r\n", info);
		if (m->mdmreg[12] & 2) {
			/* Show numeric results */
			sprintf(s, "%d", code);
			isdn_tty_at_cout(s, info);
		} else {
			if (code == 2) {
				isdn_tty_at_cout("CALLER NUMBER: ", info);
				isdn_tty_at_cout(dev->num[info->drv_index], info);
				isdn_tty_at_cout("\r\n", info);
			}
			isdn_tty_at_cout(msg[code], info);
			if (code == 5) {
				/* Append Protocol to CONNECT message */
				isdn_tty_at_cout((m->mdmreg[14] != 3) ? "/X.75" : "/HDLC", info);
				if (m->mdmreg[13] & 2)
					isdn_tty_at_cout("/T.70", info);
			}
		}
		isdn_tty_at_cout("\r\n", info);
	}
	if (code == 3) {
		save_flags(flags);
		cli();
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
			restore_flags(flags);
			return;
		}
                if (info->tty->ldisc.flush_buffer)
                        info->tty->ldisc.flush_buffer(info->tty);
		if ((info->flags & ISDN_ASYNC_CHECK_CD) &&
		    (!((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		       (info->flags & ISDN_ASYNC_CALLOUT_NOHUP)))) {
			tty_hangup(info->tty);
                }
		restore_flags(flags);
	}
}

/*
 * Display a modem-register-value.
 */
static void isdn_tty_show_profile(int ridx, modem_info * info)
{
	char v[6];

	sprintf(v, "\r\n%d", info->emu.mdmreg[ridx]);
	isdn_tty_at_cout(v, info);
}

/*
 * Get MSN-string from char-pointer, set pointer to end of number
 */
static void isdn_tty_get_msnstr(char *n, char **p)
{
	while ((*p[0] >= '0' && *p[0] <= '9') || (*p[0] == ','))
		*n++ = *p[0]++;
	*n = '\0';
}

/*
 * Get phone-number from modem-commandbuffer
 */
static void isdn_tty_getdial(char *p, char *q)
{
	int first = 1;

	while (strchr("0123456789,#.*WPTS-", *p) && *p) {
		if ((*p >= '0' && *p <= '9') || ((*p == 'S') && first))
			*q++ = *p;
		p++;
		first = 0;
	}
	*q = 0;
}

#define PARSE_ERROR { isdn_tty_modem_result(4, info); return; }
#define PARSE_ERROR1 { isdn_tty_modem_result(4, info); return 1; }

/*
 * Parse AT&.. commands.
 */
static int isdn_tty_cmd_ATand(char **p, modem_info * info)
{
        atemu *m = &info->emu;
        int i;
        char rb[100];

        switch (*p[0]) {
                case 'B':
                        /* &B - Set Buffersize */
                        p[0]++;
                        i = isdn_getnum(p);
                        if ((i < 0) || (i > ISDN_SERIAL_XMIT_SIZE))
                                PARSE_ERROR1;
#ifdef CONFIG_ISDN_AUDIO
                        if ((m->mdmreg[18] & 1) && (i > VBUF))
                                PARSE_ERROR1;
#endif
                        m->mdmreg[16] = i / 16;
                        info->xmit_size = m->mdmreg[16] * 16;
                        break;
                case 'D':
                        /* &D - Set DCD-Low-behavior */
                        p[0]++;
                        switch (isdn_getnum(p)) {
                                case 0:
                                        m->mdmreg[13] &= ~4;
                                        m->mdmreg[12] &= ~32;
                                        break;
                                case 2:
                                        m->mdmreg[13] |= 4;
                                        m->mdmreg[12] &= ~32;
                                        break;
                                case 3:
                                        m->mdmreg[13] |= 4;
                                        m->mdmreg[12] |= 32;
                                        break;
                                default:
                                        PARSE_ERROR1
                        }
                        break;
                case 'E':
                        /* &E -Set EAZ/MSN */
                        p[0]++;
                        isdn_tty_get_msnstr(m->msn, p);
                        break;
                case 'F':
                        /* &F -Set Factory-Defaults */
                        p[0]++;
                        isdn_tty_reset_profile(m);
                        isdn_tty_modem_reset_regs(m, 1);
                        break;
                case 'S':
                        /* &S - Set Windowsize */
                        p[0]++;
                        i = isdn_getnum(p);
                        if ((i > 0) && (i < 9))
                                m->mdmreg[17] = i;
                        else
                                PARSE_ERROR1;
                        break;
                case 'V':
                        /* &V - Show registers */
                        p[0]++;
                        for (i = 0; i < ISDN_MODEM_ANZREG; i++) {
                                sprintf(rb, "S%d=%d%s", i, 
                                        m->mdmreg[i], (i == 6) ? "\r\n" : " ");
                                isdn_tty_at_cout(rb, info);
                        }
                        sprintf(rb, "\r\nEAZ/MSN: %s\r\n",
                                strlen(m->msn) ? m->msn : "None");
                        isdn_tty_at_cout(rb, info);
                        break;
                case 'W':
                        /* &W - Write Profile */
                        p[0]++;
                        switch (*p[0]) {
                                case '0':
                                        p[0]++;
                                        modem_write_profile(m);
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                case 'X':
                        /* &X - Switch to BTX-Mode */
                        p[0]++;
                        switch (isdn_getnum(p)) {
                                case 0:
                                        m->mdmreg[13] &= ~2;
                                        info->xmit_size = m->mdmreg[16] * 16;
                                        break;
                                case 1:
                                        m->mdmreg[13] |= 2;
                                        m->mdmreg[14] = 0;
                                        info->xmit_size = 112;
                                        m->mdmreg[18] = 4;
                                        m->mdmreg[19] = 0;
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                default:
                        PARSE_ERROR1;
        }
        return 0;
}

/*
 * Perform ATS command
 */
static int isdn_tty_cmd_ATS(char **p, modem_info * info)
{
        atemu *m = &info->emu;
        int mreg;
        int mval;

        mreg = isdn_getnum(p);
        if (mreg < 0 || mreg > ISDN_MODEM_ANZREG)
                PARSE_ERROR1;
        switch (*p[0]) {
                case '=':
                        p[0]++;
                        mval = isdn_getnum(p);
                        if (mval < 0 || mval > 255)
                                PARSE_ERROR1;
                        switch (mreg) {
                                /* Some plausibility checks */
                                case 14:
                                        if (mval > ISDN_PROTO_L2_TRANS)
                                                PARSE_ERROR1;
                                        break;
                                case 16:
                                        if ((mval * 16) > ISDN_SERIAL_XMIT_SIZE)
                                                PARSE_ERROR1;
#ifdef CONFIG_ISDN_AUDIO
                                        if ((m->mdmreg[18] & 1) && (mval > VBUFX))
                                                PARSE_ERROR1;
#endif
                                        info->xmit_size = mval * 16;
                                        break;
                                case 20:
                                        PARSE_ERROR1;
                        }
                        m->mdmreg[mreg] = mval;
                        break;
                case '?':
                        p[0]++;
                        isdn_tty_show_profile(mreg, info);
                        break;
                default:
                        PARSE_ERROR1;
                        break;
        }
        return 0;
}

/*
 * Perform ATA command
 */
static void isdn_tty_cmd_ATA(modem_info * info)
{
        atemu *m = &info->emu;
        isdn_ctrl cmd;
        int l2;

        if (info->msr & UART_MSR_RI) {
                /* Accept incoming call */
                m->mdmreg[1] = 0;
                info->msr &= ~UART_MSR_RI;
                l2 = m->mdmreg[14];
#ifdef CONFIG_ISDN_AUDIO
                /* If more than one bit set in reg18, autoselect Layer2 */
                if ((m->mdmreg[18] & m->mdmreg[20]) != m->mdmreg[18])
                        if (m->mdmreg[20] == 1) l2 = 4;
#endif
                cmd.driver = info->isdn_driver;
                cmd.command = ISDN_CMD_SETL2;
                cmd.arg = info->isdn_channel + (l2 << 8);
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                cmd.driver = info->isdn_driver;
                cmd.command = ISDN_CMD_SETL3;
                cmd.arg = info->isdn_channel + (m->mdmreg[15] << 8);
                dev->drv[info->isdn_driver]->interface->command(&cmd);
                cmd.driver = info->isdn_driver;
                cmd.arg = info->isdn_channel;
                cmd.command = ISDN_CMD_ACCEPTD;
                dev->drv[info->isdn_driver]->interface->command(&cmd);
        } else
                isdn_tty_modem_result(8, info);
}

#ifdef CONFIG_ISDN_AUDIO
/*
 * Parse AT+F.. commands
 */
static int isdn_tty_cmd_PLUSF(char **p, modem_info * info)
{
        atemu *m = &info->emu;
        int par;
	char rs[20];

        if (!strncmp(p[0],"CLASS",5)) {
                p[0] += 5;
                switch (*p[0]) {
                        case '?':
                                p[0]++;
                                sprintf(rs,"\r\n%d",
                                        (m->mdmreg[18]&1)?8:0);
                                isdn_tty_at_cout(rs, info);
                                break;
                        case '=':
                                p[0]++;
                                switch (*p[0]) {
                                        case '0':
                                                p[0]++;
                                                m->mdmreg[18] = 4;
                                                info->xmit_size =
                                                        m->mdmreg[16] * 16;
                                                break;
                                        case '8':
                                                p[0]++;
                                                m->mdmreg[18] = 5;
                                                info->xmit_size = VBUF;
                                                break;
                                        case '?':
                                                p[0]++;
                                                isdn_tty_at_cout("\r\n0,8",
                                                                 info);
                                                break;
                                        default:
                                                PARSE_ERROR1;
                                }
                                break;
                        default:
                                PARSE_ERROR1;
                }
                return 0;
        }        
        if (!strncmp(p[0],"AA",2)) {
                p[0] += 2;
                switch (*p[0]) {
                        case '?':
                                p[0]++;
                                sprintf(rs,"\r\n%d",
                                        m->mdmreg[0]);
                                isdn_tty_at_cout(rs, info);
                                break;
                        case '=':
                                p[0]++;
                                par = isdn_getnum(p);
                                if ((par < 0) || (par > 255))
                                        PARSE_ERROR1;
                                m->mdmreg[0]=par;
                                break;
                        default:
                                PARSE_ERROR1;                                
                }
                return 0;
        }
        PARSE_ERROR1;
}

/*
 * Parse AT+V.. commands
 */
static int isdn_tty_cmd_PLUSV(char **p, modem_info * info)
{
        atemu *m = &info->emu;
        static char *vcmd[] = {"NH","IP","LS","RX","SD","SM","TX",NULL};
        int i;
	int par1;
	int par2;
	char rs[20];

        i = 0;
        while (vcmd[i]) {
                if (!strncmp(vcmd[i],p[0],2)) {
                        p[0] += 2;
                        break;
                }
                i++;
        }
        switch (i) {
                case 0:
                        /* AT+VNH - Auto hangup feature */
                        switch (*p[0]) {
                                case '?':
                                        p[0]++;
                                        isdn_tty_at_cout("\r\n1", info);
                                        break;
                                case '=':
                                        p[0]++;
                                        switch (*p[0]) {
                                                case '1':
                                                        p[0]++;
                                                        break;
                                                case '?':
                                                        p[0]++;
                                                        isdn_tty_at_cout("\r\n1", info);
                                                        break;
                                                default:
                                                        PARSE_ERROR1;
                                        }
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                case 1:
                        /* AT+VIP - Reset all voice parameters */
                        isdn_tty_modem_reset_vpar(m);
                        break;
                case 2:
                        /* AT+VLS - Select device, accept incoming call */
                        switch (*p[0]) {
                                case '?':
                                        p[0]++;
                                        sprintf(rs,"\r\n%d",m->vpar[0]);
                                        isdn_tty_at_cout(rs, info);
                                        break;
                                case '=':
                                        p[0]++;
                                        switch (*p[0]) {
                                                case '0':
                                                        p[0]++;
                                                        m->vpar[0] = 0;
                                                        break;
                                                case '2':
                                                        p[0]++;
                                                        m->vpar[0] = 2;
                                                        break;
                                                case '?':
                                                        p[0]++;
                                                        isdn_tty_at_cout("\r\n0,2", info);
                                                        break;
                                                default:
                                                        PARSE_ERROR1;
                                        }
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                case 3:
                        /* AT+VRX - Start recording */
                        if (!m->vpar[0])
                                PARSE_ERROR1;
                        if (info->online != 1) {
                                isdn_tty_modem_result(8, info);
                                return 1;
                        }
                        info->dtmf_state = isdn_audio_dtmf_init(info->dtmf_state);
                        if (!info->dtmf_state) {
                                printk(KERN_WARNING "isdn_tty: Couldn't malloc dtmf state\n");
                                PARSE_ERROR1;
                        }
                        if (m->vpar[3] < 5) {
                                info->adpcmr = isdn_audio_adpcm_init(info->adpcmr, m->vpar[3]);
                                if (!info->adpcmr) {
                                        printk(KERN_WARNING "isdn_tty: Couldn't malloc adpcm state\n");
                                        PARSE_ERROR1;
                                }
                        }
                        info->vonline = 1;
                        isdn_tty_modem_result(1, info);
                        return 1;
                        break;
                case 4:
                        /* AT+VSD - Silence detection */
                        switch (*p[0]) {
                                case '?':
                                        p[0]++;
                                        sprintf(rs,"\r\n<%d>,<%d>",
                                                m->vpar[1],
                                                m->vpar[2]);
                                        isdn_tty_at_cout(rs, info);
                                        break;
                                case '=':
                                        p[0]++;
                                        switch (*p[0]) {
                                                case '0':
                                                case '1':
                                                case '2':
                                                case '3':
                                                        par1 = isdn_getnum(p);
                                                        if ((par1 < 0) || (par1 > 31))
                                                                PARSE_ERROR1;
                                                        if (*p[0] != ',')
                                                                PARSE_ERROR1;
                                                        p[0]++;
                                                        par2 = isdn_getnum(p);
                                                        if ((par2 < 0) || (par2 > 255))
                                                                PARSE_ERROR1;
                                                        m->vpar[1] = par1;
                                                        m->vpar[2] = par2;
                                                        break;
                                                case '?':
                                                        p[0]++;
                                                        isdn_tty_at_cout("\r\n<0-31>,<0-255>",
                                                                         info);
                                                        break;
                                                default:
                                                        PARSE_ERROR1;
                                        }
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                case 5:
                        /* AT+VSM - Select compression */
                        switch (*p[0]) {
                                case '?':
                                        p[0]++;
                                        sprintf(rs,"\r\n<%d>,<%d><8000>",
                                                m->vpar[3],
                                                m->vpar[1]);
                                        isdn_tty_at_cout(rs, info);
                                        break;
                                case '=':
                                        p[0]++;
                                        switch (*p[0]) {
                                                case '2':
                                                case '3':
                                                case '4':
                                                case '5':
                                                case '6':
                                                        par1 = isdn_getnum(p);
                                                        if ((par1 < 2) || (par1 > 6))
                                                                PARSE_ERROR1;
                                                        m->vpar[3] = par1;
                                                        break;
                                                case '?':
                                                        p[0]++;
                                                        isdn_tty_at_cout("\r\n2;ADPCM;2;0;(8000)\r\n",
                                                                         info);
                                                        isdn_tty_at_cout("3;ADPCM;3;0;(8000)\r\n",
                                                                         info);
                                                        isdn_tty_at_cout("4;ADPCM;4;0;(8000)\r\n",
                                                                         info);
                                                        isdn_tty_at_cout("5;ALAW;8;0;(8000)",
                                                                         info);
                                                        isdn_tty_at_cout("6;ULAW;8;0;(8000)",
                                                                         info);
                                                        break;
                                                default:
                                                        PARSE_ERROR1;
                                        }
                                        break;
                                default:
                                        PARSE_ERROR1;
                        }
                        break;
                case 6:
                        /* AT+VTX - Start sending */
                        if (!m->vpar[0])
                                PARSE_ERROR1;
                        if (info->online != 1) {
                                isdn_tty_modem_result(8, info);
                                return 1;
                        }
                        info->dtmf_state = isdn_audio_dtmf_init(info->dtmf_state);
                        if (!info->dtmf_state) {
                                printk(KERN_WARNING "isdn_tty: Couldn't malloc dtmf state\n");
                                PARSE_ERROR1;
                        }
                        if (m->vpar[3] < 5) {
                                info->adpcms = isdn_audio_adpcm_init(info->adpcms, m->vpar[3]);
                                if (!info->adpcms) {
                                        printk(KERN_WARNING "isdn_tty: Couldn't malloc adpcm state\n");
                                        PARSE_ERROR1;
                                }
                        }
                        m->lastDLE = 0;
                        info->vonline = 2;
                        isdn_tty_modem_result(1, info);
                        return 1;
                        break;
                default:
                        PARSE_ERROR1;
        }
        return 0;
}
#endif        /* CONFIG_ISDN_AUDIO */

/*
 * Parse and perform an AT-command-line.
 */
static void isdn_tty_parse_at(modem_info * info)
{
        atemu *m = &info->emu;
        char *p;
        char ds[40];

#ifdef ISDN_DEBUG_AT
        printk(KERN_DEBUG "AT: '%s'\n", m->mdmcmd);
#endif
        for (p = &m->mdmcmd[2]; *p;) {
                switch (*p) {
                        case 'A':
                                /* A - Accept incoming call */
                                p++;
                                isdn_tty_cmd_ATA(info);
                                return;
                                break;
                        case 'D':
                                /* D - Dial */
                                isdn_tty_getdial(++p, ds);
                                p += strlen(p);
                                if (!strlen(m->msn))
                                        isdn_tty_modem_result(10, info);
                                else if (strlen(ds))
                                        isdn_tty_dial(ds, info, m);
                                else
                                        isdn_tty_modem_result(4, info);
                                return;
                        case 'E':
                                /* E - Turn Echo on/off */
                                p++;
                                switch (isdn_getnum(&p)) {
                                        case 0:
                                                m->mdmreg[12] &= ~4;
                                                break;
                                        case 1:
                                                m->mdmreg[12] |= 4;
                                                break;
                                        default:
                                                PARSE_ERROR;
                                }
                                break;
                        case 'H':
                                /* H - On/Off-hook */
                                p++;
                                switch (*p) {
                                        case '0':
                                                p++;
                                                isdn_tty_on_hook(info);
                                                break;
                                        case '1':
                                                p++;
                                                isdn_tty_off_hook();
                                                break;
                                        default:
                                                isdn_tty_on_hook(info);
                                                break;
                                }
                                break;
                        case 'I':
                                /* I - Information */
                                p++;
                                isdn_tty_at_cout("\r\nLinux ISDN", info);
                                switch (*p) {
                                        case '0':
                                        case '1':
                                                p++;
                                                break;
                                        default:
                                }
                                break;
                        case 'O':
                                /* O - Go online */
                                p++;
                                if (info->msr & UART_MSR_DCD)
                                        /* if B-Channel is up */
                                        isdn_tty_modem_result(5, info);
                                else
                                        isdn_tty_modem_result(3, info);
                                return;
                        case 'Q':
                                /* Q - Turn Emulator messages on/off */
                                p++;
                                switch (isdn_getnum(&p)) {
                                        case 0:
                                                m->mdmreg[12] |= 1;
                                                break;
                                        case 1:
                                                m->mdmreg[12] &= ~1;
                                                break;
                                        default:
                                                PARSE_ERROR;
                                }
                                break;
                        case 'S':
                                /* S - Set/Get Register */
                                p++;
                                if (isdn_tty_cmd_ATS(&p, info))
                                        return;
                                break;
                        case 'V':
                                /* V - Numeric or ASCII Emulator-messages */
                                p++;
                                switch (isdn_getnum(&p)) {
                                        case 0:
                                                m->mdmreg[12] |= 2;
                                                break;
                                        case 1:
                                                m->mdmreg[12] &= ~2;
                                                break;
                                        default:
                                                PARSE_ERROR;
                                }
                                break;
                        case 'Z':
                                /* Z - Load Registers from Profile */
                                p++;
                                isdn_tty_modem_reset_regs(m, 1);
                                break;
#ifdef CONFIG_ISDN_AUDIO
                        case '+':
                                p++;
                                switch (*p) {
                                        case 'F':
                                                p++;
                                                if (isdn_tty_cmd_PLUSF(&p, info))
                                                        return;
                                                break;
                                        case 'V':
                                                if (!(m->mdmreg[18] & 1))
                                                        PARSE_ERROR;
                                                p++;
                                                if (isdn_tty_cmd_PLUSV(&p, info))
                                                        return;
                                                break;
                                }
                                break;
#endif        /* CONFIG_ISDN_AUDIO */
                        case '&':
                                p++;
                                if (isdn_tty_cmd_ATand(&p, info))
                                        return;
                                break;
                        default:
                                isdn_tty_modem_result(4, info);
                                return;
                }
        }
        isdn_tty_modem_result(0, info);
}

/* Need own toupper() because standard-toupper is not available
 * within modules.
 */
#define my_toupper(c) (((c>='a')&&(c<='z'))?(c&0xdf):c)

/*
 * Perform line-editing of AT-commands
 *
 * Parameters:
 *   p        inputbuffer
 *   count    length of buffer
 *   channel  index to line (minor-device)
 *   user     flag: buffer is in userspace
 */
static int isdn_tty_edit_at(const char *p, int count, modem_info * info, int user)
{
	atemu *m = &info->emu;
	int total = 0;
	u_char c;
	char eb[2];
	int cnt;

	for (cnt = count; cnt > 0; p++, cnt--) {
		if (user)
			c = get_user(p);
		else
			c = *p;
		total++;
		if (c == m->mdmreg[3] || c == m->mdmreg[4]) {
			/* Separator (CR oder LF) */
			m->mdmcmd[m->mdmcmdl] = 0;
			if (m->mdmreg[12] & 4) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if (m->mdmcmdl >= 2)
				isdn_tty_parse_at(info);
			m->mdmcmdl = 0;
			continue;
		}
		if (c == m->mdmreg[5] && m->mdmreg[5] < 128) {
			/* Backspace-Funktion */
			if ((m->mdmcmdl > 2) || (!m->mdmcmdl)) {
				if (m->mdmcmdl)
					m->mdmcmdl--;
				if (m->mdmreg[12] & 4)
					isdn_tty_at_cout("\b", info);
			}
			continue;
		}
		if (cmdchar(c)) {
			if (m->mdmreg[12] & 4) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if (m->mdmcmdl < 255) {
				c = my_toupper(c);
				switch (m->mdmcmdl) {
                                        case 0:
                                                if (c == 'A')
                                                        m->mdmcmd[m->mdmcmdl++] = c;
                                                break;
                                        case 1:
                                                if (c == 'T')
                                                        m->mdmcmd[m->mdmcmdl++] = c;
                                                break;
                                        default:
                                                m->mdmcmd[m->mdmcmdl++] = c;
				}
			}
		}
	}
	return total;
}

/*
 * Switch all modem-channels who are online and got a valid
 * escape-sequence 1.5 seconds ago, to command-mode.
 * This function is called every second via timer-interrupt from within 
 * timer-dispatcher isdn_timer_function()
 */
void isdn_tty_modem_escape(void)
{
	int ton = 0;
	int i;
	int midx;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_MODEM(dev->usage[i]))
			if ((midx = dev->m_idx[i]) >= 0) {
                                modem_info *info = &dev->mdm.info[midx];
				if (info->online) {
					ton = 1;
					if ((info->emu.pluscount == 3) &&
					    ((jiffies - info->emu.lastplus) > PLUSWAIT2)) {
						info->emu.pluscount = 0;
						info->online = 0;
						isdn_tty_modem_result(0, info);
					}
				}
                        }
	isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, ton);
}

/*
 * Put a RING-message to all modem-channels who have the RI-bit set.
 * This function is called every second via timer-interrupt from within 
 * timer-dispatcher isdn_timer_function()
 */
void isdn_tty_modem_ring(void)
{
	int ton = 0;
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                modem_info *info = &dev->mdm.info[i];
                if (info->msr & UART_MSR_RI) {
                        ton = 1;
                        isdn_tty_modem_result(2, info);
                }
        }
	isdn_timer_ctrl(ISDN_TIMER_MODEMRING, ton);
}

/*
 * For all online tty's, try sending data to
 * the lower levels.
 */
void isdn_tty_modem_xmit(void)
{
	int ton = 1;
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                modem_info *info = &dev->mdm.info[i];
                if (info->online) {
                        ton = 1;
                        isdn_tty_senddown(info);
                        isdn_tty_tint(info);
                }
        }
	isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, ton);
}

/*
 * A packet has been output successfully.
 * Search the tty-devices for an appropriate device, decrement its
 * counter for outstanding packets, and set CTS.
 */
void isdn_tty_bsent(int drv, int chan)
{
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                modem_info *info = &dev->mdm.info[i];
                if ((info->isdn_driver == drv) &&
                    (info->isdn_channel == chan) ) {
                        info->msr |= UART_MSR_CTS;
                        if (info->send_outstanding)
                                if (!(--info->send_outstanding))
                                        info->lsr |= UART_LSR_TEMT;
                        isdn_tty_tint(info);
                }
        }
	return;
}
