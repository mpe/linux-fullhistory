/*****************************************************************************/

/*
 *	baycom_epp.c  -- baycom epp radio modem driver.
 *
 *	Copyright (C) 1998
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 *
 *  History:
 *   0.1  xx.xx.98  Initial version by Matthias Welwarsky (dg2fef)
 *   0.2  21.04.98  Massive rework by Thomas Sailer
 *                  Integrated FPGA EPP modem configuration routines
 *   0.3  11.05.98  Took FPGA config out and moved it into a separate program
 *
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/parport.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
//#include <net/ax25dev.h>
#include <linux/kmod.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/soundmodem.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
/* prototypes for ax25_encapsulate and ax25_rebuild_header */
#include <net/ax25.h> 
#endif /* CONFIG_AX25 || CONFIG_AX25_MODULE */

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

/* --------------------------------------------------------------------- */

/*
 * currently this module is supposed to support both module styles, i.e.
 * the old one present up to about 2.1.9, and the new one functioning
 * starting with 2.1.21. The reason is I have a kit allowing to compile
 * this module also under 2.0.x which was requested by several people.
 * This will go in 2.2
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= 0x20100
#include <asm/uaccess.h>
#else
#include <asm/segment.h>
#include <linux/mm.h>

#undef put_user
#undef get_user

#define put_user(x,ptr) ({ __put_user((unsigned long)(x),(ptr),sizeof(*(ptr))); 0; })
#define get_user(x,ptr) ({ x = ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr)))); 0; })

extern inline int copy_from_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_READ, from, n);
        if (i)
                return i;
        memcpy_fromfs(to, from, n);
        return 0;
}

extern inline int copy_to_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_WRITE, to, n);
        if (i)
                return i;
        memcpy_tofs(to, from, n);
        return 0;
}
#endif

#if LINUX_VERSION_CODE >= 0x20123
#include <linux/init.h>
#else
#define __init
#define __initdata
#define __initfunc(x) x
#endif

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG
#define BAYCOM_MAGIC 19730510

/* --------------------------------------------------------------------- */

static const char paranoia_str[] = KERN_ERR 
"baycom_epp: bad magic number for hdlcdrv_state struct in routine %s\n";

#define baycom_paranoia_check(dev,routine,retval)                                              \
({                                                                                             \
	if (!dev || !dev->priv || ((struct baycom_state *)dev->priv)->magic != BAYCOM_MAGIC) { \
		printk(paranoia_str, routine);                                                 \
		return retval;                                                                 \
	}                                                                                      \
})

#define baycom_paranoia_check_void(dev,routine)                                                \
({                                                                                             \
	if (!dev || !dev->priv || ((struct baycom_state *)dev->priv)->magic != BAYCOM_MAGIC) { \
		printk(paranoia_str, routine);                                                 \
		return;                                                                        \
	}                                                                                      \
})

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_epp";
static const char bc_drvinfo[] = KERN_INFO "baycom_epp: (C) 1998 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "baycom_epp: version 0.3 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

static struct device baycom_device[NR_PORTS];

static struct {
	const char *mode;
	int iobase;
} baycom_ports[NR_PORTS] = { { NULL, 0 }, };

/* --------------------------------------------------------------------- */

/* EPP status register */
#define EPP_DCDBIT      0x80
#define EPP_PTTBIT      0x08
#define EPP_NREF        0x01
#define EPP_NRAEF       0x02
#define EPP_NRHF        0x04
#define EPP_NTHF        0x20
#define EPP_NTAEF       0x10
#define EPP_NTEF        EPP_PTTBIT

/* EPP control register */
#define EPP_TX_FIFO_ENABLE 0x10
#define EPP_RX_FIFO_ENABLE 0x08
#define EPP_MODEM_ENABLE   0x20
#define EPP_LEDS           0xC0
#define EPP_IRQ_ENABLE     0x10

/* LPT registers */
#define LPTREG_ECONTROL       0x402
#define LPTREG_CONFIGB        0x401
#define LPTREG_CONFIGA        0x400
#define LPTREG_EPPDATA        0x004
#define LPTREG_EPPADDR        0x003
#define LPTREG_CONTROL        0x002
#define LPTREG_STATUS         0x001
#define LPTREG_DATA           0x000

/* LPT control register */
#define LPTCTRL_PROGRAM       0x04   /* 0 to reprogram */
#define LPTCTRL_WRITE         0x01
#define LPTCTRL_ADDRSTB       0x08
#define LPTCTRL_DATASTB       0x02
#define LPTCTRL_INTEN         0x10

/* LPT status register */
#define LPTSTAT_SHIFT_NINTR   6
#define LPTSTAT_WAIT          0x80
#define LPTSTAT_NINTR         (1<<LPTSTAT_SHIFT_NINTR)
#define LPTSTAT_PE            0x20
#define LPTSTAT_DONE          0x10
#define LPTSTAT_NERROR        0x08
#define LPTSTAT_EPPTIMEOUT    0x01

/* LPT data register */
#define LPTDATA_SHIFT_TDI     0
#define LPTDATA_SHIFT_TMS     2
#define LPTDATA_TDI           (1<<LPTDATA_SHIFT_TDI)
#define LPTDATA_TCK           0x02
#define LPTDATA_TMS           (1<<LPTDATA_SHIFT_TMS)
#define LPTDATA_INITBIAS      0x80


/* EPP modem config/status bits */
#define EPP_DCDBIT            0x80
#define EPP_PTTBIT            0x08
#define EPP_RXEBIT            0x01
#define EPP_RXAEBIT           0x02
#define EPP_RXHFULL           0x04

#define EPP_NTHF              0x20
#define EPP_NTAEF             0x10
#define EPP_NTEF              EPP_PTTBIT

#define EPP_TX_FIFO_ENABLE    0x10
#define EPP_RX_FIFO_ENABLE    0x08
#define EPP_MODEM_ENABLE      0x20
#define EPP_LEDS              0xC0
#define EPP_IRQ_ENABLE        0x10

/* Xilinx 4k JTAG instructions */
#define XC4K_IRLENGTH   3
#define XC4K_EXTEST     0
#define XC4K_PRELOAD    1
#define XC4K_CONFIGURE  5
#define XC4K_BYPASS     7

