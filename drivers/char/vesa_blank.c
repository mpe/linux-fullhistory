/*
 * vesa_blank.c
 *
 * Exported functions:
 *	void vesa_blank(void);
 *	void vesa_unblank(void);
 *	void set_vesa_blanking(const unsigned long arg);
 *
 * Not all hardware reacts well to this code - activate at your own risk.
 * Activation is done using a sufficiently recent version of setterm
 * or using a tiny C program like the following.
 *
-----------------------------------------------------------------------
|#include <stdio.h>
|#include <linux/termios.h>
|main(int argc, char *argv[]) {
|    int fd;
|    struct { char ten, onoff; } arg;
|
|    if (argc != 2) {
|	fprintf(stderr, "usage: setvesablank ON|on|off\n");
|	exit(1);
|    }
|    if ((fd = open("/dev/console", 0)) < 0)
|      fd = 0;
|    arg.ten = 10;
|    arg.onoff = 0;
|    if (!strcmp(argv[1], "on"))
|      arg.onoff = 1;
|    else if (!strcmp(argv[1], "ON"))
|      arg.onoff = 2;
|    if (ioctl(fd, TIOCLINUX, &arg)) {
|	perror("setvesablank: TIOCLINUX");
|	exit(1);
|    }
|    exit(0);
|}
-----------------------------------------------------------------------
*/

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>

extern unsigned short video_port_reg, video_port_val;

/*
 * This file handles the VESA Power Saving Protocol that lets a
 * monitor be powered down whenever not needed for a longer time.
 * The VESA protocol defines:
 *
 *  Mode/Status		HSync	VSync	Video
 *  -------------------------------------------
 *  "On"		on	on	active  (mode 0)
 *  "Suspend" {either}	on	off	blank   (mode 1)
 *            {  or  }	off	on	blank   
 *  "Off"               off	off	blank	(mode 2)
 *
 * Original code taken from the Power Management Utility (PMU) of
 * Huang shi chao, delivered together with many new monitor models
 * capable of the VESA Power Saving Protocol.
 *
 * Adapted to Linux by Christoph Rimek (chrimek@toppoint.de)  15-may-94.
 * A slightly adapted fragment of his README follows.
 *
Patch (based on Linux Kernel revision 1.0) for handling the Power Saving
feature of the new monitor generation. The code works on all these monitors
(mine is a Smile 1506) and should run on *all* video adapter cards (change
some i/o-addresses), although tested only on two different VGA-cards: a  
cheap Cirrus Logic (5428) and a miro Crystal 8S (S3-805).

You can choose from two options:

(1) Setting vesa_blanking_mode to 1.
    The code will save the current setting of your video adapters'
    register settings and then program the controller to turn off
    the vertical synchronisation pulse.

(2) Setting vesa_blanking_mode to 2.
    If your monitor locally has an Off_Mode timer then you should not
    force your video card to send the OFF-signal - your monitor will
    power down by itself.
    If your monitor cannot handle this and needs the Off-signal directly,
    or if you like your monitor to power down immediately when the
    blank_timer times out, then you choose this option.

On the other hand I'd recommend to not choose this second option unless
it is absolutely necessary. Powering down a monitor to the Off_State with
an approx. power consumption of 3-5 Watts is a rather strong action for
the CRT and it should not be done every now and then. If the software only  
sends the signal to enter Standby mode, you have the chance to interfere
before the monitor powers down. Do not set a too short period, if you love
your hardware :-)) .

If requested, in the future it may be possible to install another timer
to provide a configurable delay between the two stages Standby and Off
similar to the "setterm -blank"-feature.
*/

#define seq_port_reg	(0x3c4)		/* Sequencer register select port */
#define seq_port_val	(0x3c5)		/* Sequencer register value port  */
#define video_misc_rd	(0x3cc)		/* Video misc. read port	  */
#define video_misc_wr	(0x3c2)		/* Video misc. write port	  */

/* structure holding original VGA register settings */
static struct {
	unsigned char	SeqCtrlIndex;		/* Sequencer Index reg.   */
	unsigned char	CrtCtrlIndex;		/* CRT-Contr. Index reg.  */
	unsigned char	CrtMiscIO;		/* Miscellaneous register */
	unsigned char	HorizontalTotal;	/* CRT-Controller:00h */
	unsigned char	HorizDisplayEnd;	/* CRT-Controller:01h */
	unsigned char	StartHorizRetrace;	/* CRT-Controller:04h */
	unsigned char	EndHorizRetrace;	/* CRT-Controller:05h */
	unsigned char	Overflow;		/* CRT-Controller:07h */
	unsigned char	StartVertRetrace;	/* CRT-Controller:10h */
	unsigned char	EndVertRetrace;		/* CRT-Controller:11h */
	unsigned char	ModeControl;		/* CRT-Controller:17h */
	unsigned char	ClockingMode;		/* Seq-Controller:01h */
} vga;

static int vesa_blanking_mode = 0;
static int vesa_blanked = 0;

