/*
 * hdlcdrv.h  -- HDLC packet radio network driver.
 * The Linux soundcard driver for 1200 baud and 9600 baud packet radio
 * (C) 1996 by Thomas Sailer, HB9JNX/AE4WA
 */

#ifndef _HDLCDRV_H
#define _HDLCDRV_H

#include <linux/version.h>
#include <linux/sockios.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < 0x20119
#include <linux/if_ether.h>
#endif
#include <linux/netdevice.h>

/* -------------------------------------------------------------------- */
/*
 * structs for the IOCTL commands
 */

struct hdlcdrv_params {
	int iobase;
	int irq;
	int dma;
	int dma2;
	int seriobase;
	int pariobase;
	int midiiobase;
};	

struct hdlcdrv_channel_params {
	int tx_delay;  /* the transmitter keyup delay in 10ms units */
	int tx_tail;   /* the transmitter keyoff delay in 10ms units */
	int slottime;  /* the slottime in 10ms; usually 10 = 100ms */
	int ppersist;  /* the p-persistence 0..255 */
	int fulldup;   /* some driver do not support full duplex, setting */
	               /* this just makes them send even if DCD is on */
};	

struct hdlcdrv_old_channel_state {
  	int ptt;
  	int dcd;
  	int ptt_keyed;
#if LINUX_VERSION_CODE < 0x20100
  	struct enet_statistics stats;
#endif
};

struct hdlcdrv_channel_state {
 	int ptt;
 	int dcd;
 	int ptt_keyed;
 	unsigned long tx_packets;
 	unsigned long tx_errors;
 	unsigned long rx_packets;
 	unsigned long rx_errors;
};

struct hdlcdrv_ioctl {
	int cmd;
	union {
		struct hdlcdrv_params mp;
		struct hdlcdrv_channel_params cp;
		struct hdlcdrv_channel_state cs;
		struct hdlcdrv_old_channel_state ocs;
		unsigned int calibrate;
		unsigned char bits;
		char modename[128];
		char drivername[32];
	} data;
};

/* -------------------------------------------------------------------- */

/*
 * ioctl values
 */
#define HDLCDRVCTL_GETMODEMPAR       0
#define HDLCDRVCTL_SETMODEMPAR       1
#define HDLCDRVCTL_MODEMPARMASK      2  /* not handled by hdlcdrv */
#define HDLCDRVCTL_GETCHANNELPAR    10
#define HDLCDRVCTL_SETCHANNELPAR    11
#define HDLCDRVCTL_OLDGETSTAT       20
#define HDLCDRVCTL_CALIBRATE        21
#define HDLCDRVCTL_GETSTAT          22

/*
 * these are mainly for debugging purposes
 */
#define HDLCDRVCTL_GETSAMPLES       30
#define HDLCDRVCTL_GETBITS          31

/*
 * not handled by hdlcdrv, but by its depending drivers
 */
#define HDLCDRVCTL_GETMODE          40
#define HDLCDRVCTL_SETMODE          41
#define HDLCDRVCTL_MODELIST         42
#define HDLCDRVCTL_DRIVERNAME       43

/*
 * mask of needed modem parameters, returned by HDLCDRVCTL_MODEMPARMASK
 */
#define HDLCDRV_PARMASK_IOBASE      (1<<0)
#define HDLCDRV_PARMASK_IRQ         (1<<1)
#define HDLCDRV_PARMASK_DMA         (1<<2)
#define HDLCDRV_PARMASK_DMA2        (1<<3)
#define HDLCDRV_PARMASK_SERIOBASE   (1<<4)
#define HDLCDRV_PARMASK_PARIOBASE   (1<<5)
#define HDLCDRV_PARMASK_MIDIIOBASE  (1<<6)

/* -------------------------------------------------------------------- */

#ifdef __KERNEL__

#define HDLCDRV_MAGIC      0x5ac6e778
#define HDLCDRV_IFNAMELEN    6
#define HDLCDRV_HDLCBUFFER  32 /* should be a power of 2 for speed reasons */
#define HDLCDRV_BITBUFFER  256 /* should be a power of 2 for speed reasons */
#undef HDLCDRV_LOOPBACK  /* define for HDLC debugging purposes */
#define HDLCDRV_DEBUG

