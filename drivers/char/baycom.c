/*****************************************************************************/

/*
 *	baycom.c  -- baycom ser12 and par96 radio modem driver.
 *
 *	Copyright (C) 1996  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 *  Supported modems
 *
 *  ser12: This is a very simple 1200 baud AFSK modem. The modem consists only
 *         of a modulator/demodulator chip, usually a TI TCM3105. The computer
 *         is responsible for regenerating the receiver bit clock, as well as
 *         for handling the HDLC protocol. The modem connects to a serial port,
 *         hence the name. Since the serial port is not used as an async serial
 *         port, the kernel driver for serial ports cannot be used, and this
 *         driver only supports standard serial hardware (8250, 16450, 16550)
 *  
 *  par96: This is a modem for 9600 baud FSK compatible to the G3RUH standard.
 *         The modem does all the filtering and regenerates the receiver clock.
 *         Data is transferred from and to the PC via a shift register.
 *         The shift register is filled with 16 bits and an interrupt is
 *         signalled. The PC then empties the shift register in a burst. This
 *         modem connects to the parallel port, hence the name. The modem
 *         leaves the implementation of the HDLC protocol and the scrambler
 *         polynomial to the PC.
 *  
 *  par97: This is a redesign of the par96 modem by Henning Rech, DF9IC. The
 *         modem is protocol compatible to par96, but uses only three low
 *         power ICs and can therefore be fed from the parallel port and
 *         does not require an additional power supply.
 *
 *
 *  Command line options (insmod command line)
 * 
 *  major    major number the driver should use; default 60 
 *  modem    modem type of the first channel (minor 0); 1=ser12,
 *           2=par96/par97, any other value invalid
 *  iobase   base address of the port; common values are for ser12 0x3f8,
 *           0x2f8, 0x3e8, 0x2e8 and for par96/par97 0x378, 0x278, 0x3bc
 *  irq      interrupt line of the port; common values are for ser12 3,4
 *           and for par96/par97 7
 *  options  0=use hardware DCD, 1=use software DCD
 * 
 *
 *  History:
 *   0.1  03.05.96  Renamed from ser12 0.5 and added support for par96
 *                  Various resource allocation cleanups
 *   0.2  12.05.96  Changed major to allocated 51. Integrated into kernel
 *                  source tree
 *   0.3  04.06.96  Major bug fixed (forgot to wake up after write) which
 *                  interestingly manifested only with kernel ax25
 *                  (the slip line discipline)
 *                  introduced bottom half and tq_baycom
 *                  HDLC processing now done with interrupts on
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/baycom.h>

/* --------------------------------------------------------------------- */

#define BAYCOM_TYPE_NORMAL 0		/* not used */
#define TTY_DRIVER_TYPE_BAYCOM 6

/*
 * ser12 options:
 * BAYCOM_OPTIONS_SOFTDCD: if undefined, you must use the transmitters
 * hardware carrier detect circuitry, the driver will report DCD as soon as
 * there are transitions on the input line. Advantage: lower interrupt load
 * on the system. Disadvantage: slower, since hardware carrier detect
 * circuitry is usually slow.
 */

#define BUFLEN_RX 8192
#define BUFLEN_TX 8192

#define NR_PORTS 4

#define KISS_VERBOSE

#define BAYCOM_MAGIC 0x3105bac0

/* --------------------------------------------------------------------- */

/*
 * user settable parameters (from the command line)
 */
#ifndef MODULE
static
#endif /* MODULE */
int major = BAYCOM_MAJOR;

/* --------------------------------------------------------------------- */

static struct tty_struct *baycom_table[NR_PORTS];
static struct termios *baycom_termios[NR_PORTS];
static struct termios *baycom_termios_locked[NR_PORTS];

static int baycom_refcount;

static struct tty_driver baycom_driver;

static struct {
	int modem, iobase, irq, options;
} baycom_ports[NR_PORTS] = { { BAYCOM_MODEM_INVALID, 0, 0, 0, }, };

/* --------------------------------------------------------------------- */

#define RBR(iobase) (iobase+0)
#define THR(iobase) (iobase+0)
#define IER(iobase) (iobase+1)
#define IIR(iobase) (iobase+2)
#define FCR(iobase) (iobase+2)
#define LCR(iobase) (iobase+3)
#define MCR(iobase) (iobase+4)
#define LSR(iobase) (iobase+5)
#define MSR(iobase) (iobase+6)
#define SCR(iobase) (iobase+7)
#define DLL(iobase) (iobase+0)
#define DLM(iobase) (iobase+1)

#define SER12_EXTENT 8

#define LPT_DATA(iobase)    (iobase+0)
#define LPT_STATUS(iobase)  (iobase+1)
#define LPT_CONTROL(iobase) (iobase+2)
#define LPT_IRQ_ENABLE      0x10
#define PAR96_BURSTBITS 16
#define PAR96_BURST     4
#define PAR96_PTT       2
#define PAR96_TXBIT     1
#define PAR96_ACK       0x40
#define PAR96_RXBIT     0x20
#define PAR96_DCD       0x10
#define PAR97_POWER     0xf8

#define PAR96_EXTENT 3

/* ---------------------------------------------------------------------- */

struct access_params {
	int tx_delay;
	int tx_tail;
	int slottime;
	int ppersist;
	int fulldup;
};

struct hdlc_state_rx {
	int rx_state;	/* 0 = sync hunt, != 0 receiving */
	unsigned int bitstream;
	unsigned int bitbuf;
	int numbits;
	unsigned int shreg1, shreg2;

	int len;
	unsigned char *bp;
	unsigned char buffer[BAYCOM_MAXFLEN+2];	   /* make room for CRC */
};

struct hdlc_state_tx {
	/*
	 * 0 = send flags
	 * 1 = send txtail (flags)
	 * 2 = send packet
	 */
	int tx_state;	
	int numflags;
	unsigned int bitstream;
	unsigned int current_byte;
	unsigned char ptt;

	unsigned int bitbuf;
	int numbits;
	unsigned int shreg1, shreg2;

	int len;
	unsigned char *bp;
	unsigned char buffer[BAYCOM_MAXFLEN+2];		/* make room for CRC */
};

struct modem_state_ser12 {
	unsigned char last_sample;
	unsigned char interm_sample;
	unsigned int bit_pll;
	unsigned int dcd_shreg;
	int dcd_sum0, dcd_sum1, dcd_sum2;
	unsigned int dcd_time;
	unsigned char last_rxbit;
	unsigned char tx_bit;
};

struct modem_state_par96 {
	int dcd_count;
	unsigned int dcd_shreg;
	unsigned long descram;
	unsigned long scram;
};

struct modem_state {
	unsigned char dcd;
	short arb_divider;
	unsigned char flags;
	struct modem_state_ser12 ser12;
	struct modem_state_par96 par96;
};

struct packet_buffer {
	unsigned int rd;
	unsigned int wr;
	
	unsigned int buflen;
	unsigned char *buffer;
};

struct packet_hdr {
	unsigned int next;
	unsigned int len;
	/* packet following */
};

#ifdef BAYCOM_DEBUG
struct bit_buffer {
	unsigned int rd;
	unsigned int wr;
	unsigned int shreg;
	unsigned char buffer[64];
};

struct debug_vals {
	unsigned long last_jiffies;
	unsigned cur_intcnt;
	unsigned last_intcnt;
	int cur_pllcorr;
	int last_pllcorr;
};
#endif /* BAYCOM_DEBUG */

struct kiss_decode {
	unsigned char dec_state; /* 0 = hunt FEND */
	unsigned char escaped;
	unsigned char pkt_buf[BAYCOM_MAXFLEN+1];
	unsigned int wr;
};

/* ---------------------------------------------------------------------- */

struct baycom_state {
	int magic;

	unsigned char modem_type;

	unsigned int iobase;
	unsigned int irq;
	unsigned int options;

	int opened;
	struct tty_struct *tty;

#ifdef BAYCOM_USE_BH
	struct tq_struct tq_receiver, tq_transmitter, tq_arbitrate;
#endif /* BAYCOM_USE_BH */

	struct packet_buffer rx_buf;
	struct packet_buffer tx_buf;

	struct access_params ch_params;

	struct hdlc_state_rx hdlc_rx;
	struct hdlc_state_tx hdlc_tx;

	int calibrate;

	struct modem_state modem;

#ifdef BAYCOM_DEBUG
	struct bit_buffer bitbuf_channel;
	struct bit_buffer bitbuf_hdlc;
	
	struct debug_vals debug_vals;
#endif /* BAYCOM_DEBUG */

	struct kiss_decode kiss_decode;

	struct baycom_statistics stat;
};

/* --------------------------------------------------------------------- */

struct baycom_state baycom_state[NR_PORTS];

#ifdef BAYCOM_USE_BH
DECLARE_TASK_QUEUE(tq_baycom);
#endif /* BAYCOM_USE_BH */

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

static inline void append_crc_ccitt(unsigned char *buffer, int len)
{
 	unsigned int crc = 0xffff;

	for (;len>0;len--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buffer++) & 0xff];
	crc ^= 0xffff;
	*buffer++ = crc;
	*buffer++ = crc >> 8;
}

