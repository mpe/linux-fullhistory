/* radiotrack (radioreveal) driver for Linux radio support
 * (c) 1997 M. Kirkwood
 */
/* TODO: Allow for more than one of these foolish entities :-) */

/* Notes on the hardware (reverse engineered from other peoples'
 * reverse engineering of AIMS' code :-)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *
 *  The signal strength query is unsurprisingly inaccurate.  And it seems
 *  to indicate that (on my card, at least) the frequency setting isn't
 *  too great.  (I have to tune up .025MHz from what the freq should be
 *  to get a report that the thing is tuned.)
 *
 *  Volume control is (ugh) analogue:
 *   out(port, start_increasing_volume);
 *   wait(a_wee_while);
 *   out(port, stop_changing_the_volume);
 *  
 */

#include <linux/config.h>
#include <linux/radio.h>
#include <linux/ioport.h>

#include <linux/delay.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "rtrack.h"

/* Let's just be a bit careful here, shall we? */
#if (CONFIG_RADIO_RTRACK_PORT != 0x20f) && (CONFIG_RADIO_RTRACK_PORT != 0x30f)
#error Invalid port specified for RadioTrack
#endif

/* local prototypes */
void outbits(int bits, int data, int port);
void decvol(int port);
void incvol(int port);
void mute(int port);
void unmute(int port);

/* public structurey-type things */
int rt_port = CONFIG_RADIO_RTRACK_PORT;

struct radio_cap rt_cap = {
	0,			/* device index (not dealt with here) */
	RADIO_TYPE_RTRACK,	/* type */
	1,			/* number of bandwidths */
	0, 10			/* volmin, volmax */
};

/* we only get one band/protocol with radiotrack */
struct radio_band rt_band = {
	0,			/* device index */
	0,			/* bandwidth "index" */
	RADIO_PROTOCOL_FM,
	RADIO_BAND_FM_STD,
	RADIO_FM_FRTOINT(88.0),	/* freq range */
	RADIO_FM_FRTOINT(108.0),
	0,1			/* sig strength range */
};

/* p'raps these should become struct radio_ops and struct radio_status? */
struct radio_device rt_dev = {
	&rt_cap,
	&rt_band,
	&rt_setvol,
	0,				/* curvol */
	&rt_setband,
	0,				/* curband */
	&rt_setfreq,
	0,				/* curfreq */
	&rt_getsigstr,
	NULL,				/* next (to be filled in later) */
	&rt_port			/* misc */
};


void radiotrack_init()
{
int dev_num, i;

/* XXX - probe here?? - XXX */

/* try to grab the i/o port */
	if(check_region(rt_port,2)) {
		printk("rtrack.c: port 0x%x already used\n", rt_port);
		return;
	}

	request_region(rt_port,2,"rtrack");

	dev_num = radio_add_device(&rt_dev);
/* initialise the card */
	/* set the volume very low */
	for(i=rt_cap.volmax; i>rt_cap.volmin; i--)
		decvol(rt_port);
	rt_dev.curvol = rt_cap.volmin;
}


int rt_setvol(struct radio_device *dev, int vol)
{
int port, i;

	if(vol == dev->curvol)
		return 0;

	port = *(int*)(dev->misc);
	if(vol == 0)
		mute(port);

	if(vol > dev->curvol)
		for(i = dev->curvol; i < vol; i++)
			incvol(port);
	else
		for(i = dev->curvol; i > vol; i--)
			decvol(port);

	if(dev->curvol == 0)
		unmute(port);

	return 0;
}


int rt_setband(struct radio_device *dev, int band)
{
/* we know in advance that we only have one band, and
 * the wrapper checks the validity of all the inputs
 */
	return 0;
}

int rt_setfreq(struct radio_device *dev, int freq)
{
int myport = *(int*)(dev->misc);

	outbits(16, RTRACK_ENCODE(freq), myport);
	outbits(8, 0xa0, myport);
/* XXX - get rid of this once setvol is implemented properly - XXX */
/* these insist on turning the thing on.  not sure I approve... */
	mdelay(1);
	outb(0, myport);
	outb(0xc8, myport);

	return 0;
}

int rt_getsigstr(struct radio_device *dev)
{
int res;
int myport = *(int*)(dev->misc);

	outb(0xf8, myport);
	mdelay(200);
	res = (int)inb(myport);
	mdelay(10);
	outb(0xe8, myport);
	if(res == 0xfd)
		return 1;
	else
		return 0;
}


/* local things */
void outbits(int bits, int data, int port)
{
	while(bits--) {
		if(data & 1) {
			outw(5, port);
			outw(5, port);
			outw(7, port);
			outw(7, port);
		} else {
			outw(1, port);
			outw(1, port);
			outw(3, port);
			outw(3, port);
		}
		data>>=1;
	}
}

void decvol(int port)
{
	outb(0x48, port);
	mdelay(100);
	outb(0xc8, port);
}

void incvol(int port)
{
	outb(0x88, port);
	mdelay(100);
	outb(0xc8, port);
}

void mute(int port)
{
	outb(0, port);
	outb(0xc0, port);
}

void unmute(int port)
{
	outb(0, port);
	outb(0xc8, port);
}