#define EPP_CONVENTIONAL  0
#define EPP_FPGA          1
#define EPP_FPGAEXTSTATUS 2

#define TXBUFFER_SIZE     ((HDLCDRV_MAXFLEN*6/5)+8)

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board.
 */

struct baycom_state {
	int magic;

        struct pardevice *pdev;
	unsigned int bh_running;
	struct tq_struct run_bh;
	unsigned int modem;
	unsigned int bitrate;
	unsigned char stat;

	char ifname[HDLCDRV_IFNAMELEN];

	struct {
		unsigned int intclk;
		unsigned int divider;
		unsigned int extmodem;
		unsigned int loopback;
	} cfg;

        struct hdlcdrv_channel_params ch_params;

        struct {
		unsigned int bitbuf, bitstream, numbits, state;
		unsigned char *bufptr;
		int bufcnt;
		unsigned char buf[TXBUFFER_SIZE];
        } hdlcrx;

        struct {
		int calibrate;
                int slotcnt;
		int flags;
		enum { tx_idle = 0, tx_keyup, tx_data, tx_tail } state;
		unsigned char *bufptr;
		int bufcnt;
		unsigned char buf[TXBUFFER_SIZE];
        } hdlctx;

        struct net_device_stats stats;
	unsigned int ptt_keyed;
        struct sk_buff_head send_queue;  /* Packets awaiting transmission */


#ifdef BAYCOM_DEBUG
	struct debug_vals {
		unsigned long last_jiffies;
		unsigned cur_intcnt;
		unsigned last_intcnt;
		int cur_pllcorr;
		int last_pllcorr;
		unsigned int mod_cycles;
		unsigned int demod_cycles;
	} debug_vals;
#endif /* BAYCOM_DEBUG */
};

/* --------------------------------------------------------------------- */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* --------------------------------------------------------------------- */

#define KISS_VERBOSE

/* --------------------------------------------------------------------- */

#define PARAM_TXDELAY   1
#define PARAM_PERSIST   2
#define PARAM_SLOTTIME  3
#define PARAM_TXTAIL    4
#define PARAM_FULLDUP   5
#define PARAM_HARDWARE  6
#define PARAM_RETURN    255

/* --------------------------------------------------------------------- */
/*
 * the CRC routines are stolen from WAMPES
 * by Dieter Deyke
 */

static const unsigned short crc_ccitt_table[] = {
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

/*---------------------------------------------------------------------------*/

#if 0
extern inline void append_crc_ccitt(unsigned char *buffer, int len)
{
 	unsigned int crc = 0xffff;

	for (;len>0;len--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buffer++) & 0xff];
	crc ^= 0xffff;
	*buffer++ = crc;
	*buffer++ = crc >> 8;
}
#endif

/*---------------------------------------------------------------------------*/

extern inline int check_crc_ccitt(const unsigned char *buf, int cnt)
{
	unsigned int crc = 0xffff;

	for (; cnt > 0; cnt--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buf++) & 0xff];
	return (crc & 0xffff) == 0xf0b8;
}

/*---------------------------------------------------------------------------*/

extern inline int calc_crc_ccitt(const unsigned char *buf, int cnt)
{
	unsigned int crc = 0xffff;

	for (; cnt > 0; cnt--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buf++) & 0xff];
	crc ^= 0xffff;
	return (crc & 0xffff);
}

/* ---------------------------------------------------------------------- */

#define tenms_to_flags(bc,tenms) ((tenms * bc->bitrate) / 800)

/* --------------------------------------------------------------------- */

static void inline baycom_int_freq(struct baycom_state *bc)
{
#ifdef BAYCOM_DEBUG
	unsigned long cur_jiffies = jiffies;
	/*
	 * measure the interrupt frequency
	 */
	bc->debug_vals.cur_intcnt++;
	if ((cur_jiffies - bc->debug_vals.last_jiffies) >= HZ) {
		bc->debug_vals.last_jiffies = cur_jiffies;
		bc->debug_vals.last_intcnt = bc->debug_vals.cur_intcnt;
		bc->debug_vals.cur_intcnt = 0;
		bc->debug_vals.last_pllcorr = bc->debug_vals.cur_pllcorr;
		bc->debug_vals.cur_pllcorr = 0;
	}
#endif /* BAYCOM_DEBUG */
}

/* ---------------------------------------------------------------------- */
/*
 *    eppconfig_path should be setable  via /proc/sys.
 */

char eppconfig_path[256] = "/sbin/eppfpga";

static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/usr/bin:/bin", NULL };

static int errno;

static int exec_eppfpga(void *b)
{
	struct baycom_state *bc = (struct baycom_state *)b;
	char modearg[256];
	char portarg[16];
        char *argv[] = { eppconfig_path, "-s", "-p", portarg, "-m", modearg, NULL};
        int i;

	/* set up arguments */
	sprintf(modearg, "%sclk,%smodem,divider=%d%s,extstat",
		bc->cfg.intclk ? "int" : "ext",
		bc->cfg.extmodem ? "ext" : "int", bc->cfg.divider,
		bc->cfg.loopback ? ",loopback" : "");
	sprintf(portarg, "%ld", bc->pdev->port->base);
	printk(KERN_DEBUG "%s: %s -s -p %s -m %s\n", bc_drvname, eppconfig_path, portarg, modearg);

        for (i = 0; i < current->files->max_fds; i++ )
		if (current->files->fd[i]) 
			close(i);
        set_fs(KERNEL_DS);      /* Allow execve args to be in kernel space. */
        current->uid = current->euid = current->fsuid = 0;
        if (execve(eppconfig_path, argv, envp) < 0) {
                printk(KERN_ERR "%s: failed to exec %s -s -p %s -m %s, errno = %d\n",
                       bc_drvname, eppconfig_path, portarg, modearg, errno);
                return -errno;
        }
        return 0;
}


/* eppconfig: called during ifconfig up to configure the modem */