/*---------------------------------------------------------------------------*/

static inline int check_crc_ccitt(const unsigned char *buf,int cnt)
{
	unsigned int crc = 0xffff;

	for (; cnt > 0; cnt--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buf++) & 0xff];
	return (crc & 0xffff) == 0xf0b8;
}

/*---------------------------------------------------------------------------*/

#if 0
static int calc_crc_ccitt(const unsigned char *buf,int cnt)
{
	unsigned int crc = 0xffff;

	for (; cnt > 0; cnt--)
		crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ *buf++) & 0xff];
	crc ^= 0xffff;
	return (crc & 0xffff);
}
#endif

/* ---------------------------------------------------------------------- */

static int store_packet(struct packet_buffer *buf, unsigned char *data,
	char from_user, unsigned int len)
{
	unsigned int free;
	struct packet_hdr *hdr;
	unsigned int needed = sizeof(struct packet_hdr)+len;
	
	free = buf->rd-buf->wr;
	if(buf->rd <= buf->wr) {
		free = buf->buflen - buf->wr;
		if((free < needed) && (buf->rd >= needed)) {
			hdr = (struct packet_hdr *)(buf->buffer+buf->wr);
			hdr->next = 0;
			hdr->len = 0;
			buf->wr = 0;
			free = buf->rd;
		}
	}
	if(free < needed) return 0;		/* buffer overrun */
	hdr = (struct packet_hdr *)(buf->buffer+buf->wr);
	if (from_user) 
		memcpy_fromfs(hdr+1,data,len);
	else
		memcpy(hdr+1,data,len);
	hdr->len = len;
	hdr->next = buf->wr+needed;
	if (hdr->next + sizeof(struct packet_hdr) >= buf->buflen)
		hdr->next = 0;
	buf->wr = hdr->next;
	return 1;
}
	
/* ---------------------------------------------------------------------- */

static void get_packet(struct packet_buffer *buf, unsigned char **data,
	unsigned int *len)
{
	struct packet_hdr *hdr;
	
	*data = NULL;
	*len = 0;
	if (buf->rd == buf->wr)
		return;
	hdr = (struct packet_hdr *)(buf->buffer+buf->rd);
	while (!(hdr->len)) {
		buf->rd = hdr->next;
		if (buf->rd == buf->wr)
			return;
		hdr = (struct packet_hdr *)(buf->buffer+buf->rd);
	}
	*data = (unsigned char *)(hdr+1);
	*len = hdr->len;
}

/* ---------------------------------------------------------------------- */

static void ack_packet(struct packet_buffer *buf)
{
	struct packet_hdr *hdr;
	
	if (buf->rd == buf->wr)
		return;
	hdr = (struct packet_hdr *)(buf->buffer+buf->rd);
	buf->rd = hdr->next;
}

/* ---------------------------------------------------------------------- */

static int store_kiss_packet(struct packet_buffer *buf, unsigned char *data,
	unsigned int len)
{
	unsigned char *bp = data;
	int ln = len;
	/*
	 * variables of buf
	 */
	unsigned int rd;
	unsigned int wr;
	unsigned int buflen;
	unsigned char *buffer;

	if (!len || !data || !buf)
		return 0;
	buflen = buf->buflen;
	rd = buf->rd;
	wr = buf->wr;
	buffer = buf->buffer;
	
#define ADD_CHAR(c) {\
		buffer[wr++] = c;\
		if (wr >= buflen) wr = 0;\
		if (wr == rd) return 0;\
	}
#define ADD_KISSCHAR(c) {\
		if (((c) & 0xff) == KISS_FEND) {\
			ADD_CHAR(KISS_FESC);\
			ADD_CHAR(KISS_TFEND);\
		} else if (((c) & 0xff) == KISS_FESC) {\
			ADD_CHAR(KISS_FESC);\
			ADD_CHAR(KISS_TFESC);\
		} else {\
			ADD_CHAR(c);\
		}\
	}

	ADD_CHAR(KISS_FEND);
	ADD_KISSCHAR(KISS_CMD_DATA);
	for(; ln > 0; ln--,bp++) {
		ADD_KISSCHAR(*bp);
	}
	ADD_CHAR(KISS_FEND);
	buf->wr = wr;
#undef ADD_CHAR
#undef ADD_KISSCHAR
	return 1;
}

/* ---------------------------------------------------------------------- */

#ifdef BAYCOM_DEBUG
static inline void add_bitbuffer(struct bit_buffer * buf, unsigned int bit)
{
	unsigned char new;

	if (!buf) return;
	new = buf->shreg & 1;
	buf->shreg >>= 1;
	if (bit)
		buf->shreg |= 0x80;
	if (new) {
		buf->buffer[buf->wr] = buf->shreg;
		buf->wr = (buf->wr+1) % sizeof(buf->buffer);
		buf->shreg = 0x80;
	}
}

static inline void add_bitbuffer_word(struct bit_buffer * buf, 
				      unsigned int bits)
{
	buf->buffer[buf->wr] = bits & 0xff;
	buf->wr = (buf->wr+1) % sizeof(buf->buffer);
	buf->buffer[buf->wr] = (bits >> 8) & 0xff;
	buf->wr = (buf->wr+1) % sizeof(buf->buffer);

}
#endif /* BAYCOM_DEBUG */

/* ---------------------------------------------------------------------- */

static inline unsigned int tenms_to_flags(struct baycom_state *bc, 
					  unsigned int tenms)
{
	switch (bc->modem_type) {
	case BAYCOM_MODEM_SER12:
		return tenms * 12 / 8;
	case BAYCOM_MODEM_PAR96:
		return tenms * 12;
	default:
		return 0;
	}
}

/* ---------------------------------------------------------------------- */
/*
 * The HDLC routines
 */

static inline int hdlc_rx_add_bytes(struct baycom_state *bc, 
				    unsigned int bits, int num)
{
	int added = 0;
	while (bc->hdlc_rx.rx_state && num >= 8) {
		if (bc->hdlc_rx.len >= sizeof(bc->hdlc_rx.buffer)) {
			bc->hdlc_rx.rx_state = 0;
			return 0;
		}
		*bc->hdlc_rx.bp++ = bits >> (32-num);
		bc->hdlc_rx.len++;
		num -= 8;
		added += 8;
	}
	return added;
}

static inline void hdlc_rx_flag(struct baycom_state *bc)
{
	if (bc->hdlc_rx.len < 4) 
		return;
	if (!check_crc_ccitt(bc->hdlc_rx.buffer, bc->hdlc_rx.len)) 
		return;
       	bc->stat.rx_packets++;
	if (!store_kiss_packet(&bc->rx_buf,
			       bc->hdlc_rx.buffer,
			       bc->hdlc_rx.len-2))
		bc->stat.rx_bufferoverrun++;
}

static void hdlc_rx_word(struct baycom_state *bc, unsigned int word)
{
	int i;
	unsigned int mask1, mask2, mask3, mask4, mask5, mask6;
	
	if (!bc) return;

	word &= 0xffff;
#ifdef BAYCOM_DEBUG
	add_bitbuffer_word(&bc->bitbuf_hdlc, word);
#endif /* BAYCOM_DEBUG */
       	bc->hdlc_rx.bitstream >>= 16;
	bc->hdlc_rx.bitstream |= word << 16;
	bc->hdlc_rx.bitbuf >>= 16;
	bc->hdlc_rx.bitbuf |= word << 16;
	bc->hdlc_rx.numbits += 16;
	for(i = 15, mask1 = 0x1fc00, mask2 = 0x1fe00, mask3 = 0x0fc00,
	    mask4 = 0x1f800, mask5 = 0xf800, mask6 = 0xffff; 
	    i >= 0; 
	    i--, mask1 <<= 1, mask2 <<= 1, mask3 <<= 1, mask4 <<= 1, 
	    mask5 <<= 1, mask6 = (mask6 << 1) | 1) {
		if ((bc->hdlc_rx.bitstream & mask1) == mask1)
			bc->hdlc_rx.rx_state = 0; /* abort received */
		else if ((bc->hdlc_rx.bitstream & mask2) == mask3) {
			/* flag received */
			if (bc->hdlc_rx.rx_state) {
				hdlc_rx_add_bytes(bc, bc->hdlc_rx.bitbuf << 
						  (8 + i), bc->hdlc_rx.numbits
						  - 8 - i);
				hdlc_rx_flag(bc);
			}
			bc->hdlc_rx.len = 0;
			bc->hdlc_rx.bp = bc->hdlc_rx.buffer;
			bc->hdlc_rx.rx_state = 1;
			bc->hdlc_rx.numbits = i;
		} else if ((bc->hdlc_rx.bitstream & mask4) == mask5) {
			/* stuffed bit */
			bc->hdlc_rx.numbits--;
			bc->hdlc_rx.bitbuf = (bc->hdlc_rx.bitbuf & (~mask6)) |
				((bc->hdlc_rx.bitbuf & mask6) << 1);
		}
	}
	bc->hdlc_rx.numbits -= hdlc_rx_add_bytes(bc, bc->hdlc_rx.bitbuf,
						 bc->hdlc_rx.numbits);
}

/* ---------------------------------------------------------------------- */