/* maximum packet length, excluding CRC */
#define HDLCDRV_MAXFLEN             400	


struct hdlcdrv_hdlcbuffer {
	unsigned rd, wr;
	unsigned short buf[HDLCDRV_HDLCBUFFER];
};

#ifdef HDLCDRV_DEBUG
struct hdlcdrv_bitbuffer {
	unsigned int rd;
	unsigned int wr;
	unsigned int shreg;
	unsigned char buffer[HDLCDRV_BITBUFFER];
};

extern inline void hdlcdrv_add_bitbuffer(struct hdlcdrv_bitbuffer *buf, 
					 unsigned int bit)
{
	unsigned char new;

	new = buf->shreg & 1;
	buf->shreg >>= 1;
	buf->shreg |= (!!bit) << 7;
	if (new) {
		buf->buffer[buf->wr] = buf->shreg;
		buf->wr = (buf->wr+1) % sizeof(buf->buffer);
		buf->shreg = 0x80;
	}
}

extern inline void hdlcdrv_add_bitbuffer_word(struct hdlcdrv_bitbuffer *buf, 
					      unsigned int bits)
{
	buf->buffer[buf->wr] = bits & 0xff;
	buf->wr = (buf->wr+1) % sizeof(buf->buffer);
	buf->buffer[buf->wr] = (bits >> 8) & 0xff;
	buf->wr = (buf->wr+1) % sizeof(buf->buffer);

}
#endif /* HDLCDRV_DEBUG */

/* -------------------------------------------------------------------- */
/*
 * Information that need to be kept for each driver. 
 */

struct hdlcdrv_ops {
	/*
	 * first some informations needed by the hdlcdrv routines
	 */
	const char *drvname;
	const char *drvinfo;
	/*
	 * the routines called by the hdlcdrv routines
	 */
	int (*open)(struct device *);
	int (*close)(struct device *);
	int (*ioctl)(struct device *, struct ifreq *, 
		     struct hdlcdrv_ioctl *, int);
};

struct hdlcdrv_state {
	int magic;

	char ifname[HDLCDRV_IFNAMELEN];

	const struct hdlcdrv_ops *ops;

	struct {
		int bitrate;
	} par;

	struct hdlcdrv_pttoutput {
		int dma2;
		int seriobase;
		int pariobase;
		int midiiobase;
		unsigned int flags;
	} ptt_out;

	struct hdlcdrv_channel_params ch_params;

	struct hdlcdrv_hdlcrx {
		struct hdlcdrv_hdlcbuffer hbuf;
		int in_hdlc_rx;
		/* 0 = sync hunt, != 0 receiving */
		int rx_state;	
		unsigned int bitstream;
		unsigned int bitbuf;
		int numbits;
		unsigned char dcd;
		
		int len;
		unsigned char *bp;
		unsigned char buffer[HDLCDRV_MAXFLEN+2];
	} hdlcrx;

	struct hdlcdrv_hdlctx {
		struct hdlcdrv_hdlcbuffer hbuf;
		int in_hdlc_tx;
		/*
		 * 0 = send flags
		 * 1 = send txtail (flags)
		 * 2 = send packet
		 */
		int tx_state;	
		int numflags;
		unsigned int bitstream;
		unsigned char ptt;
		int calibrate;
		int slotcnt;

		unsigned int bitbuf;
		int numbits;
		
		int len;
		unsigned char *bp;
		unsigned char buffer[HDLCDRV_MAXFLEN+2];
	} hdlctx;

#ifdef HDLCDRV_DEBUG
	struct hdlcdrv_bitbuffer bitbuf_channel;
	struct hdlcdrv_bitbuffer bitbuf_hdlc;
#endif /* HDLCDRV_DEBUG */

#if LINUX_VERSION_CODE < 0x20119
	struct enet_statistics stats;
#else
	struct net_device_stats stats;
#endif
	int ptt_keyed;