static int eppconfig(struct baycom_state *bc)
{
        int i, pid, r;
	mm_segment_t fs;

        pid = kernel_thread(exec_eppfpga, bc, CLONE_FS);
        if (pid < 0) {
                printk(KERN_ERR "%s: fork failed, errno %d\n", bc_drvname, -pid);
                return pid;
        }
	fs = get_fs();
        set_fs(KERNEL_DS);      /* Allow i to be in kernel space. */
	r = waitpid(pid, &i, __WCLONE);
	set_fs(fs);
        if (r != pid) {
                printk(KERN_ERR "%s: waitpid(%d) failed, returning %d\n",
		       bc_drvname, pid, r);
		return -1;
        }
	printk(KERN_DEBUG "%s: eppfpga returned %d\n", bc_drvname, i);
	return i;
}

/* ---------------------------------------------------------------------- */

static void epp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
}

/* ---------------------------------------------------------------------- */

static void inline do_kiss_params(struct baycom_state *bc,
				  unsigned char *data, unsigned long len)
{

#ifdef KISS_VERBOSE
#define PKP(a,b) printk(KERN_INFO "%s: channel params: " a "\n", bc->ifname, b)
#else /* KISS_VERBOSE */	      
#define PKP(a,b) 
#endif /* KISS_VERBOSE */	      

	if (len < 2)
		return;
	switch(data[0]) {
	case PARAM_TXDELAY:
		bc->ch_params.tx_delay = data[1];
		PKP("TX delay = %ums", 10 * bc->ch_params.tx_delay);
		break;
	case PARAM_PERSIST:   
		bc->ch_params.ppersist = data[1];
		PKP("p persistence = %u", bc->ch_params.ppersist);
		break;
	case PARAM_SLOTTIME:  
		bc->ch_params.slottime = data[1];
		PKP("slot time = %ums", bc->ch_params.slottime);
		break;
	case PARAM_TXTAIL:    
		bc->ch_params.tx_tail = data[1];
		PKP("TX tail = %ums", bc->ch_params.tx_tail);
		break;
	case PARAM_FULLDUP:   
		bc->ch_params.fulldup = !!data[1];
		PKP("%s duplex", bc->ch_params.fulldup ? "full" : "half");
		break;
	default:
		break;
	}
#undef PKP
}

/* --------------------------------------------------------------------- */
/*
 * high performance HDLC encoder
 * yes, it's ugly, but generates pretty good code
 */

#define ENCODEITERA(j)                         \
({                                             \
        if (!(notbitstream & (0x1f0 << j)))    \
                goto stuff##j;                 \
  encodeend##j:                                \
})

#define ENCODEITERB(j)                                          \
({                                                              \
  stuff##j:                                                     \
        bitstream &= ~(0x100 << j);                             \
        bitbuf = (bitbuf & (((2 << j) << numbit) - 1)) |        \
                ((bitbuf & ~(((2 << j) << numbit) - 1)) << 1);  \
        numbit++;                                               \
        notbitstream = ~bitstream;                              \
        goto encodeend##j;                                      \
})


static void encode_hdlc(struct baycom_state *bc)
{
	struct sk_buff *skb;
	unsigned char *wp, *bp;
	int pkt_len;
        unsigned bitstream, notbitstream, bitbuf, numbit, crc;
	unsigned char crcarr[2];
	
	if (bc->hdlctx.bufcnt > 0)
		return;
	while ((skb = skb_dequeue(&bc->send_queue))) {
		if (skb->data[0] != 0) {
			do_kiss_params(bc, skb->data, skb->len);
			dev_kfree_skb(skb);
			continue;
		}
		pkt_len = skb->len-1; /* strip KISS byte */
		if (pkt_len >= HDLCDRV_MAXFLEN || pkt_len < 2) {
			dev_kfree_skb(skb);
			continue;
		}
		wp = bc->hdlctx.buf;
		bp = skb->data+1;
		crc = calc_crc_ccitt(bp, pkt_len);
		crcarr[0] = crc;
		crcarr[1] = crc >> 8;
		*wp++ = 0x7e;
                bitstream = bitbuf = numbit = 0;
		while (pkt_len > -2) {
                        bitstream >>= 8;
                        bitstream |= ((unsigned int)*bp) << 8;
                        bitbuf |= ((unsigned int)*bp) << numbit;
                        notbitstream = ~bitstream;
			bp++;
			pkt_len--;
			if (!pkt_len)
				bp = crcarr;
                        ENCODEITERA(0);
                        ENCODEITERA(1);
                        ENCODEITERA(2);
                        ENCODEITERA(3);
                        ENCODEITERA(4);
                        ENCODEITERA(5);
                        ENCODEITERA(6);
                        ENCODEITERA(7);
                        goto enditer;
                        ENCODEITERB(0);
                        ENCODEITERB(1);
                        ENCODEITERB(2);
                        ENCODEITERB(3);
                        ENCODEITERB(4);
                        ENCODEITERB(5);
                        ENCODEITERB(6);
                        ENCODEITERB(7);
                  enditer:
                        numbit += 8;
                        while (numbit >= 8) {
                                *wp++ = bitbuf;
                                bitbuf >>= 8;
                                numbit -= 8;
                        }
                }
		bitbuf |= 0x7e7e << numbit;
                numbit += 16;
                while (numbit >= 8) {
                        *wp++ = bitbuf;
                        bitbuf >>= 8;
                        numbit -= 8;
                }
		bc->hdlctx.bufptr = bc->hdlctx.buf;
		bc->hdlctx.bufcnt = wp - bc->hdlctx.buf;
		dev_kfree_skb(skb);
		bc->stats.tx_packets++;
		return;
	}
}

/* ---------------------------------------------------------------------- */

static unsigned short random_seed;

static inline unsigned short random_num(void)
{
	random_seed = 28629 * random_seed + 157;
	return random_seed;
}

/* ---------------------------------------------------------------------- */