static unsigned int hdlc_tx_word(struct baycom_state *bc)
{
	unsigned int mask1, mask2, mask3;
	int i;

	if (!bc || !bc->hdlc_tx.ptt)
		return 0;
	for (;;) {
		if (bc->hdlc_tx.numbits >= 16) {
			unsigned int ret = bc->hdlc_tx.bitbuf & 0xffff;
			bc->hdlc_tx.bitbuf >>= 16;
			bc->hdlc_tx.numbits -= 16;
			return ret;
		}
		switch (bc->hdlc_tx.tx_state) {
		default:
			bc->hdlc_tx.ptt = 0;
			bc->hdlc_tx.tx_state = 0;
			return 0;
		case 0:
		case 1:
			if (bc->hdlc_tx.numflags) {
				bc->hdlc_tx.numflags--;
				bc->hdlc_tx.bitbuf |= 
					0x7e7e << bc->hdlc_tx.numbits;
				bc->hdlc_tx.numbits += 16;
				break;
			}
			if (bc->hdlc_tx.tx_state == 1) {
				bc->hdlc_tx.ptt = 0;
				return 0;
			}
			get_packet(&bc->tx_buf, &bc->hdlc_tx.bp,
				   &bc->hdlc_tx.len);
			if (!bc->hdlc_tx.bp || !bc->hdlc_tx.len) {
				bc->hdlc_tx.tx_state = 1;
				bc->hdlc_tx.numflags = tenms_to_flags
					(bc, bc->ch_params.tx_tail);
				break;
			}
			if (bc->hdlc_tx.len >= BAYCOM_MAXFLEN) {
				bc->hdlc_tx.tx_state = 0;
				bc->hdlc_tx.numflags = 1;
				ack_packet(&bc->tx_buf);
				break;
			}
			memcpy(bc->hdlc_tx.buffer, bc->hdlc_tx.bp, 
			       bc->hdlc_tx.len);
			ack_packet(&bc->tx_buf);
			bc->hdlc_tx.bp = bc->hdlc_tx.buffer;
			append_crc_ccitt(bc->hdlc_tx.buffer, bc->hdlc_tx.len);
			/* the appended CRC */
			bc->hdlc_tx.len += 2; 
			bc->hdlc_tx.tx_state = 2;
			bc->hdlc_tx.bitstream = 0;
			bc->stat.tx_packets++;
			break;
		case 2:
			if (!bc->hdlc_tx.len) {
				bc->hdlc_tx.tx_state = 0;
				bc->hdlc_tx.numflags = 1;
				break;
			}
			bc->hdlc_tx.len--;
			bc->hdlc_tx.bitbuf |= *bc->hdlc_tx.bp <<
				bc->hdlc_tx.numbits;
			bc->hdlc_tx.bitstream >>= 8;
			bc->hdlc_tx.bitstream |= (*bc->hdlc_tx.bp++) << 16;
			mask1 = 0x1f000;
			mask2 = 0x10000;
			mask3 = 0xffffffff >> (31-bc->hdlc_tx.numbits);
			bc->hdlc_tx.numbits += 8;
			for(i = 0; i < 8; i++, mask1 <<= 1, mask2 <<= 1, 
			    mask3 = (mask3 << 1) | 1) {
				if ((bc->hdlc_tx.bitstream & mask1) != mask1) 
					continue;
				bc->hdlc_tx.bitstream &= ~mask2;
				bc->hdlc_tx.bitbuf = 
					(bc->hdlc_tx.bitbuf & mask3) |
						((bc->hdlc_tx.bitbuf & 
						 (~mask3)) << 1);
				bc->hdlc_tx.numbits++;
				mask3 = (mask3 << 1) | 1;
			}
			break;
		}
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

static inline void tx_arbitrate(struct baycom_state *bc)
{
	unsigned char *bp;
	unsigned int len;
	
	if (!bc || bc->hdlc_tx.ptt || bc->modem.dcd)
		return;
	get_packet(&bc->tx_buf, &bp, &len);
	if (!bp || !len)
		return;
	
	if (!bc->ch_params.fulldup) {
		if ((random_num() % 256) > bc->ch_params.ppersist)
			return;
	}
	bc->hdlc_tx.ptt = 1;
	bc->hdlc_tx.tx_state = 0;
	bc->hdlc_tx.numflags = tenms_to_flags(bc, bc->ch_params.tx_delay);
	bc->stat.ptt_keyed++;
}

/* --------------------------------------------------------------------- */

#ifdef BAYCOM_DEBUG
static void inline baycom_int_freq(struct baycom_state *bc)
{
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
}
#endif /* BAYCOM_DEBUG */

/* --------------------------------------------------------------------- */

static inline void rx_chars_to_flip(struct baycom_state *bc) 
{
	int flip_free;
	unsigned int cnt;
	unsigned int new_rd;
	unsigned long flags;

	if ((!bc) || (!bc->tty) || (bc->tty->flip.count >= TTY_FLIPBUF_SIZE) ||
	    (bc->rx_buf.rd == bc->rx_buf.wr) || 
	    (!bc->tty->flip.char_buf_ptr) ||
	    (!bc->tty->flip.flag_buf_ptr))
		return;
	for(;;) {
		flip_free = TTY_FLIPBUF_SIZE - bc->tty->flip.count;
		if (bc->rx_buf.rd <= bc->rx_buf.wr)
			cnt = bc->rx_buf.wr - bc->rx_buf.rd;
		else
			cnt = bc->rx_buf.buflen - bc->rx_buf.rd;
		if ((flip_free <= 0) || (!cnt)) {
			tty_schedule_flip(bc->tty);
			return;
		}
		if (cnt > flip_free)
			cnt = flip_free;
		save_flags(flags); cli();
		memcpy(bc->tty->flip.char_buf_ptr, bc->rx_buf.buffer+bc->rx_buf.rd, cnt);
		memset(bc->tty->flip.flag_buf_ptr, TTY_NORMAL, cnt);
		bc->tty->flip.count += cnt;
		bc->tty->flip.char_buf_ptr += cnt;
		bc->tty->flip.flag_buf_ptr += cnt;
		restore_flags(flags);
		new_rd = bc->rx_buf.rd+cnt;
		if (new_rd >= bc->rx_buf.buflen)
			new_rd -= bc->rx_buf.buflen;
		bc->rx_buf.rd = new_rd;
	}
}

/* --------------------------------------------------------------------- */
/*
 * ===================== SER12 specific routines =========================
 */

static void inline ser12_set_divisor(struct baycom_state *bc, 
				     unsigned char divisor)
{
	outb(0x81, LCR(bc->iobase));	/* DLAB = 1 */
	outb(divisor, DLL(bc->iobase));
	outb(0, DLM(bc->iobase));
	outb(0x01, LCR(bc->iobase));	/* word length = 6 */
}

/* --------------------------------------------------------------------- */

/*
 * must call the TX arbitrator every 10ms
 */
#define SER12_ARB_DIVIDER(bc) ((bc->options & BAYCOM_OPTIONS_SOFTDCD) ? \
			       36 : 24)
#define SER12_DCD_INTERVAL(bc) ((bc->options & BAYCOM_OPTIONS_SOFTDCD) ? \
				240 : 12)

static void baycom_ser12_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct baycom_state *bc = (struct baycom_state *)dev_id;
	unsigned char cur_s;
	
	if (!bc || bc->magic != BAYCOM_MAGIC)
		return;
	/*
	 * make sure the next interrupt is generated;
	 * 0 must be used to power the modem; the modem draws its
	 * power from the TxD line
	 */	
	outb(0x00, THR(bc->iobase));
	rx_chars_to_flip(bc);
#ifdef BAYCOM_DEBUG
	baycom_int_freq(bc);
#endif /* BAYCOM_DEBUG */
	/*
	 * check if transmitter active
	 */
	if (bc->hdlc_tx.ptt || bc->calibrate > 0) {
		ser12_set_divisor(bc, 12); /* one interrupt per channel bit */
		/*
		 * first output the last bit (!) then call HDLC transmitter,
		 * since this may take quite long
		 */
		outb(0x0e | (bc->modem.ser12.tx_bit ? 1 : 0), MCR(bc->iobase));
		if (bc->hdlc_tx.shreg1 <= 1) {
			if (bc->calibrate > 0) {
				bc->hdlc_tx.shreg1 = 0x10000;
				bc->calibrate--;
			} else {
#ifdef BAYCOM_USE_BH
				bc->hdlc_tx.shreg1 = bc->hdlc_tx.shreg2;
				bc->hdlc_tx.shreg2 = 0;
				queue_task_irq_off(&bc->tq_transmitter, 
						   &tq_baycom);
				mark_bh(BAYCOM_BH);
#ifdef HDLC_LOOPBACK
				bc->hdlc_rx.shreg2 = bc->hdlc_tx.shreg1;
				queue_task_irq_off(&bc->tq_receiver, 
						   &tq_baycom);
#endif /* HDLC_LOOPBACK */
#else /* BAYCOM_USE_BH */
				bc->hdlc_tx.shreg1 = hdlc_tx_word(bc) 
					| 0x10000;
#ifdef HDLC_LOOPBACK
				hdlc_rx_word(bc, bc->hdlc_tx.shreg1);
#endif /* HDLC_LOOPBACK */
#endif /* BAYCOM_USE_BH */
			}	
		}
		if (!(bc->hdlc_tx.shreg1 & 1))
			bc->modem.ser12.tx_bit = !bc->modem.ser12.tx_bit;
		bc->hdlc_tx.shreg1 >>= 1;
		return;
	}
	/*
	 * do demodulator
	 */
	outb(0x0d, MCR(bc->iobase));			/* transmitter off */
	cur_s = inb(MSR(bc->iobase)) & 0x10;	/* the CTS line */