	struct sk_buff_head send_queue;  /* Packets awaiting transmission */
};


/* -------------------------------------------------------------------- */

extern inline int hdlcdrv_hbuf_full(struct hdlcdrv_hdlcbuffer *hb) 
{
	return !((HDLCDRV_HDLCBUFFER - 1 + hb->rd - hb->wr) 
		 % HDLCDRV_HDLCBUFFER);
}

/* -------------------------------------------------------------------- */

extern inline int hdlcdrv_hbuf_empty(struct hdlcdrv_hdlcbuffer *hb)
{
	return hb->rd == hb->wr;
}

/* -------------------------------------------------------------------- */

extern inline unsigned short hdlcdrv_hbuf_get(struct hdlcdrv_hdlcbuffer *hb)
{
	unsigned newr;
	unsigned short val;
	unsigned long flags;

	if (hb->rd == hb->wr)
		return 0;
	save_flags(flags);
	cli();
	newr = (hb->rd+1) % HDLCDRV_HDLCBUFFER;
	val = hb->buf[hb->rd];
	hb->rd = newr;
	restore_flags(flags);
	return val;
}

/* -------------------------------------------------------------------- */

extern inline void hdlcdrv_hbuf_put(struct hdlcdrv_hdlcbuffer *hb, 
				    unsigned short val)
{
	unsigned newp;
	unsigned long flags;

	save_flags(flags);
	cli();
	newp = (hb->wr+1) % HDLCDRV_HDLCBUFFER;
	if (newp != hb->rd) { 
		hb->buf[hb->wr] = val & 0xffff;
		hb->wr = newp;
	}
	restore_flags(flags);
}

/* -------------------------------------------------------------------- */

extern inline void hdlcdrv_putbits(struct hdlcdrv_state *s, unsigned int bits)
{
	hdlcdrv_hbuf_put(&s->hdlcrx.hbuf, bits);
}

extern inline unsigned int hdlcdrv_getbits(struct hdlcdrv_state *s)
{
	unsigned int ret;

	if (hdlcdrv_hbuf_empty(&s->hdlctx.hbuf)) {
		if (s->hdlctx.calibrate > 0)
			s->hdlctx.calibrate--;
		else
			s->hdlctx.ptt = 0;
		ret = 0;
	} else 
		ret = hdlcdrv_hbuf_get(&s->hdlctx.hbuf);
#ifdef HDLCDRV_LOOPBACK
	hdlcdrv_hbuf_put(&s->hdlcrx.hbuf, ret);
#endif /* HDLCDRV_LOOPBACK */
	return ret;
}

extern inline void hdlcdrv_channelbit(struct hdlcdrv_state *s, unsigned int bit)
{
#ifdef HDLCDRV_DEBUG
	hdlcdrv_add_bitbuffer(&s->bitbuf_channel, bit);
#endif /* HDLCDRV_DEBUG */
}

extern inline void hdlcdrv_setdcd(struct hdlcdrv_state *s, int dcd)
{
	s->hdlcrx.dcd = !!dcd;
}

extern inline int hdlcdrv_ptt(struct hdlcdrv_state *s)
{
	return s->hdlctx.ptt || (s->hdlctx.calibrate > 0);
}

/* -------------------------------------------------------------------- */

void hdlcdrv_receiver(struct device *, struct hdlcdrv_state *);
void hdlcdrv_transmitter(struct device *, struct hdlcdrv_state *);
void hdlcdrv_arbitrate(struct device *, struct hdlcdrv_state *);
int hdlcdrv_register_hdlcdrv(struct device *dev, const struct hdlcdrv_ops *ops,
			     unsigned int privsize, char *ifname,
			     unsigned int baseaddr, unsigned int irq, 
			     unsigned int dma);
int hdlcdrv_unregister_hdlcdrv(struct device *dev);

/* -------------------------------------------------------------------- */



#endif /* __KERNEL__ */

/* -------------------------------------------------------------------- */

#endif /* _HDLCDRV_H */

/* -------------------------------------------------------------------- */