static void transmit(struct baycom_state *bc, int cnt, unsigned char stat)
{
	struct parport *pp = bc->pdev->port;
	int i;

	if (bc->hdlctx.state == tx_tail && !(stat & EPP_PTTBIT))
		bc->hdlctx.state = tx_idle;
	if (bc->hdlctx.state == tx_idle && bc->hdlctx.calibrate <= 0) {
		if (bc->hdlctx.bufcnt <= 0)
			encode_hdlc(bc);
		if (bc->hdlctx.bufcnt <= 0)
			return;
		if (!bc->ch_params.fulldup) {
			if (!(stat & EPP_DCDBIT)) {
				bc->hdlctx.slotcnt = bc->ch_params.slottime;
				return;
			}
			if ((--bc->hdlctx.slotcnt) > 0)
				return;
			bc->hdlctx.slotcnt = bc->ch_params.slottime;
			if ((random_num() % 256) > bc->ch_params.ppersist)
				return;
		}
	}
	if (bc->hdlctx.state == tx_idle && bc->hdlctx.bufcnt > 0) {
		bc->hdlctx.state = tx_keyup;
		bc->hdlctx.flags = tenms_to_flags(bc, bc->ch_params.tx_delay);
		bc->ptt_keyed++;
	}
	while (cnt > 0) {
		switch (bc->hdlctx.state) {
		case tx_keyup:
			i = min(cnt, bc->hdlctx.flags);
			cnt -= i;
			bc->hdlctx.flags -= i;
			if (bc->hdlctx.flags <= 0)
				bc->hdlctx.state = tx_data;
			for (; i > 0; i--)
				parport_epp_write_data(pp, 0x7e);
			break;

		case tx_data:
			if (bc->hdlctx.bufcnt <= 0) {
				encode_hdlc(bc);
				if (bc->hdlctx.bufcnt <= 0) {
					bc->hdlctx.state = tx_tail;
					bc->hdlctx.flags = tenms_to_flags(bc, bc->ch_params.tx_tail);
					break;
				}
			}
			i = min(cnt, bc->hdlctx.bufcnt);
			bc->hdlctx.bufcnt -= i;
			cnt -= i;
			for (; i > 0; i--)
				parport_epp_write_data(pp, *(bc->hdlctx.bufptr)++);
			break;
			
		case tx_tail:
			encode_hdlc(bc);
			if (bc->hdlctx.bufcnt > 0) {
				bc->hdlctx.state = tx_data;
				break;
			}
			i = min(cnt, bc->hdlctx.flags);
			if (i) {
				cnt -= i;
				bc->hdlctx.flags -= i;
				for (; i > 0; i--)
					parport_epp_write_data(pp, 0x7e);
				break;
			}

		default:  /* fall through */
			if (bc->hdlctx.calibrate <= 0)
				return;
			i = min(cnt, bc->hdlctx.calibrate);
			cnt -= i;
			bc->hdlctx.calibrate -= i;
			for (; i > 0; i--)
				parport_epp_write_data(pp, 0);
			break;
		}
	}
}

/* ---------------------------------------------------------------------- */

static void do_rxpacket(struct device *dev)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;
	struct sk_buff *skb;
	unsigned char *cp;
	unsigned pktlen;

	if (bc->hdlcrx.bufcnt < 4) 
		return;
	if (!check_crc_ccitt(bc->hdlcrx.buf, bc->hdlcrx.bufcnt)) 
		return;
	pktlen = bc->hdlcrx.bufcnt-2+1; /* KISS kludge */
	if (!(skb = dev_alloc_skb(pktlen))) {
		printk("%s: memory squeeze, dropping packet\n", bc->ifname);
		bc->stats.rx_dropped++;
		return;
	}
	skb->dev = dev;
	cp = skb_put(skb, pktlen);
	*cp++ = 0; /* KISS kludge */
	memcpy(cp, bc->hdlcrx.buf, pktlen - 1);
	skb->protocol = htons(ETH_P_AX25);
	skb->mac.raw = skb->data;
	netif_rx(skb);
	bc->stats.rx_packets++;
}

#define DECODEITERA(j)                                                        \
({                                                                            \
        if (!(notbitstream & (0x0fc << j)))              /* flag or abort */  \
                goto flgabrt##j;                                              \
        if ((bitstream & (0x1f8 << j)) == (0xf8 << j))   /* stuffed bit */    \
                goto stuff##j;                                                \
  enditer##j:                                                                 \
})

#define DECODEITERB(j)                                                                 \
({                                                                                     \
  flgabrt##j:                                                                          \
        if (!(notbitstream & (0x1fc << j))) {              /* abort received */        \
                state = 0;                                                             \
                goto enditer##j;                                                       \
        }                                                                              \
        if ((bitstream & (0x1fe << j)) != (0x0fc << j))   /* flag received */          \
                goto enditer##j;                                                       \
        if (state)                                                                     \
                do_rxpacket(dev);                                                      \
        bc->hdlcrx.bufcnt = 0;                                                         \
        bc->hdlcrx.bufptr = bc->hdlcrx.buf;                                            \
        state = 1;                                                                     \
        numbits = 7-j;                                                                 \
        goto enditer##j;                                                               \
  stuff##j:                                                                            \
        numbits--;                                                                     \
        bitbuf = (bitbuf & ((~0xff) << j)) | ((bitbuf & ~((~0xff) << j)) << 1);        \
        goto enditer##j;                                                               \
})
        
static void receive(struct device *dev, int cnt)
{
	struct baycom_state *bc = (struct baycom_state *)dev->priv;
	struct parport *pp = bc->pdev->port;
        unsigned int bitbuf, notbitstream, bitstream, numbits, state;
        unsigned char ch;
        
        numbits = bc->hdlcrx.numbits;
	state = bc->hdlcrx.state;
	bitstream = bc->hdlcrx.bitstream;
	bitbuf = bc->hdlcrx.bitbuf;
	for (; cnt > 0; cnt--) {
		ch = parport_epp_read_data(pp);
		bitstream >>= 8;
                bitstream |= ch << 8;
                bitbuf >>= 8;
                bitbuf |= ch << 8;
                numbits += 8;
                notbitstream = ~bitstream;
                DECODEITERA(0);
                DECODEITERA(1);
                DECODEITERA(2);
                DECODEITERA(3);
                DECODEITERA(4);
                DECODEITERA(5);
                DECODEITERA(6);
                DECODEITERA(7);
                goto enddec;
                DECODEITERB(0);
                DECODEITERB(1);
                DECODEITERB(2);
                DECODEITERB(3);
                DECODEITERB(4);
                DECODEITERB(5);
                DECODEITERB(6);
                DECODEITERB(7);
          enddec:
                while (state && numbits >= 8) {
                        if (bc->hdlcrx.bufcnt >= TXBUFFER_SIZE) {
                                state = 0;
                        } else {
				*(bc->hdlcrx.bufptr)++ = bitbuf >> (16-numbits);
				bc->hdlcrx.bufcnt++;
				numbits -= 8;
			}
		}
        }
        bc->hdlcrx.numbits = numbits;
	bc->hdlcrx.state = state;
	bc->hdlcrx.bitstream = bitstream;
	bc->hdlcrx.bitbuf = bitbuf;
}