#ifdef BAYCOM_DEBUG
	add_bitbuffer(&bc->bitbuf_channel, cur_s);
#endif /* BAYCOM_DEBUG */
	bc->modem.ser12.dcd_shreg <<= 1;
	if(cur_s != bc->modem.ser12.last_sample) {
		bc->modem.ser12.dcd_shreg |= 1;

		if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
			unsigned int dcdspos, dcdsneg;

			dcdspos = dcdsneg = 0;
			dcdspos += ((bc->modem.ser12.dcd_shreg >> 1) & 1);
			if (!(bc->modem.ser12.dcd_shreg & 0x7ffffffe))
				dcdspos += 2;
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 2) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 3) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 4) & 1);

			bc->modem.ser12.dcd_sum0 += 16*dcdspos - dcdsneg;
		} else
			bc->modem.ser12.dcd_sum0--;
	}
	bc->modem.ser12.last_sample = cur_s;
	if(!bc->modem.ser12.dcd_time) {
		bc->modem.dcd = (bc->modem.ser12.dcd_sum0 + 
				 bc->modem.ser12.dcd_sum1 +
				 bc->modem.ser12.dcd_sum2) < 0;
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		/* offset to ensure DCD off on silent input */
		bc->modem.ser12.dcd_sum0 = 2;
		bc->modem.ser12.dcd_time = SER12_DCD_INTERVAL(bc);
	}
	bc->modem.ser12.dcd_time--;
	if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
		/*
		 * PLL code for the improved software DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(bc, 4);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 7) {
			case 1: /* transition too late */
				ser12_set_divisor(bc, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 4:	/* transition too early */
				ser12_set_divisor(bc, 3);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(bc, 4);
				break;
			}
			bc->hdlc_rx.shreg1 >>= 1;
			if (bc->modem.ser12.last_sample == 
			    bc->modem.ser12.last_rxbit)
				bc->hdlc_rx.shreg1 |= 0x10000;
			bc->modem.ser12.last_rxbit = 
				bc->modem.ser12.last_sample;
		}
		if (++bc->modem.ser12.interm_sample >= 3)
			bc->modem.ser12.interm_sample = 0;		
	} else {
		/*
		 * PLL algorithm for the hardware squelch DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(bc, 6);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 3) {
			case 1: /* transition too late */
				ser12_set_divisor(bc, 7);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 2:	/* transition too early */
				ser12_set_divisor(bc, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(bc, 6);
				break;
			}
			bc->hdlc_rx.shreg1 >>= 1;
			if (bc->modem.ser12.last_sample == 
			    bc->modem.ser12.last_rxbit)
				bc->hdlc_rx.shreg1 |= 0x10000;
			bc->modem.ser12.last_rxbit = 
				bc->modem.ser12.last_sample;
		}
		bc->modem.ser12.interm_sample = !bc->modem.ser12.interm_sample;
	}
	if (bc->hdlc_rx.shreg1 & 1) {
#ifdef BAYCOM_USE_BH
		bc->hdlc_rx.shreg2 = (bc->hdlc_rx.shreg1 >> 1) | 0x10000;
		queue_task_irq_off(&bc->tq_receiver, &tq_baycom);
		mark_bh(BAYCOM_BH);
#else /* BAYCOM_USE_BH */
		hdlc_rx_word(bc, bc->hdlc_rx.shreg1 >> 1);
#endif /* BAYCOM_USE_BH */
		bc->hdlc_rx.shreg1 = 0x10000;
	}
	if (--bc->modem.arb_divider <= 0) {
#ifdef BAYCOM_USE_BH
		queue_task_irq_off(&bc->tq_arbitrate, &tq_baycom);
		mark_bh(BAYCOM_BH);
#else /* BAYCOM_USE_BH */
		tx_arbitrate(bc);
#endif /* BAYCOM_USE_BH */
		bc->modem.arb_divider = bc->ch_params.slottime * 
			SER12_ARB_DIVIDER(bc);
	}
}

/* --------------------------------------------------------------------- */

enum uart { c_uart_unknown, c_uart_8250,
	c_uart_16450, c_uart_16550, c_uart_16550A};
static const char *uart_str[] =
	{ "unknown", "8250", "16450", "16550", "16550A" };

static enum uart ser12_check_uart(unsigned int iobase)
{
	unsigned char b1,b2,b3;
	enum uart u;
	enum uart uart_tab[] =
		{ c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A };

	b1 = inb(MCR(iobase));
	outb(b1 | 0x10, MCR(iobase));	/* loopback mode */
	b2 = inb(MSR(iobase));
	outb(0x1a, MCR(iobase));
	b3 = inb(MSR(iobase)) & 0xf0;
	outb(b1, MCR(iobase));			/* restore old values */
	outb(b2, MSR(iobase));
	if (b3 != 0x90) 
		return c_uart_unknown;
	inb(RBR(iobase));
	inb(RBR(iobase));
	outb(0x01, FCR(iobase));		/* enable FIFOs */
	u = uart_tab[(inb(IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, SCR(iobase));
		b1 = inb(SCR(iobase));
		outb(0xa5, SCR(iobase));
		b2 = inb(SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5)) 
			u = c_uart_8250;
	}
	return u;
}

/* --------------------------------------------------------------------- */

static int ser12_allocate_resources(unsigned int iobase, unsigned int irq,
				    unsigned int options)
{
	enum uart u;

	if (!iobase || iobase > 0xfff || irq < 2 || irq > 15)
		return -ENXIO;
	if (check_region(iobase, SER12_EXTENT))
		return -EACCES;
	if ((u = ser12_check_uart(iobase)) == c_uart_unknown)
		return -EIO;
	request_region(iobase, SER12_EXTENT, "baycom_ser12");
	outb(0, FCR(iobase));		/* disable FIFOs */
	outb(0x0d, MCR(iobase));
	printk(KERN_INFO "baycom: ser12 at iobase 0x%x irq %u options 0x%x "
	       "uart %s\n", iobase, irq, options, uart_str[u]);
	return 0;
}
	
/* --------------------------------------------------------------------- */

static void ser12_deallocate_resources(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_SER12)
		return;
	/*
	 * disable interrupts
	 */
	outb(0, IER(bc->iobase));
	outb(1, MCR(bc->iobase));
	/* 
	 * this should prevent kernel: Trying to free IRQx
	 * messages
	 */
	if (bc->opened > 0)
		free_irq(bc->irq, bc);
	release_region(bc->iobase, SER12_EXTENT);
	bc->modem_type = BAYCOM_MODEM_INVALID;
	printk(KERN_INFO "baycom: release ser12 at iobase 0x%x irq %u\n",
	       bc->iobase, bc->irq);
	bc->iobase = bc->irq = bc->options = 0;
}

/* --------------------------------------------------------------------- */

static int ser12_on_open(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_SER12)
		return -ENXIO;
	/*
	 * set the SIO to 6 Bits/character and 19200 or 28800 baud, so that
	 * we get exactly (hopefully) 2 or 3 interrupts per radio symbol,
	 * depending on the usage of the software DCD routine
	 */
	ser12_set_divisor(bc, (bc->options & BAYCOM_OPTIONS_SOFTDCD) ? 4 : 6);
	outb(0x0d, MCR(bc->iobase));
	outb(0, IER(bc->iobase));
	if (request_irq(bc->irq, baycom_ser12_interrupt, 0, 
			"baycom_ser12", bc))
		return -EBUSY;
	/*
	 * enable transmitter empty interrupt
	 */
	outb(2, IER(bc->iobase));  
	/* 
	 * the value here serves to power the modem
	 */     
	outb(0x00, THR(bc->iobase));
	return 0;
}

/* --------------------------------------------------------------------- */

static void ser12_on_close(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_SER12)
		return;
	/*
	 * disable interrupts
	 */
	outb(0, IER(bc->iobase));
	outb(1, MCR(bc->iobase));
	free_irq(bc->irq, bc);	
}

/* --------------------------------------------------------------------- */
/*
 * ===================== PAR96 specific routines =========================
 */

#define PAR96_DESCRAM_TAP1 0x20000
#define PAR96_DESCRAM_TAP2 0x01000
#define PAR96_DESCRAM_TAP3 0x00001

#define PAR96_DESCRAM_TAPSH1 17
#define PAR96_DESCRAM_TAPSH2 12
#define PAR96_DESCRAM_TAPSH3 0

#define PAR96_SCRAM_TAP1 0x20000 /* X^17 */
#define PAR96_SCRAM_TAPN 0x00021 /* X^0+X^5 */

/* --------------------------------------------------------------------- */