/* routine to blank a vesa screen */
void vesa_blank(void)
{
	int mode;

	if((mode = vesa_blanking_mode) == 0)
	  return;

	/* save original values of VGA controller registers */
	cli();
	vga.SeqCtrlIndex = inb_p(seq_port_reg);
	vga.CrtCtrlIndex = inb_p(video_port_reg);
	vga.CrtMiscIO = inb_p(video_misc_rd);
	sti();

	if(mode == 2) {
	    outb_p(0x00,video_port_reg);		/* HorizontalTotal */
	    vga.HorizontalTotal = inb_p(video_port_val);
	    outb_p(0x01,video_port_reg);		/* HorizDisplayEnd */
	    vga.HorizDisplayEnd = inb_p(video_port_val);
	    outb_p(0x04,video_port_reg);		/* StartHorizRetrace */
	    vga.StartHorizRetrace = inb_p(video_port_val);
	    outb_p(0x05,video_port_reg);		/* EndHorizRetrace */
	    vga.EndHorizRetrace = inb_p(video_port_val);
	}
	outb_p(0x07,video_port_reg);			/* Overflow */
	vga.Overflow = inb_p(video_port_val);
	outb_p(0x10,video_port_reg);			/* StartVertRetrace */
	vga.StartVertRetrace = inb_p(video_port_val);
	outb_p(0x11,video_port_reg);			/* EndVertRetrace */
	vga.EndVertRetrace = inb_p(video_port_val);
	outb_p(0x17,video_port_reg);			/* ModeControl */
	vga.ModeControl = inb_p(video_port_val);
	outb_p(0x01,seq_port_reg);			/* ClockingMode */
	vga.ClockingMode = inb_p(seq_port_val);

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	cli();
	outb_p(0x01,seq_port_reg);
	outb_p(vga.ClockingMode | 0x20,seq_port_val);

	/* test for vertical retrace in process.... */
	if ((vga.CrtMiscIO & 0x80) == 0x80)
		outb_p(vga.CrtMiscIO & 0xef,video_misc_wr);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	outb_p(0x10,video_port_reg);		/* StartVertRetrace */
	outb_p(0xff,video_port_val);		/* maximum value */
	outb_p(0x11,video_port_reg);		/* EndVertRetrace */
	outb_p(0x40,video_port_val);            /* minimum (bits 0..3)  */
	outb_p(0x07,video_port_reg);		/* Overflow */
	outb_p(vga.Overflow | 0x84,video_port_val);	/* bits 9,10 of  */
							/* vert. retrace */
	if (mode == 2) {
	    /*
	     * Set <End of horizontal retrace> to minimum (0) and
	     *  <Start of horizontal Retrace> to maximum
	     * Result: turn off horizontal sync (HSync) pulse.
	     */
	    outb_p(0x04,video_port_reg);	/* StartHorizRetrace */
	    outb_p(0xff,video_port_val);	/* maximum */
	    outb_p(0x05,video_port_reg);	/* EndHorizRetrace */
	    outb_p(0x00,video_port_val);	/* minimum (0) */
	}

	/* restore both index registers */
	outb_p(vga.SeqCtrlIndex,seq_port_reg);
	outb_p(vga.CrtCtrlIndex,video_port_reg);
	sti();

	vesa_blanked = mode;
}	

/* routine to unblank a vesa screen */
void vesa_unblank(void)
{
	if (!vesa_blanked)
	  return;

	/* restore original values of VGA controller registers */
	cli();
	outb_p(vga.CrtMiscIO,video_misc_wr);

	if (vesa_blanked == 2) {
	    outb_p(0x00,video_port_reg);	/* HorizontalTotal */
	    outb_p(vga.HorizontalTotal,video_port_val);
	    outb_p(0x01,video_port_reg);	/* HorizDisplayEnd */
	    outb_p(vga.HorizDisplayEnd,video_port_val);
	    outb_p(0x04,video_port_reg);	/* StartHorizRetrace */
	    outb_p(vga.StartHorizRetrace,video_port_val);
	    outb_p(0x05,video_port_reg);	/* EndHorizRetrace */
	    outb_p(vga.EndHorizRetrace,video_port_val);
	}
	outb_p(0x07,video_port_reg);		/* Overflow */
	outb_p(vga.Overflow,video_port_val);
	outb_p(0x10,video_port_reg);		/* StartVertRetrace */
	outb_p(vga.StartVertRetrace,video_port_val);
	outb_p(0x11,video_port_reg);		/* EndVertRetrace */
	outb_p(vga.EndVertRetrace,video_port_val);
	outb_p(0x17,video_port_reg);		/* ModeControl */
	outb_p(vga.ModeControl,video_port_val);
	outb_p(0x01,seq_port_reg);		/* ClockingMode */
	outb_p(vga.ClockingMode,seq_port_val);

	/* restore index/control registers */
	outb_p(vga.SeqCtrlIndex,seq_port_reg);
	outb_p(vga.CrtCtrlIndex,video_port_reg);
	sti();

	vesa_blanked = 0;
}

void set_vesa_blanking(const unsigned long arg)
{
	char *argp = (char *)(arg + 1);
	unsigned int mode = get_fs_byte(argp);
	vesa_blanking_mode = ((mode < 3) ? mode : 0);
}