/* --------------------------------------------------------------------- */

#ifdef __i386__
#define GETTICK(x)                                                \
({                                                                \
	if (current_cpu_data.x86_capability & X86_FEATURE_TSC)    \
		__asm__ __volatile__("rdtsc" : "=a" (x) : : "dx");\
})
#else /* __i386__ */
#define GETTICK(x)
#endif /* __i386__ */

static void epp_bh(struct device *dev)
{
	struct baycom_state *bc;
	struct parport *pp;
	unsigned char stat;
	unsigned int time1 = 0, time2 = 0, time3 = 0;
	int cnt, cnt2;
	
	baycom_paranoia_check_void(dev, "epp_bh");
	bc = (struct baycom_state *)dev->priv;
	if (!bc->bh_running)
		return;
	baycom_int_freq(bc);
	pp = bc->pdev->port;
	/* update status */
	bc->stat = stat = parport_epp_read_addr(pp);
	bc->debug_vals.last_pllcorr = stat;
	GETTICK(time1);
	if (bc->modem == EPP_FPGAEXTSTATUS) {
		/* get input count */
		parport_epp_write_addr(pp, EPP_TX_FIFO_ENABLE|EPP_RX_FIFO_ENABLE|EPP_MODEM_ENABLE|1);
		cnt = parport_epp_read_addr(pp);
		cnt |= parport_epp_read_addr(pp) << 8;
		cnt &= 0x7fff;
		/* get output count */
		parport_epp_write_addr(pp, EPP_TX_FIFO_ENABLE|EPP_RX_FIFO_ENABLE|EPP_MODEM_ENABLE|2);
		cnt2 = parport_epp_read_addr(pp);
		cnt2 |= parport_epp_read_addr(pp) << 8;
		cnt2 = 16384 - (cnt2 & 0x7fff);
		/* return to normal */
		parport_epp_write_addr(pp, EPP_TX_FIFO_ENABLE|EPP_RX_FIFO_ENABLE|EPP_MODEM_ENABLE);
		transmit(bc, cnt2, stat);
		GETTICK(time2);
		receive(dev, cnt);
		bc->stat = stat = parport_epp_read_addr(pp);
	} else {
		/* try to tx */
		switch (stat & (EPP_NTAEF|EPP_NTHF)) {
		case EPP_NTHF:
			cnt = 2048 - 256;
			break;
		
		case EPP_NTAEF:
			cnt = 2048 - 1793;
			break;
		
		case 0:
			cnt = 0;
			break;
		
		default:
			cnt = 2048 - 1025;
			break;
		}
		transmit(bc, cnt, stat);
		GETTICK(time2);
		/* do receiver */
		while ((stat & (EPP_NRAEF|EPP_NRHF)) != EPP_NRHF) {
			switch (stat & (EPP_NRAEF|EPP_NRHF)) {
			case EPP_NRAEF:
				cnt = 1025;
				break;

			case 0:
				cnt = 1793;
				break;

			default:
				cnt = 256;
				break;
			}
			receive(dev, cnt);
			stat = parport_epp_read_addr(pp);
			if (parport_epp_check_timeout(pp))
				goto epptimeout;
		}
		cnt = 0;
		if (bc->bitrate < 50000)
			cnt = 256;
		else if (bc->bitrate < 100000)
			cnt = 128;
		while (cnt > 0 && stat & EPP_NREF) {
			receive(dev, 1);
			cnt--;
			stat = parport_epp_read_addr(pp);
		}
	}
	GETTICK(time3);
#ifdef BAYCOM_DEBUG
	bc->debug_vals.mod_cycles = time2 - time1;
	bc->debug_vals.demod_cycles = time3 - time2;
#endif /* BAYCOM_DEBUG */
	if (parport_epp_check_timeout(pp)) 
		goto epptimeout;
	queue_task(&bc->run_bh, &tq_timer);
	return;
 epptimeout:
	printk(KERN_ERR "%s: EPP timeout!\n", bc_drvname);
}

/* ---------------------------------------------------------------------- */
/*
 * ===================== network driver interface =========================
 */

static int baycom_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct baycom_state *bc;

	baycom_paranoia_check(dev, "baycom_send_packet", 0);
	bc = (struct baycom_state *)dev->priv;
	skb_queue_tail(&bc->send_queue, skb);
	dev->trans_start = jiffies;	
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_set_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *)addr;

	/* addr is an AX.25 shifted ASCII mac address */
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len); 
	return 0;                                         
}

/* --------------------------------------------------------------------- */

static struct net_device_stats *baycom_get_stats(struct device *dev)
{
	struct baycom_state *bc;

	baycom_paranoia_check(dev, "baycom_get_stats", NULL);
	bc = (struct baycom_state *)dev->priv;
	/* 
	 * Get the current statistics.  This may be called with the
	 * card open or closed. 
	 */
	return &bc->stats;
}

/* --------------------------------------------------------------------- */

static void epp_wakeup(void *handle)
{
        struct device *dev = (struct device *)handle;
        struct baycom_state *bc;

	baycom_paranoia_check_void(dev, "epp_wakeup");
        bc = (struct baycom_state *)dev->priv;
        printk(KERN_DEBUG "baycom_epp: %s: why am I being woken up?\n", dev->name);
        if (!parport_claim(bc->pdev))
                printk(KERN_DEBUG "baycom_epp: %s: I'm broken.\n", dev->name);
}

/* --------------------------------------------------------------------- */

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */

static int epp_open(struct device *dev)
{
	struct baycom_state *bc;
        struct parport *pp;
	const struct tq_struct run_bh = {
		0, 0, (void *)(void *)epp_bh, dev
	};
	unsigned int i, j;
	unsigned char stat;
	unsigned long tstart;
	
	baycom_paranoia_check(dev, "epp_open", -ENXIO);
	bc = (struct baycom_state *)dev->priv;
	if (dev->start)
		return 0;
        pp = parport_enumerate();
        while (pp && pp->base != dev->base_addr) 
                pp = pp->next;
        if (!pp) {
                printk(KERN_ERR "%s: parport at 0x%lx unknown\n", bc_drvname, dev->base_addr);
                return -ENXIO;
        }
#if 0
        if (pp->irq < 0) {
                printk(KERN_ERR "%s: parport at 0x%lx has no irq\n", bc_drvname, pp->base);
                return -ENXIO;
        }
#endif
	memset(&bc->modem, 0, sizeof(bc->modem));
        if (!(bc->pdev = parport_register_device(pp, dev->name, NULL, epp_wakeup, 
                                                 epp_interrupt, PARPORT_DEV_EXCL, dev))) {
                printk(KERN_ERR "%s: cannot register parport at 0x%lx\n", bc_drvname, pp->base);
                return -ENXIO;
        }
        if (parport_claim(bc->pdev)) {
                printk(KERN_ERR "%s: parport at 0x%lx busy\n", bc_drvname, pp->base);
                parport_unregister_device(bc->pdev);
                return -EBUSY;
        }
	if (!(pp->modes & (PARPORT_MODE_PCECPEPP|PARPORT_MODE_PCEPP))) {
                printk(KERN_ERR "%s: parport at 0x%lx does not support any EPP mode\n",
		       bc_drvname, pp->base);
                parport_unregister_device(bc->pdev);
                return -EIO;		
	}
        dev->irq = /*pp->irq*/ 0;
	bc->run_bh = run_bh;
	bc->bh_running = 1;
	if (pp->modes & PARPORT_MODE_PCECPEPP) {
		printk(KERN_INFO "%s: trying to enable EPP mode\n", bc_drvname);
		parport_frob_econtrol(pp, 0xe0, 0x80);
	}
        /* bc->pdev->port->ops->change_mode(bc->pdev->port, PARPORT_MODE_PCEPP);  not yet implemented */
	bc->modem = EPP_CONVENTIONAL;
	if (eppconfig(bc))
		printk(KERN_INFO "%s: no FPGA detected, assuming conventional EPP modem\n", bc_drvname);
	else
		bc->modem = /*EPP_FPGA*/ EPP_FPGAEXTSTATUS;
	parport_write_control(pp, LPTCTRL_PROGRAM); /* prepare EPP mode; we aren't using interrupts */
	/* reset the modem */
	parport_epp_write_addr(pp, 0);
	parport_epp_write_addr(pp, EPP_TX_FIFO_ENABLE|EPP_RX_FIFO_ENABLE|EPP_MODEM_ENABLE);
	/* autoprobe baud rate */
	tstart = jiffies;
	i = 0;
	while ((signed)(jiffies-tstart-HZ/3) < 0) {
		stat = parport_epp_read_addr(pp);
		if ((stat & (EPP_NRAEF|EPP_NRHF)) == EPP_NRHF) {
			schedule();
			continue;
		}
		for (j = 0; j < 256; j++)
			parport_epp_read_data(pp);
		i += 256;
	}
	for (j = 0; j < 256; j++) {
		stat = parport_epp_read_addr(pp);
		if (!(stat & EPP_NREF))
			break;
		parport_epp_read_data(pp);
		i++;
	}
	tstart = jiffies - tstart;
	bc->bitrate = i * (8 * HZ) / tstart;
	j = 1;
	i = bc->bitrate >> 3;
	while (j < 7 && i > 150) {
		j++;
		i >>= 1;
	}
	printk(KERN_INFO "%s: autoprobed bitrate: %d  int divider: %d  int rate: %d\n", 
	       bc_drvname, bc->bitrate, j, bc->bitrate >> (j+2));
	parport_epp_write_addr(pp, EPP_TX_FIFO_ENABLE|EPP_RX_FIFO_ENABLE|EPP_MODEM_ENABLE/*|j*/);
	/*
	 * initialise hdlc variables
	 */
	bc->hdlcrx.state = 0;
	bc->hdlcrx.numbits = 0;
	bc->hdlctx.state = tx_idle;
	bc->hdlctx.bufcnt = 0;
	bc->hdlctx.slotcnt = bc->ch_params.slottime;
	bc->hdlctx.calibrate = 0;
        dev->start = 1;
       	dev->tbusy = 0;
	dev->interrupt = 0;
	/* start the bottom half stuff */
	queue_task(&bc->run_bh, &tq_timer);
	MOD_INC_USE_COUNT;
	return 0;

#if 0
  errreturn:
        parport_release(bc->pdev);
        parport_unregister_device(bc->pdev);
	return -EIO;
#endif
}

/* --------------------------------------------------------------------- */