static void baycom_par96_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	register struct baycom_state *bc = (struct baycom_state *)dev_id;
	int i;
	unsigned int data, mask, mask2;
	
	if (!bc || bc->magic != BAYCOM_MAGIC)
		return;

	rx_chars_to_flip(bc);
#ifdef BAYCOM_DEBUG
	baycom_int_freq(bc);
#endif /* BAYCOM_DEBUG */
	/*
	 * check if transmitter active
	 */
	if (bc->hdlc_tx.ptt || bc->calibrate > 0) {
		/*
		 * first output the last 16 bits (!) then call HDLC
		 * transmitter, since this may take quite long
		 * do the differential encoder and the scrambler on the fly
		 */
		data = bc->hdlc_tx.shreg1;
		for(i = 0; i < PAR96_BURSTBITS; i++, data >>= 1) {
			unsigned char val = PAR97_POWER;
			bc->modem.par96.scram = ((bc->modem.par96.scram << 1) |
						 (bc->modem.par96.scram & 1));
			if (!(data & 1))
				bc->modem.par96.scram ^= 1;
			if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 1))
				bc->modem.par96.scram ^= 
					(PAR96_SCRAM_TAPN << 1);
			if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 2))
				val |= PAR96_TXBIT;
			outb(val, LPT_DATA(bc->iobase));
			outb(val | PAR96_BURST, LPT_DATA(bc->iobase));
		}
		if (bc->calibrate > 0) {
			bc->hdlc_tx.shreg1 = 0x10000;
			bc->calibrate--;
		} else {
#ifdef BAYCOM_USE_BH
			bc->hdlc_tx.shreg1 = bc->hdlc_tx.shreg2;
			bc->hdlc_tx.shreg2 = 0;
			queue_task_irq_off(&bc->tq_transmitter, &tq_baycom);
			mark_bh(BAYCOM_BH);
#ifdef HDLC_LOOPBACK
			bc->hdlc_rx.shreg2 = bc->hdlc_tx.shreg1;
			queue_task_irq_off(&bc->tq_receiver, &tq_baycom);
#endif /* HDLC_LOOPBACK */
#else /* BAYCOM_USE_BH */
			bc->hdlc_tx.shreg1 = hdlc_tx_word(bc);
#ifdef HDLC_LOOPBACK
			hdlc_rx_word(bc, bc->hdlc_tx.shreg1);
#endif /* HDLC_LOOPBACK */
#endif /* BAYCOM_USE_BH */
		}
		return;
	}
	/*
	 * do receiver; differential decode and descramble on the fly
	 */
	for(data = i = 0; i < PAR96_BURSTBITS; i++) {
		unsigned int descx;
		bc->modem.par96.descram = (bc->modem.par96.descram << 1);
		if (inb(LPT_STATUS(bc->iobase)) & PAR96_RXBIT)
			bc->modem.par96.descram |= 1;
		descx = bc->modem.par96.descram ^ 
			(bc->modem.par96.descram >> 1);
		/* now the diff decoded data is inverted in descram */
		outb(PAR97_POWER | PAR96_PTT, LPT_DATA(bc->iobase));
		descx ^= ((descx >> PAR96_DESCRAM_TAPSH1) ^
			  (descx >> PAR96_DESCRAM_TAPSH2));
		data >>= 1;
		if (!(descx & 1))
			data |= 0x8000;
		outb(PAR97_POWER | PAR96_PTT | PAR96_BURST, 
		     LPT_DATA(bc->iobase));
	}
#ifdef BAYCOM_USE_BH
	bc->hdlc_rx.shreg2 = bc->hdlc_rx.shreg1;
	bc->hdlc_rx.shreg1 = data | 0x10000;
	queue_task_irq_off(&bc->tq_receiver, &tq_baycom);
	mark_bh(BAYCOM_BH);
#else /* BAYCOM_USE_BH */
	hdlc_rx_word(bc, data);
#endif /* BAYCOM_USE_BH */
	/*
	 * do DCD algorithm
	 */
	if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
		bc->modem.par96.dcd_shreg = (bc->modem.par96.dcd_shreg >> 16)
			| (data << 16);
		/* search for flags and set the dcd counter appropriately */
		for(mask = 0x1fe00, mask2 = 0xfc00, i = 0; 
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if ((bc->modem.par96.dcd_shreg & mask) == mask2)
				bc->modem.par96.dcd_count = BAYCOM_MAXFLEN+4;
		/* check for abort/noise sequences */
		for(mask = 0x1fe00, mask2 = 0x1fe00, i = 0; 
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if ((bc->modem.par96.dcd_shreg & mask) == mask2)
				if (bc->modem.par96.dcd_count >= 0)
					bc->modem.par96.dcd_count -= 
						BAYCOM_MAXFLEN-10;
		/* decrement and set the dcd variable */
		if (bc->modem.par96.dcd_count >= 0)
			bc->modem.par96.dcd_count -= 2;
		bc->modem.dcd = bc->modem.par96.dcd_count > 0;
	} else {
		bc->modem.dcd = !!(inb(LPT_STATUS(bc->iobase)) & PAR96_DCD);
	}
	if (--bc->modem.arb_divider <= 0) {
#ifdef BAYCOM_USE_BH
		queue_task_irq_off(&bc->tq_arbitrate, &tq_baycom);
		mark_bh(BAYCOM_BH);
#else /* BAYCOM_USE_BH */
		tx_arbitrate(bc);
#endif /* BAYCOM_USE_BH */
		bc->modem.arb_divider = bc->ch_params.slottime * 6;
	}
}

/* --------------------------------------------------------------------- */

static int par96_check_lpt(unsigned int iobase)
{
	unsigned char b1,b2;
	int i;

	b1 = inb(LPT_DATA(iobase));
	b2 = inb(LPT_CONTROL(iobase));
	outb(0xaa, LPT_DATA(iobase));
	i = inb(LPT_DATA(iobase)) == 0xaa;
	outb(0x55, LPT_DATA(iobase));
	i &= inb(LPT_DATA(iobase)) == 0x55;
	outb(0x0a, LPT_CONTROL(iobase));
	i &= (inb(LPT_CONTROL(iobase)) & 0xf) == 0x0a;
	outb(0x05, LPT_CONTROL(iobase));
	i &= (inb(LPT_CONTROL(iobase)) & 0xf) == 0x05;
	outb(b1, LPT_DATA(iobase));
	outb(b2, LPT_CONTROL(iobase));
	return !i;
}

/* --------------------------------------------------------------------- */

static int par96_allocate_resources(unsigned int iobase, unsigned int irq,
				    unsigned int options)
{
	if (!iobase || iobase > 0xfff || irq < 2 || irq > 15)
		return -ENXIO;
	if (check_region(iobase, PAR96_EXTENT))
		return -EACCES;
	if (par96_check_lpt(iobase))
		return -EIO;
	request_region(iobase, PAR96_EXTENT, "baycom_par96");
	outb(0, LPT_CONTROL(iobase));                 /* disable interrupt */
	outb(PAR96_PTT | PAR97_POWER, LPT_DATA(iobase)); /* switch off PTT */
	printk(KERN_INFO "baycom: par96 at iobase 0x%x irq %u options 0x%x\n", 
	       iobase, irq, options);
	return 0;
}
	
/* --------------------------------------------------------------------- */

static void par96_deallocate_resources(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_PAR96)
		return;
	outb(0, LPT_CONTROL(bc->iobase));      /* disable interrupt */
	outb(PAR96_PTT, LPT_DATA(bc->iobase)); /* switch off PTT */
	/* 
	 * this should prevent kernel: Trying to free IRQx
	 * messages
	 */
	if (bc->opened > 0)
		free_irq(bc->irq, bc);
	release_region(bc->iobase, PAR96_EXTENT);
	bc->modem_type = BAYCOM_MODEM_INVALID;
	printk(KERN_INFO "baycom: release par96 at iobase 0x%x irq %u\n",
	       bc->iobase, bc->irq);
	bc->iobase = bc->irq = bc->options = 0;
}

/* --------------------------------------------------------------------- */

static int par96_on_open(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_PAR96)
		return -ENXIO;
	outb(0, LPT_CONTROL(bc->iobase));      /* disable interrupt */
	 /* switch off PTT */
	outb(PAR96_PTT | PAR97_POWER, LPT_DATA(bc->iobase));
	if (request_irq(bc->irq, baycom_par96_interrupt, 0, 
			"baycom_par96", bc))
		return -EBUSY;
	outb(LPT_IRQ_ENABLE, LPT_CONTROL(bc->iobase));  /* enable interrupt */
	return 0;
}

/* --------------------------------------------------------------------- */

static void par96_on_close(struct baycom_state *bc) 
{
	if (!bc || bc->modem_type != BAYCOM_MODEM_PAR96)
		return;
	outb(0, LPT_CONTROL(bc->iobase));  /* disable interrupt */
	/* switch off PTT */
	outb(PAR96_PTT | PAR97_POWER, LPT_DATA(bc->iobase));
	free_irq(bc->irq, bc);	
}

/* --------------------------------------------------------------------- */
/*
 * ===================== Bottom half (soft interrupt) ====================
 */

#ifdef BAYCOM_USE_BH
static void bh_receiver(void *private)
{
	struct baycom_state *bc = (struct baycom_state *)private;
	unsigned int temp;

	if (!bc || bc->magic != BAYCOM_MAGIC)
		return;
	if (!bc->hdlc_rx.shreg2)
		return;
	temp = bc->hdlc_rx.shreg2;
	bc->hdlc_rx.shreg2 = 0;
	hdlc_rx_word(bc, temp);
}

/* --------------------------------------------------------------------- */

static void bh_transmitter(void *private)
{
	struct baycom_state *bc = (struct baycom_state *)private;

	if (!bc || bc->magic != BAYCOM_MAGIC)
		return;
	if (bc->hdlc_tx.shreg2)
		return;
	bc->hdlc_tx.shreg2 = hdlc_tx_word(bc) | 0x10000;
}

/* --------------------------------------------------------------------- */

static void bh_arbitrate(void *private)
{
	struct baycom_state *bc = (struct baycom_state *)private;

	if (!bc || bc->magic != BAYCOM_MAGIC)
		return;
	tx_arbitrate(bc);
}

/* --------------------------------------------------------------------- */

static void baycom_bottom_half(void)
{
	run_task_queue(&tq_baycom);
}
#endif /* BAYCOM_USE_BH */

/* --------------------------------------------------------------------- */
/*
 * ===================== TTY interface routines ==========================
 */

static inline int baycom_paranoia_check(struct baycom_state *bc, 
					const char *routine)
{
	if (!bc || bc->magic != BAYCOM_MAGIC) {
		printk(KERN_ERR "baycom: bad magic number for baycom struct "
		       "in routine %s\n", routine);
		return 1;
	}
	return 0;
}

/* --------------------------------------------------------------------- */
/*
 * Here the tty driver code starts
 */

static void baycom_put_fend(struct baycom_state *bc)
{
	if (bc->kiss_decode.wr <= 0 ||
	    (bc->kiss_decode.pkt_buf[0] & 0xf0) != 0)
		return;

	switch (bc->kiss_decode.pkt_buf[0] & 0xf) {
	case KISS_CMD_DATA:
		if (bc->kiss_decode.wr <= 8) 
			break;
		if (!store_packet(&bc->tx_buf, bc->kiss_decode.pkt_buf+1, 0, 
				  bc->kiss_decode.wr-1))
			bc->stat.tx_bufferoverrun++;
		break;

	case KISS_CMD_TXDELAY:
		if (bc->kiss_decode.wr < 2) 
			break;
		bc->ch_params.tx_delay = bc->kiss_decode.pkt_buf[1];
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: TX delay = %ums\n", 
		       bc->ch_params.tx_delay * 10);
#endif /* KISS_VERBOSE */
		break;

	case KISS_CMD_PPERSIST:
		if (bc->kiss_decode.wr < 2) 
			break;
		bc->ch_params.ppersist = bc->kiss_decode.pkt_buf[1];
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: p-persistence = %u\n", 
		       bc->ch_params.ppersist);
#endif /* KISS_VERBOSE */
		break;

	case KISS_CMD_SLOTTIME:
		if (bc->kiss_decode.wr < 2) 
			break;
		bc->ch_params.slottime = bc->kiss_decode.pkt_buf[1];
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: slottime = %ums\n", 
		       bc->ch_params.slottime * 10);
#endif /* KISS_VERBOSE */
		break;

	case KISS_CMD_TXTAIL:
		if (bc->kiss_decode.wr < 2) 
			break;
		bc->ch_params.tx_tail = bc->kiss_decode.pkt_buf[1];
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: TX tail = %ums\n",
		       bc->ch_params.tx_tail * 10);
#endif /* KISS_VERBOSE */
		break;

	case KISS_CMD_FULLDUP:
		if (bc->kiss_decode.wr < 2) 
			break;
		bc->ch_params.fulldup = bc->kiss_decode.pkt_buf[1];
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: %s duplex\n", 
		       bc->ch_params.fulldup ? "full" : "half");
#endif /* KISS_VERBOSE */
		break;

	default:
#ifdef KISS_VERBOSE
		printk(KERN_INFO "baycom: unhandled KISS packet code %u\n",
		       bc->kiss_decode.pkt_buf[0] & 0xf);
#endif /* KISS_VERBOSE */
		break;
	}
}

/* --------------------------------------------------------------------- */

static void baycom_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct baycom_state *bc;
		
	if (!tty)
		return;
	if (baycom_paranoia_check(bc = tty->driver_data, "put_char"))
		return;
		
	if (ch == KISS_FEND) {
		baycom_put_fend(bc);
		bc->kiss_decode.wr = 0;
		bc->kiss_decode.escaped = 0;
		bc->kiss_decode.dec_state = 1;
		return;
	}
	if (!bc->kiss_decode.dec_state)
		return;
	if (bc->kiss_decode.wr >= sizeof(bc->kiss_decode.pkt_buf)) {
		bc->kiss_decode.wr = 0;
		bc->kiss_decode.dec_state = 0;
		return;
	}
	if (bc->kiss_decode.escaped) {
		if (ch == KISS_TFEND)
			bc->kiss_decode.pkt_buf[bc->kiss_decode.wr++] = 
				KISS_FEND;
		else if (ch == KISS_TFESC)
			bc->kiss_decode.pkt_buf[bc->kiss_decode.wr++] = 
				KISS_FESC;
		else {
			bc->kiss_decode.wr = 0;
			bc->kiss_decode.dec_state = 0;
		}
		bc->kiss_decode.escaped = 0;
		return;
	}
	bc->kiss_decode.pkt_buf[bc->kiss_decode.wr++] = ch;
}
	
/* --------------------------------------------------------------------- */

static int baycom_write(struct tty_struct * tty, int from_user,
	const unsigned char *buf, int count)
{
	int c;
	const unsigned char *bp;
	struct baycom_state *bc;
		
	if (!tty || !buf || count <= 0)
		return count;
	
	if (baycom_paranoia_check(bc = tty->driver_data, "write"))
		return count; 
		
	if (from_user) {
		for(c = count, bp = buf; c > 0; c--,bp++)
			baycom_put_char(tty, get_user(bp));
	} else {
		for(c = count, bp = buf; c > 0; c--,bp++)
			baycom_put_char(tty, *bp);
	}
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
	return count;
}

/* --------------------------------------------------------------------- */

static int baycom_write_room(struct tty_struct *tty)
{
	int free;
	struct baycom_state *bc;
		
	if (!tty)
		return 0;
	if (baycom_paranoia_check(bc = tty->driver_data, "write_room"))
		return 0;
		
	free = bc->tx_buf.rd - bc->tx_buf.wr;
	if (free <= 0) {
		free = bc->tx_buf.buflen - bc->tx_buf.wr;
		if (free < bc->tx_buf.rd)
			free = bc->tx_buf.rd;	/* we may fold */
	}

	return free / 2; /* a rather pessimistic estimate */
}

/* --------------------------------------------------------------------- */

static int baycom_chars_in_buffer(struct tty_struct *tty)
{
	int cnt;
	struct baycom_state *bc;
		
	if (!tty)
		return 0;
	if (baycom_paranoia_check(bc = tty->driver_data, "chars_in_buffer"))
		return 0;

	cnt = bc->tx_buf.wr - bc->tx_buf.rd;
	if (cnt < 0)
		cnt += bc->tx_buf.buflen;
		
	return cnt;
}

/* --------------------------------------------------------------------- */

static void baycom_flush_buffer(struct tty_struct *tty)
{
	struct baycom_state *bc;
		
	if (!tty)
		return;
	if (baycom_paranoia_check(bc = tty->driver_data, "flush_buffer"))
		return;

	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
}

/* --------------------------------------------------------------------- */

static inline void baycom_dealloc_hw(struct baycom_state *bc) 
{
	if (!bc || bc->magic != BAYCOM_MAGIC || 
	    bc->modem_type == BAYCOM_MODEM_INVALID)
		return;
	switch(bc->modem_type) {
	case BAYCOM_MODEM_SER12:
		ser12_deallocate_resources(bc);
		break;
	case BAYCOM_MODEM_PAR96:
		par96_deallocate_resources(bc);
		break;
	}
}

/* --------------------------------------------------------------------- */