static int epp_close(struct device *dev)
{
	struct baycom_state *bc;
	struct parport *pp;
	struct sk_buff *skb;

	baycom_paranoia_check(dev, "epp_close", -EINVAL);
	if (!dev->start)
		return 0;
	bc = (struct baycom_state *)dev->priv;
	pp = bc->pdev->port;
	bc->bh_running = 0;
	dev->start = 0;
	dev->tbusy = 1;
	run_task_queue(&tq_timer);  /* dequeue bottom half */
	bc->stat = EPP_DCDBIT;
	parport_epp_write_addr(pp, 0);
	parport_write_control(pp, 0); /* reset the adapter */
        parport_release(bc->pdev);
        parport_unregister_device(bc->pdev);
        /* Free any buffers left in the hardware transmit queue */
        while ((skb = skb_dequeue(&bc->send_queue)))
			dev_kfree_skb(skb);
	printk(KERN_INFO "%s: close epp at iobase 0x%lx irq %u\n",
	       bc_drvname, dev->base_addr, dev->irq);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	const char *cp;

	if (strstr(modestr,"intclk"))
		bc->cfg.intclk = 1;
	if (strstr(modestr,"extclk"))
		bc->cfg.intclk = 0;
	if (strstr(modestr,"intmodem"))
		bc->cfg.extmodem = 0;
	if (strstr(modestr,"extmodem"))
		bc->cfg.extmodem = 1;
	if (strstr(modestr,"noloopback"))
		bc->cfg.loopback = 0;
	if (strstr(modestr,"loopback"))
		bc->cfg.loopback = 1;
	if ((cp = strstr(modestr,"divider="))) {
		bc->cfg.divider = simple_strtoul(cp+8, NULL, 0);
		if (bc->cfg.divider < 1)
			bc->cfg.divider = 1;
		if (bc->cfg.divider > 1023)
			bc->cfg.divider = 1023;
	}
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	struct baycom_state *bc;
	struct baycom_ioctl bi;
	struct hdlcdrv_ioctl hi;
	struct sm_ioctl si;

	baycom_paranoia_check(dev, "baycom_ioctl", -EINVAL);
	bc = (struct baycom_state *)dev->priv;
	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;
	if (get_user(cmd, (int *)ifr->ifr_data))
		return -EFAULT;
#ifdef BAYCOM_DEBUG
	if (cmd == BAYCOMCTL_GETDEBUG) {
		bi.data.dbg.debug1 = bc->ptt_keyed;
		bi.data.dbg.debug2 = bc->debug_vals.last_intcnt;
		bi.data.dbg.debug3 = bc->debug_vals.last_pllcorr;
		bc->debug_vals.last_intcnt = 0;
		if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SMCTL_GETDEBUG) {
                si.data.dbg.int_rate = bc->debug_vals.last_intcnt;
                si.data.dbg.mod_cycles = bc->debug_vals.mod_cycles;
                si.data.dbg.demod_cycles = bc->debug_vals.demod_cycles;
                si.data.dbg.dma_residue = 0;
                bc->debug_vals.mod_cycles = bc->debug_vals.demod_cycles = 0;
		bc->debug_vals.last_intcnt = 0;
                if (copy_to_user(ifr->ifr_data, &si, sizeof(si)))
                        return -EFAULT;
                return 0;
	}
#endif /* BAYCOM_DEBUG */

	if (copy_from_user(&hi, ifr->ifr_data, sizeof(hi)))
		return -EFAULT;
	switch (hi.cmd) {
	default:
		return -ENOIOCTLCMD;

	case HDLCDRVCTL_GETCHANNELPAR:
		hi.data.cp.tx_delay = bc->ch_params.tx_delay;
		hi.data.cp.tx_tail = bc->ch_params.tx_tail;
		hi.data.cp.slottime = bc->ch_params.slottime;
		hi.data.cp.ppersist = bc->ch_params.ppersist;
		hi.data.cp.fulldup = bc->ch_params.fulldup;
		break;

	case HDLCDRVCTL_SETCHANNELPAR:
		if (!suser())
			return -EACCES;
		bc->ch_params.tx_delay = hi.data.cp.tx_delay;
		bc->ch_params.tx_tail = hi.data.cp.tx_tail;
		bc->ch_params.slottime = hi.data.cp.slottime;
		bc->ch_params.ppersist = hi.data.cp.ppersist;
		bc->ch_params.fulldup = hi.data.cp.fulldup;
		bc->hdlctx.slotcnt = 1;
		return 0;
		
	case HDLCDRVCTL_GETMODEMPAR:
		hi.data.mp.iobase = dev->base_addr;
		hi.data.mp.irq = dev->irq;
		hi.data.mp.dma = dev->dma;
		hi.data.mp.dma2 = 0;
		hi.data.mp.seriobase = 0;
		hi.data.mp.pariobase = 0;
		hi.data.mp.midiiobase = 0;
		break;

	case HDLCDRVCTL_SETMODEMPAR:
		if ((!suser()) || dev->start)
			return -EACCES;
		dev->base_addr = hi.data.mp.iobase;
		dev->irq = /*hi.data.mp.irq*/0;
		dev->dma = /*hi.data.mp.dma*/0;
		return 0;	
		
	case HDLCDRVCTL_GETSTAT:
		hi.data.cs.ptt = !!(bc->stat & EPP_PTTBIT);
		hi.data.cs.dcd = !(bc->stat & EPP_DCDBIT);
		hi.data.cs.ptt_keyed = bc->ptt_keyed;
		hi.data.cs.tx_packets = bc->stats.tx_packets;
		hi.data.cs.tx_errors = bc->stats.tx_errors;
		hi.data.cs.rx_packets = bc->stats.rx_packets;
		hi.data.cs.rx_errors = bc->stats.rx_errors;
		break;		

	case HDLCDRVCTL_OLDGETSTAT:
		hi.data.ocs.ptt = !!(bc->stat & EPP_PTTBIT);
		hi.data.ocs.dcd = !(bc->stat & EPP_DCDBIT);
		hi.data.ocs.ptt_keyed = bc->ptt_keyed;
		break;		

	case HDLCDRVCTL_CALIBRATE:
		bc->hdlctx.calibrate = hi.data.calibrate * bc->bitrate / 8;
		return 0;

	case HDLCDRVCTL_DRIVERNAME:
		strncpy(hi.data.drivername, "baycom_epp", sizeof(hi.data.drivername));
		break;
		
	case HDLCDRVCTL_GETMODE:
		sprintf(hi.data.modename, "%sclk,%smodem,divider=%d%s", 
			bc->cfg.intclk ? "int" : "ext",
			bc->cfg.extmodem ? "ext" : "int", bc->cfg.divider,
			bc->cfg.loopback ? ",loopback" : "");
		break;

	case HDLCDRVCTL_SETMODE:
		if (!suser() || dev->start)
			return -EACCES;
		hi.data.modename[sizeof(hi.data.modename)-1] = '\0';
		return baycom_setmode(bc, hi.data.modename);

	case HDLCDRVCTL_MODELIST:
		strncpy(hi.data.modename, "intclk,extclk,intmodem,extmodem,divider=x",
			sizeof(hi.data.modename));
		break;

	case HDLCDRVCTL_MODEMPARMASK:
		return HDLCDRV_PARMASK_IOBASE;

	}
	if (copy_to_user(ifr->ifr_data, &hi, sizeof(hi)))
		return -EFAULT;
	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * Check for a network adaptor of this type, and return '0' if one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 * If dev->base_addr == 2, allocate space for the device and return success
 * (detachable devices only).
 */
static int baycom_probe(struct device *dev)
{
	static char ax25_bcast[AX25_ADDR_LEN] = {
		'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '0' << 1
	};
	static char ax25_nocall[AX25_ADDR_LEN] = {
		'L' << 1, 'I' << 1, 'N' << 1, 'U' << 1, 'X' << 1, ' ' << 1, '1' << 1
	};
	const struct hdlcdrv_channel_params dflt_ch_params = { 
		20, 2, 10, 40, 0 
	};
	struct baycom_state *bc;

	if (!dev)
		return -ENXIO;
	baycom_paranoia_check(dev, "baycom_probe", -ENXIO);
	/*
	 * not a real probe! only initialize data structures
	 */
	bc = (struct baycom_state *)dev->priv;
	/*
	 * initialize the baycom_state struct
	 */
	bc->ch_params = dflt_ch_params;
	bc->ptt_keyed = 0;

	/*
	 * initialize the device struct
	 */
	dev->open = epp_open;
	dev->stop = epp_close;
	dev->do_ioctl = baycom_ioctl;
	dev->hard_start_xmit = baycom_send_packet;
	dev->get_stats = baycom_get_stats;

	/* Fill in the fields of the device structure */
	dev_init_buffers(dev);

	skb_queue_head_init(&bc->send_queue);
	
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
	dev->hard_header = ax25_encapsulate;
	dev->rebuild_header = ax25_rebuild_header;
#else /* CONFIG_AX25 || CONFIG_AX25_MODULE */
	dev->hard_header = NULL;
	dev->rebuild_header = NULL;
#endif /* CONFIG_AX25 || CONFIG_AX25_MODULE */
	dev->set_mac_address = baycom_set_mac_address;
	
	dev->type = ARPHRD_AX25;           /* AF_AX25 device */
	dev->hard_header_len = AX25_MAX_HEADER_LEN + AX25_BPQ_HEADER_LEN;
	dev->mtu = AX25_DEF_PACLEN;        /* eth_mtu is the default */
	dev->addr_len = AX25_ADDR_LEN;     /* sizeof an ax.25 address */
	memcpy(dev->broadcast, ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr, ax25_nocall, AX25_ADDR_LEN);

	/* New style flags */
	dev->flags = 0;

	return 0;
}

/* --------------------------------------------------------------------- */

__initfunc(int baycom_epp_init(void))
{
	struct device *dev;
	int i, found = 0;
	char set_hw = 1;
	struct baycom_state *bc;

	printk(bc_drvinfo);
	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		dev = baycom_device+i;
		if (!baycom_ports[i].mode)
			set_hw = 0;
		if (!set_hw)
			baycom_ports[i].iobase = 0;
		memset(dev, 0, sizeof(struct device));
		if (!(bc = dev->priv = kmalloc(sizeof(struct baycom_state), GFP_KERNEL)))
			return -ENOMEM;
		/*
		 * initialize part of the baycom_state struct
		 */
		memset(bc, 0, sizeof(struct baycom_state));
		bc->magic = BAYCOM_MAGIC;
		sprintf(bc->ifname, "bce%d", i);
		/*
		 * initialize part of the device struct
		 */
		dev->name = bc->ifname;
		dev->if_port = 0;
		dev->init = baycom_probe;
		dev->start = 0;
		dev->tbusy = 1;
		dev->base_addr = baycom_ports[i].iobase;
		dev->irq = 0;
		dev->dma = 0;
		if (register_netdev(dev)) {
			printk(KERN_WARNING "%s: cannot register net device %s\n", bc_drvname, bc->ifname);
			kfree(dev->priv);
			return -ENXIO;
		}
		if (set_hw && baycom_setmode(bc, baycom_ports[i].mode))
			set_hw = 0;
		found++;
	}
	if (!found)
		return -ENXIO;
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

/*
 * command line settable parameters
 */
static const char *mode[NR_PORTS] = { "epp", };
static int iobase[NR_PORTS] = { 0x378, };

#if LINUX_VERSION_CODE >= 0x20115

MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode, "baycom operating mode; epp");
MODULE_PARM(iobase, "i");
MODULE_PARM_DESC(iobase, "baycom io base address");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom epp amateur radio modem driver");