static int baycom_set_hardware(struct baycom_state *bc,
			       unsigned int modem_type, unsigned int iobase, 
			       unsigned int irq, unsigned int options)
{
	int i;

	if (!bc)
		return -EINVAL;

	if (modem_type == BAYCOM_MODEM_SER12) {
		i = ser12_allocate_resources(iobase, irq, options);
		if (i < 0)
			return i;
	} else if (modem_type == BAYCOM_MODEM_PAR96) {
		i = par96_allocate_resources(iobase, irq, options);
		if (i < 0)
			return i;
	} else if (modem_type == BAYCOM_MODEM_INVALID) {
		iobase = irq = options = 0;
	} else {
		return -ENXIO;
	}
	baycom_dealloc_hw(bc);
	bc->modem_type = modem_type;
	bc->iobase = iobase;
	bc->irq = irq;
	bc->options = options;
	i = 0;
	if (bc->opened > 0) {
		switch(bc->modem_type) {
		case BAYCOM_MODEM_SER12:
			i = ser12_on_open(bc);
			break;
		case BAYCOM_MODEM_PAR96:
			i = par96_on_open(bc);
			break;
		}
	}
	return i;
}

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct tty_struct *tty, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	int i;
	struct baycom_state *bc;
	struct baycom_params par;
		
	if (!tty)
		return -EINVAL;
	if (baycom_paranoia_check(bc = tty->driver_data, "ioctl"))
		return -EINVAL;
		
	switch (cmd) {
	default:
		return -ENOIOCTLCMD;

	case TIOCMGET:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (i)
			return i;
		i = (bc->modem.dcd ? TIOCM_CAR : 0) |
			(bc->hdlc_tx.ptt ? TIOCM_RTS : 0);
		put_user(i, (int *) arg);
		return 0;
		
	case BAYCOMCTL_GETDCD:
		i = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned char));
		if (!i)
			put_user(bc->modem.dcd, (unsigned char *) arg);
		return i;
		
	case BAYCOMCTL_GETPTT:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(unsigned char));
		if (!i)
			put_user(bc->hdlc_tx.ptt, (unsigned char *) arg);
		return i;
		
	case BAYCOMCTL_PARAM_TXDELAY:
		if (arg > 255)
			return -EINVAL;
		bc->ch_params.tx_delay = arg;
		return 0;
		
	case BAYCOMCTL_PARAM_PPERSIST:
		if (arg > 255)
			return -EINVAL;
		bc->ch_params.ppersist = arg;
		return 0;
		
	case BAYCOMCTL_PARAM_SLOTTIME:
		if (arg > 255)
			return -EINVAL;
		bc->ch_params.slottime = arg;
		return 0;
		
	case BAYCOMCTL_PARAM_TXTAIL:
		if (arg > 255)
			return -EINVAL;
		bc->ch_params.tx_tail = arg;
		return 0;
		
	case BAYCOMCTL_PARAM_FULLDUP:
		bc->ch_params.fulldup = arg ? 1 : 0;
		return 0;
		
	case BAYCOMCTL_CALIBRATE:
		bc->calibrate = arg * ((bc->modem_type == BAYCOM_MODEM_PAR96) ?
				       600 : 75);
		return 0;

	case BAYCOMCTL_GETPARAMS:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(par));
		if (i)
			return i;
		par.modem_type = bc->modem_type;
		par.iobase = bc->iobase;
		par.irq = bc->irq;
		par.options = bc->options;
		par.tx_delay = bc->ch_params.tx_delay;
		par.tx_tail = bc->ch_params.tx_tail;
		par.slottime = bc->ch_params.slottime;
		par.ppersist = bc->ch_params.ppersist;
		par.fulldup = bc->ch_params.fulldup;
		memcpy_tofs((void *)arg, &par, sizeof(par));
		return 0;

	case BAYCOMCTL_SETPARAMS:
		if (!suser())
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *) arg, 
				sizeof(par));
		if (i)
			return i;
		memcpy_fromfs(&par, (void *)arg, sizeof(par));
		printk(KERN_INFO "baycom: changing hardware type: modem %u "
		       "iobase 0x%x irq %u options 0x%x\n", par.modem_type,
		       par.iobase, par.irq, par.options);
		i = baycom_set_hardware(bc, par.modem_type, par.iobase,
					par.irq, par.options); 
		if (i)
			return i;
		bc->ch_params.tx_delay = par.tx_delay;
		bc->ch_params.tx_tail = par.tx_tail;
		bc->ch_params.slottime = par.slottime;
		bc->ch_params.ppersist = par.ppersist;
		bc->ch_params.fulldup = par.fulldup;
		return 0;

	case BAYCOMCTL_GETSTAT:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(struct baycom_statistics));
		if (i)
			return i;
		memcpy_tofs((void *)arg, &bc->stat, 
			    sizeof(struct baycom_statistics));
		return 0;
		

#ifdef BAYCOM_DEBUG
	case BAYCOMCTL_GETSAMPLES:
		if (bc->bitbuf_channel.rd == bc->bitbuf_channel.wr) 
			return -EAGAIN;
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(unsigned char));
		if (!i) {
			put_user(bc->bitbuf_channel.buffer
				 [bc->bitbuf_channel.rd],
				 (unsigned char *) arg);
			bc->bitbuf_channel.rd = (bc->bitbuf_channel.rd+1) %
				sizeof(bc->bitbuf_channel.buffer);
		}
		return i;
		
	case BAYCOMCTL_GETBITS:
		if (bc->bitbuf_hdlc.rd == bc->bitbuf_hdlc.wr) 
			return -EAGAIN;
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(unsigned char));
		if (!i) {
			put_user(bc->bitbuf_hdlc.buffer[bc->bitbuf_hdlc.rd],
				 (unsigned char *) arg);
			bc->bitbuf_hdlc.rd = (bc->bitbuf_hdlc.rd+1) %
				sizeof(bc->bitbuf_hdlc.buffer);
		}
		return i;
		
	case BAYCOMCTL_DEBUG1:
		i = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned long));
		if (!i)
			put_user((bc->rx_buf.wr-bc->rx_buf.rd) % 
				 bc->rx_buf.buflen, (unsigned long *)arg);
		return i;
		
	case BAYCOMCTL_DEBUG2:
		i = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned long));
		if (!i)
			put_user(bc->debug_vals.last_intcnt, 
				 (unsigned long *)arg);
		return i;
		
	case BAYCOMCTL_DEBUG3:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(unsigned long));
		if (!i)
			put_user((long)bc->debug_vals.last_pllcorr,
				 (long *)arg);
		return i;		
#endif /* BAYCOM_DEBUG */
	}
}

/* --------------------------------------------------------------------- */

int baycom_open(struct tty_struct *tty, struct file * filp)
{
	int line;
	struct baycom_state *bc;
	int i;

	if(!tty)
		return -ENODEV;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if (line < 0 || line >= NR_PORTS)
		return -ENODEV;
	bc = baycom_state+line;

	if (bc->opened > 0) {
		bc->opened++;
		MOD_INC_USE_COUNT;
		return 0;
	}
	/*
	 * initialise some variables
	 */
	bc->calibrate = 0;

	/*
	 * allocate the buffer space
	 */
	if (bc->rx_buf.buffer)
		kfree_s(bc->rx_buf.buffer, bc->rx_buf.buflen);
	if (bc->tx_buf.buffer)
		kfree_s(bc->tx_buf.buffer, bc->tx_buf.buflen);
	bc->rx_buf.buflen = BUFLEN_RX;
	bc->tx_buf.buflen = BUFLEN_TX;
	bc->rx_buf.rd = bc->rx_buf.wr = 0;
	bc->tx_buf.rd = bc->tx_buf.wr = 0;
	bc->rx_buf.buffer = kmalloc(bc->rx_buf.buflen, GFP_KERNEL);
	bc->tx_buf.buffer = kmalloc(bc->tx_buf.buflen, GFP_KERNEL);
	if (!bc->rx_buf.buffer || !bc->tx_buf.buffer) {
		if (bc->rx_buf.buffer)
			kfree_s(bc->rx_buf.buffer, bc->rx_buf.buflen);
		if (bc->tx_buf.buffer)
			kfree_s(bc->tx_buf.buffer, bc->tx_buf.buflen);
		bc->rx_buf.buffer = bc->tx_buf.buffer = NULL;
		bc->rx_buf.buflen = bc->tx_buf.buflen = 0;
		return -ENOMEM;
	}
	/*
	 * check if the modem type has been set
	 */
	switch(bc->modem_type) {
	case BAYCOM_MODEM_SER12:
		i = ser12_on_open(bc);
		break;
	case BAYCOM_MODEM_PAR96:
		i = par96_on_open(bc);
		break;
	case BAYCOM_MODEM_INVALID:
		/*
		 * may open even if no hardware specified, in order to
		 * subsequently allow the BAYCOMCTL_SETPARAMS ioctl
		 */
		i = 0;
		break;
	default:
		return -ENODEV;
	}
	if (i) 
		return i;

	bc->opened++;
	MOD_INC_USE_COUNT;

	tty->driver_data = bc;
	bc->tty = tty;

	return 0;   
}


/* --------------------------------------------------------------------- */
	
static void baycom_close(struct tty_struct *tty, struct file * filp)
{
	struct baycom_state *bc;
		
	if(!tty) return;
	if (baycom_paranoia_check(bc = tty->driver_data, "close"))
		return;

	MOD_DEC_USE_COUNT;
	bc->opened--;
	if (bc->opened <= 0) {
		switch(bc->modem_type) {
		case BAYCOM_MODEM_SER12:
			ser12_on_close(bc);
			break;
		case BAYCOM_MODEM_PAR96:
			par96_on_close(bc);
			break;
		}
		tty->driver_data = NULL;
		bc->tty = NULL;
		bc->opened = 0;
		/*
		 * free the buffers 
		 */
		bc->rx_buf.rd = bc->rx_buf.wr = 0;
		bc->tx_buf.rd = bc->tx_buf.wr = 0;
		if (bc->rx_buf.buffer)
			kfree_s(bc->rx_buf.buffer, bc->rx_buf.buflen);
		if (bc->tx_buf.buffer)
			kfree_s(bc->tx_buf.buffer, bc->tx_buf.buflen);
		bc->rx_buf.buffer = bc->tx_buf.buffer = NULL;
		bc->rx_buf.buflen = bc->tx_buf.buflen = 0;
	}
}

/* --------------------------------------------------------------------- */
/*
 * And now the modules code and kernel interface.
 */

static void init_channel(struct baycom_state *bc)
{
	struct access_params dflt_ch_params = { 20, 2, 10, 40, 0 };

	if (!bc)
		return;

	bc->hdlc_rx.rx_state = 0;

	bc->hdlc_tx.tx_state = bc->hdlc_tx.numflags = 0;
	bc->hdlc_tx.bitstream = 0;
	bc->hdlc_tx.current_byte = bc->hdlc_tx.ptt = 0;

	memset(&bc->modem, 0, sizeof(bc->modem));

#ifdef BAYCOM_DEBUG
	bc->bitbuf_channel.rd = bc->bitbuf_channel.wr = 0;
	bc->bitbuf_channel.shreg = 0x80;

	bc->bitbuf_hdlc.rd = bc->bitbuf_hdlc.wr = 0;
	bc->bitbuf_hdlc.shreg = 0x80;
#endif /* BAYCOM_DEBUG */

	bc->kiss_decode.dec_state = bc->kiss_decode.escaped = 
	bc->kiss_decode.wr = 0;

	bc->ch_params = dflt_ch_params;

#ifdef BAYCOM_USE_BH
	bc->tq_receiver.next = bc->tq_transmitter.next =
		bc->tq_arbitrate.next = NULL;
	bc->tq_receiver.sync = bc->tq_transmitter.sync =
		bc->tq_arbitrate.sync = 0;
	bc->tq_receiver.data = bc->tq_transmitter.data =
		bc->tq_arbitrate.data = bc;
	bc->tq_receiver.routine = bh_receiver;
	bc->tq_transmitter.routine = bh_transmitter;
	bc->tq_arbitrate.routine = bh_arbitrate;
#endif /* BAYCOM_USE_BH */
}

static void init_datastructs(void)
{
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		struct baycom_state *bc = baycom_state+i;

		bc->magic = BAYCOM_MAGIC;
		bc->modem_type = BAYCOM_MODEM_INVALID;
		bc->iobase = bc->irq = bc->options = bc->opened = 0;
		bc->tty = NULL;

		bc->rx_buf.rd = bc->rx_buf.wr = 0;
		bc->rx_buf.buflen = 0;
		bc->rx_buf.buffer = NULL;

		bc->tx_buf.rd = bc->tx_buf.wr = 0;
		bc->tx_buf.buflen = 0;
		bc->tx_buf.buffer = NULL;

		memset(&bc->stat, 0, sizeof(bc->stat));

		init_channel(bc);
	}
}

int baycom_init(void) {
	int i, j;

	/*
	 * initialize the data structures
	 */
	init_datastructs();
	/*
	 * initialize bottom half handler
 	 */
#ifdef BAYCOM_USE_BH
	init_bh(BAYCOM_BH, baycom_bottom_half);
#endif /* BAYCOM_USE_BH */
	/*
	 * register the driver as tty driver
	 */
	memset(&baycom_driver, 0, sizeof(struct tty_driver));
	baycom_driver.magic = TTY_DRIVER_MAGIC;
	baycom_driver.name = "baycom";
	baycom_driver.major = major;
	baycom_driver.minor_start = 0;
	baycom_driver.num = NR_PORTS;
	baycom_driver.type = TTY_DRIVER_TYPE_BAYCOM;
	baycom_driver.subtype = BAYCOM_TYPE_NORMAL;
	baycom_driver.init_termios.c_iflag = 0;
	baycom_driver.init_termios.c_oflag = 0;
	baycom_driver.init_termios.c_cflag = CS8 | B1200 | CREAD | CLOCAL;
	baycom_driver.init_termios.c_lflag = 0;
	baycom_driver.flags = TTY_DRIVER_REAL_RAW;
	baycom_driver.refcount = &baycom_refcount;
	baycom_driver.table = baycom_table;
	baycom_driver.termios = baycom_termios;
	baycom_driver.termios_locked = baycom_termios_locked;
	/*
	 * the functions
	 */
	baycom_driver.open = baycom_open;
	baycom_driver.close = baycom_close;
	baycom_driver.write = baycom_write;
	baycom_driver.put_char = baycom_put_char;
	baycom_driver.flush_chars = NULL;
	baycom_driver.write_room = baycom_write_room;
	baycom_driver.chars_in_buffer = baycom_chars_in_buffer;
	baycom_driver.flush_buffer = baycom_flush_buffer;
	baycom_driver.ioctl = baycom_ioctl;
	/*
	 * cannot throttle the transmitter on this layer
	 */
	baycom_driver.throttle = NULL;
	baycom_driver.unthrottle = NULL;
	/*
	 * no special actions on termio changes
	 */
	baycom_driver.set_termios = NULL;
	/*
	 * no XON/XOFF and no hangup on the radio port
	 */
	baycom_driver.stop = NULL;
	baycom_driver.start = NULL;
	baycom_driver.hangup = NULL;
	baycom_driver.set_ldisc = NULL;

	if (tty_register_driver(&baycom_driver)) {
		printk(KERN_WARNING "baycom: tty_register_driver failed\n");
		return -EIO;
	}

	for (i = 0; i < NR_PORTS && 
	     baycom_ports[i].modem != BAYCOM_MODEM_INVALID; i++) {
		j = baycom_set_hardware(baycom_state+i, 
					baycom_ports[i].modem,
					baycom_ports[i].iobase, 
					baycom_ports[i].irq, 
					baycom_ports[i].options);
		if (j < 0) {
			const char *s;
			switch (-j) {
			case ENXIO:
				s = "invalid iobase and/or irq";
				break;
			case EACCES:
				s = "io region already used";
				break;
			case EIO:
				s = "no uart/lpt port at iobase";
				break;
			case EBUSY:
				s = "interface already in use";
				break;
			case EINVAL:
				s = "internal error";
				break;
			default:
				s = "unknown error";
				break;
			}
			printk(KERN_WARNING "baycom: modem %u iobase 0x%x "
			       "irq %u: (%i) %s\n", baycom_ports[i].modem, 
			       baycom_ports[i].iobase, baycom_ports[i].irq, 
			       j, s);
		}
	}

	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

int modem = BAYCOM_MODEM_INVALID;
int iobase = 0x3f8;
int irq = 4;
int options = BAYCOM_OPTIONS_SOFTDCD;

int init_module(void)
{
	int i;

	printk(KERN_INFO "baycom: init_module called\n");

	baycom_ports[0].modem = modem;
	baycom_ports[0].iobase = iobase;
	baycom_ports[0].irq = irq;
	baycom_ports[0].options = options;
	baycom_ports[1].modem = BAYCOM_MODEM_INVALID;

	i = baycom_init();
	if (i)
		return i;

	printk(KERN_INFO "baycom: version 0.3; "
	       "(C) 1996 by Thomas Sailer HB9JNX, sailer@ife.ee.ethz.ch\n");

	return 0;
}

/* --------------------------------------------------------------------- */

void cleanup_module(void)
{
	int i;

	printk(KERN_INFO "baycom: cleanup_module called\n");

	if (tty_unregister_driver(&baycom_driver))
		printk(KERN_WARNING "baycom: failed to unregister tty "
		       "driver\n");
	for(i = 0; i < NR_PORTS; i++) {
		struct baycom_state *bc = baycom_state+i;

		if (bc->magic != BAYCOM_MAGIC)
			printk(KERN_ERR "baycom: invalid magic in "
			       "cleanup_module\n");
		else {
			baycom_dealloc_hw(bc);
			/*
			 * free the buffers 
			 */
			bc->rx_buf.rd = bc->rx_buf.wr = 0;
			bc->tx_buf.rd = bc->tx_buf.wr = 0;
			if (bc->rx_buf.buffer)
				kfree_s(bc->rx_buf.buffer, bc->rx_buf.buflen);
			if (bc->tx_buf.buffer)
				kfree_s(bc->tx_buf.buffer, bc->tx_buf.buflen);
			bc->rx_buf.buffer = bc->tx_buf.buffer = NULL;
			bc->rx_buf.buflen = bc->tx_buf.buflen = 0;
		}
	}
}

#else /* MODULE */
/* --------------------------------------------------------------------- */
/*
 * format: baycom=modem,io,irq,options[,modem,io,irq,options]
 * modem=1: ser12, modem=2: par96
 * options=0: hardware DCD, options=1: software DCD
 */

void baycom_setup(char *str, int *ints)
{
	int i;

	for (i = 0; i < NR_PORTS; i++) 
		if (ints[0] >= 4*i+4) {
			baycom_ports[i].modem = ints[4*i+1];
			baycom_ports[i].iobase = ints[4*i+2];
			baycom_ports[i].irq = ints[4*i+3];
			baycom_ports[i].options = ints[4*i+4];
		} else
			baycom_ports[i].modem = BAYCOM_MODEM_INVALID;

}

#endif /* MODULE */
/* --------------------------------------------------------------------- */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