#endif

__initfunc(int init_module(void))
{
        int i;

        for (i = 0; (i < NR_PORTS) && (mode[i]); i++) {
                baycom_ports[i].mode = mode[i];
                baycom_ports[i].iobase = iobase[i];
        }
        if (i < NR_PORTS-1)
                baycom_ports[i+1].mode = NULL;
        return baycom_epp_init();
}

/* --------------------------------------------------------------------- */

void cleanup_module(void)
{
	struct device *dev;
	struct baycom_state *bc;
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		dev = baycom_device+i;
		bc = (struct baycom_state *)dev->priv;
		if (bc) {
			if (bc->magic == BAYCOM_MAGIC) {
				unregister_netdev(dev);
				kfree(dev->priv);
			} else
				printk(paranoia_str, "cleanup_module");
		}
	}
}

#else /* MODULE */
/* --------------------------------------------------------------------- */
/*
 * format: baycom=io,mode
 * mode: epp
 */

__initfunc(void baycom_epp_setup(char *str, int *ints))
{
	int i;

	for (i = 0; (i < NR_PORTS) && (baycom_ports[i].mode); i++);
	if ((i >= NR_PORTS) || (ints[0] < 1)) {
		printk(KERN_INFO "%s: too many or invalid interface "
		       "specifications\n", bc_drvname);
		return;
	}
	baycom_ports[i].mode = str;
	baycom_ports[i].iobase = ints[1];
	if (i < NR_PORTS-1)
		baycom_ports[i+1].mode = NULL;
}

#endif /* MODULE */
/* --------------------------------------------------------------------- */
