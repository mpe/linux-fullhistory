
/* linux/drivers/sound/dmasound.c */

/*

VoxWare compatible Atari TT DMA sound driver for 680x0 Linux

(c) 1995 by Michael Schlueter & Michael Marte

Michael Schlueter (michael@duck.syd.de) did the basic structure of the VFS
interface and the u-law to signed byte conversion.

Michael Marte (marte@informatik.uni-muenchen.de) did the sound queue,
/dev/mixer, /dev/sndstat and complemented the VFS interface. He would like
to thank:
Michael Schlueter for initial ideas and documentation on the MFP and
the DMA sound hardware.
Therapy? for their CD 'Troublegum' which really made me rock.

/dev/sndstat is based on code by Hannu Savolainen, the author of the
VoxWare family of drivers.

This file is subject to the terms and conditions of the GNU General Public
License.  See the file COPYING in the main directory of this archive
for more details.

History:
1995/8/25	first release

1995/9/02	++roman: fixed atari_stram_alloc() call, the timer programming
			and several race conditions

1995/9/14	++roman: After some discussion with Michael Schlueter, revised
			the interrupt disabling
			Slightly speeded up U8->S8 translation by using long
			operations where possible
			Added 4:3 interpolation for /dev/audio

1995/9/20	++TeSche: Fixed a bug in sq_write and changed /dev/audio
			converting to play at 12517Hz instead of 6258Hz.

1995/9/23	++TeSche: Changed sq_interrupt() and sq_play() to pre-program
			the DMA for another frame while there's still one
			running. This allows the IRQ response to be
			arbitrarily delayed and playing will still continue.

1995/10/14	++Guenther_Kelleter@ac3.maus.de, ++TeSche: better support for
			Falcon audio (the Falcon doesn't raise an IRQ at the
			end of a frame, but at the beginning instead!). uses
			'if (codec_dma)' in lots of places to simply switch
			between Falcon and TT code.

1995/11/06	++TeSche: started introducing a hardware abstraction scheme
			(may perhaps also serve for Amigas?), can now play
			samples at almost all frequencies by means of a more
			generalized expand routine, takes a good deal of care
			to cut data only at sample sizes, buffer size is now
			a kernel runtime option, implemented fsync() & several
			minor improvements
		++Guenther: useful hints and bugfixes, cross-checked it for
			Falcons

1996/3/9	++geert: support added for Amiga, A-law, 16-bit little endian.
			Unification to drivers/sound/dmasound.c.
1996/4/6	++Martin Mitchell: updated to 1.3 kernel.
*/


#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/config.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atariints.h>
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */

#include "dmasound.h"
#include <linux/soundcard.h>


#ifdef CONFIG_ATARI
extern void atari_microwire_cmd(int cmd);
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
   /*
    *	The minimum period for audio depends on htotal (for OCS/ECS/AGA)
    *	(Imported from arch/m68k/amiga/amisound.c)
    */

extern volatile u_short amiga_audio_min_period;


   /*
    *	amiga_mksound() should be able to restore the period after beeping
    *	(Imported from arch/m68k/amiga/amisound.c)
    */

extern u_short amiga_audio_period;


   /*
    *	Audio DMA masks
    */

#define AMI_AUDIO_OFF	(DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3)
#define AMI_AUDIO_8	(DMAF_SETCLR | DMAF_MASTER | DMAF_AUD0 | DMAF_AUD1)
#define AMI_AUDIO_14	(AMI_AUDIO_8 | DMAF_AUD2 | DMAF_AUD3)

#endif /* CONFIG_AMIGA */


/*** Some declarations *******************************************************/


#define DMASND_TT		1
#define DMASND_FALCON		2
#define DMASND_AMIGA		3

#define MAX_CATCH_RADIUS	10
#define MIN_BUFFERS		4
#define MIN_BUFSIZE 		4
#define MAX_BUFSIZE		128	/* Limit for Amiga */

static int catchRadius = 0, numBufs = 4, bufSize = 32;


#define arraysize(x)	(sizeof(x)/sizeof(*(x)))
#define min(x, y)	((x) < (y) ? (x) : (y))
#define le2be16(x)	(((x)<<8 & 0xff00) | ((x)>>8 & 0x00ff))
#define le2be16dbl(x)	(((x)<<8 & 0xff00ff00) | ((x)>>8 & 0x00ff00ff))

#define IOCTL_IN(arg)		get_user((int *)(arg))
#define IOCTL_OUT(arg, ret)	ioctl_return((int *)arg, ret)


/*** Some low level helpers **************************************************/


/* 8 bit mu-law */

static char ulaw2dma8[] = {
    -126,   -122,   -118,   -114,   -110,   -106,   -102,    -98,
     -94,    -90,    -86,    -82,    -78,    -74,    -70,    -66,
     -63,    -61,    -59,    -57,    -55,    -53,    -51,    -49,
     -47,    -45,    -43,    -41,    -39,    -37,    -35,    -33,
     -31,    -30,    -29,    -28,    -27,    -26,    -25,    -24,
     -23,    -22,    -21,    -20,    -19,    -18,    -17,    -16,
     -16,    -15,    -15,    -14,    -14,    -13,    -13,    -12,
     -12,    -11,    -11,    -10,    -10,     -9,     -9,     -8,
      -8,     -8,     -7,     -7,     -7,     -7,     -6,     -6,
      -6,     -6,     -5,     -5,     -5,     -5,     -4,     -4,
      -4,     -4,     -4,     -4,     -3,     -3,     -3,     -3,
      -3,     -3,     -3,     -3,     -2,     -2,     -2,     -2,
      -2,     -2,     -2,     -2,     -2,     -2,     -2,     -2,
      -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
      -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
      -1,     -1,     -1,     -1,     -1,     -1,     -1,      0,
     125,    121,    117,    113,    109,    105,    101,     97,
      93,     89,     85,     81,     77,     73,     69,     65,
      62,     60,     58,     56,     54,     52,     50,     48,
      46,     44,     42,     40,     38,     36,     34,     32,
      30,     29,     28,     27,     26,     25,     24,     23,
      22,     21,     20,     19,     18,     17,     16,     15,
      15,     14,     14,     13,     13,     12,     12,     11,
      11,     10,     10,      9,      9,      8,      8,      7,
       7,      7,      6,      6,      6,      6,      5,      5,
       5,      5,      4,      4,      4,      4,      3,      3,
       3,      3,      3,      3,      2,      2,      2,      2,
       2,      2,      2,      2,      1,      1,      1,      1,
       1,      1,      1,      1,      1,      1,      1,      1,
       0,      0,      0,      0,      0,      0,      0,      0,
       0,      0,      0,      0,      0,      0,      0,      0,
       0,      0,      0,      0,      0,      0,      0,      0
};

/* 8 bit A-law */

static char alaw2dma8[] = {
     -22,    -21,    -24,    -23,    -18,    -17,    -20,    -19,
     -30,    -29,    -32,    -31,    -26,    -25,    -28,    -27,
     -11,    -11,    -12,    -12,     -9,     -9,    -10,    -10,
     -15,    -15,    -16,    -16,    -13,    -13,    -14,    -14,
     -86,    -82,    -94,    -90,    -70,    -66,    -78,    -74,
    -118,   -114,   -126,   -122,   -102,    -98,   -110,   -106,
     -43,    -41,    -47,    -45,    -35,    -33,    -39,    -37,
     -59,    -57,    -63,    -61,    -51,    -49,    -55,    -53,
      -2,     -2,     -2,     -2,     -2,     -2,     -2,     -2,
      -2,     -2,     -2,     -2,     -2,     -2,     -2,     -2,
      -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
      -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
      -6,     -6,     -6,     -6,     -5,     -5,     -5,     -5,
      -8,     -8,     -8,     -8,     -7,     -7,     -7,     -7,
      -3,     -3,     -3,     -3,     -3,     -3,     -3,     -3,
      -4,     -4,     -4,     -4,     -4,     -4,     -4,     -4,
      21,     20,     23,     22,     17,     16,     19,     18,
      29,     28,     31,     30,     25,     24,     27,     26,
      10,     10,     11,     11,      8,      8,      9,      9,
      14,     14,     15,     15,     12,     12,     13,     13,
      86,     82,     94,     90,     70,     66,     78,     74,
     118,    114,    126,    122,    102,     98,    110,    106,
      43,     41,     47,     45,     35,     33,     39,     37,
      59,     57,     63,     61,     51,     49,     55,     53,
       1,      1,      1,      1,      1,      1,      1,      1,
       1,      1,      1,      1,      1,      1,      1,      1,
       0,      0,      0,      0,      0,      0,      0,      0,
       0,      0,      0,      0,      0,      0,      0,      0,
       5,      5,      5,      5,      4,      4,      4,      4,
       7,      7,      7,      7,      6,      6,      6,      6,
       2,      2,      2,      2,      2,      2,      2,      2,
       3,      3,      3,      3,      3,      3,      3,      3
};


#ifdef HAS_16BIT_TABLES

/* 16 bit mu-law */

static char ulaw2dma16[] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364,  -9852,  -9340,  -8828,  -8316,
     -7932,  -7676,  -7420,  -7164,  -6908,  -6652,  -6396,  -6140,
     -5884,  -5628,  -5372,  -5116,  -4860,  -4604,  -4348,  -4092,
     -3900,  -3772,  -3644,  -3516,  -3388,  -3260,  -3132,  -3004,
     -2876,  -2748,  -2620,  -2492,  -2364,  -2236,  -2108,  -1980,
     -1884,  -1820,  -1756,  -1692,  -1628,  -1564,  -1500,  -1436,
     -1372,  -1308,  -1244,  -1180,  -1116,  -1052,   -988,   -924,
      -876,   -844,   -812,   -780,   -748,   -716,   -684,   -652,
      -620,   -588,   -556,   -524,   -492,   -460,   -428,   -396,
      -372,   -356,   -340,   -324,   -308,   -292,   -276,   -260,
      -244,   -228,   -212,   -196,   -180,   -164,   -148,   -132,
      -120,   -112,   -104,    -96,    -88,    -80,    -72,    -64,
       -56,    -48,    -40,    -32,    -24,    -16,     -8,      0,
     32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
     23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
     15996,  15484,  14972,  14460,  13948,  13436,  12924,  12412,
     11900,  11388,  10876,  10364,   9852,   9340,   8828,   8316,
      7932,   7676,   7420,   7164,   6908,   6652,   6396,   6140,
      5884,   5628,   5372,   5116,   4860,   4604,   4348,   4092,
      3900,   3772,   3644,   3516,   3388,   3260,   3132,   3004,
      2876,   2748,   2620,   2492,   2364,   2236,   2108,   1980,
      1884,   1820,   1756,   1692,   1628,   1564,   1500,   1436,
      1372,   1308,   1244,   1180,   1116,   1052,    988,    924,
       876,    844,    812,    780,    748,    716,    684,    652,
       620,    588,    556,    524,    492,    460,    428,    396,
       372,    356,    340,    324,    308,    292,    276,    260,
       244,    228,    212,    196,    180,    164,    148,    132,
       120,    112,    104,     96,     88,     80,     72,     64,
        56,     48,     40,     32,     24,     16,      8,      0,
};

/* 16 bit A-law */

static char alaw2dma16[] = {
     -5504,  -5248,  -6016,  -5760,  -4480,  -4224,  -4992,  -4736,
     -7552,  -7296,  -8064,  -7808,  -6528,  -6272,  -7040,  -6784,
     -2752,  -2624,  -3008,  -2880,  -2240,  -2112,  -2496,  -2368,
     -3776,  -3648,  -4032,  -3904,  -3264,  -3136,  -3520,  -3392,
    -22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
    -30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
    -11008, -10496, -12032, -11520,  -8960,  -8448,  -9984,  -9472,
    -15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
      -344,   -328,   -376,   -360,   -280,   -264,   -312,   -296,
      -472,   -456,   -504,   -488,   -408,   -392,   -440,   -424,
       -88,    -72,   -120,   -104,    -24,     -8,    -56,    -40,
      -216,   -200,   -248,   -232,   -152,   -136,   -184,   -168,
     -1376,  -1312,  -1504,  -1440,  -1120,  -1056,  -1248,  -1184,
     -1888,  -1824,  -2016,  -1952,  -1632,  -1568,  -1760,  -1696,
      -688,   -656,   -752,   -720,   -560,   -528,   -624,   -592,
      -944,   -912,  -1008,   -976,   -816,   -784,   -880,   -848,
      5504,   5248,   6016,   5760,   4480,   4224,   4992,   4736,
      7552,   7296,   8064,   7808,   6528,   6272,   7040,   6784,
      2752,   2624,   3008,   2880,   2240,   2112,   2496,   2368,
      3776,   3648,   4032,   3904,   3264,   3136,   3520,   3392,
     22016,  20992,  24064,  23040,  17920,  16896,  19968,  18944,
     30208,  29184,  32256,  31232,  26112,  25088,  28160,  27136,
     11008,  10496,  12032,  11520,   8960,   8448,   9984,   9472,
     15104,  14592,  16128,  15616,  13056,  12544,  14080,  13568,
       344,    328,    376,    360,    280,    264,    312,    296,
       472,    456,    504,    488,    408,    392,    440,    424,
        88,     72,    120,    104,     24,      8,     56,     40,
       216,    200,    248,    232,    152,    136,    184,    168,
      1376,   1312,   1504,   1440,   1120,   1056,   1248,   1184,
      1888,   1824,   2016,   1952,   1632,   1568,   1760,   1696,
       688,    656,    752,    720,    560,    528,    624,    592,
       944,    912,   1008,    976,    816,    784,    880,    848,
};
#endif /* HAS_16BIT_TABLES */


#ifdef HAS_14BIT_TABLES

/* 14 bit mu-law (lsb) */

static char alaw2dma14l[] = {
        33,     33,     33,     33,     33,     33,     33,     33,
        33,     33,     33,     33,     33,     33,     33,     33,
        33,     33,     33,     33,     33,     33,     33,     33,
        33,     33,     33,     33,     33,     33,     33,     33,
         1,      1,      1,      1,      1,      1,      1,      1,
         1,      1,      1,      1,      1,      1,      1,      1,
        49,     17,     49,     17,     49,     17,     49,     17,
        49,     17,     49,     17,     49,     17,     49,     17,
        41,     57,      9,     25,     41,     57,      9,     25,
        41,     57,      9,     25,     41,     57,      9,     25,
        37,     45,     53,     61,      5,     13,     21,     29,
        37,     45,     53,     61,      5,     13,     21,     29,
        35,     39,     43,     47,     51,     55,     59,     63,
         3,      7,     11,     15,     19,     23,     27,     31,
        34,     36,     38,     40,     42,     44,     46,     48,
        50,     52,     54,     56,     58,     60,     62,      0,
        31,     31,     31,     31,     31,     31,     31,     31,
        31,     31,     31,     31,     31,     31,     31,     31,
        31,     31,     31,     31,     31,     31,     31,     31,
        31,     31,     31,     31,     31,     31,     31,     31,
        63,     63,     63,     63,     63,     63,     63,     63,
        63,     63,     63,     63,     63,     63,     63,     63,
        15,     47,     15,     47,     15,     47,     15,     47,
        15,     47,     15,     47,     15,     47,     15,     47,
        23,      7,     55,     39,     23,      7,     55,     39,
        23,      7,     55,     39,     23,      7,     55,     39,
        27,     19,     11,      3,     59,     51,     43,     35,
        27,     19,     11,      3,     59,     51,     43,     35,
        29,     25,     21,     17,     13,      9,      5,      1,
        61,     57,     53,     49,     45,     41,     37,     33,
        30,     28,     26,     24,     22,     20,     18,     16,
        14,     12,     10,      8,      6,      4,      2,      0
};

/* 14 bit A-law (lsb) */

static char alaw2dma14l[] = {
        32,     32,     32,     32,     32,     32,     32,     32,
        32,     32,     32,     32,     32,     32,     32,     32,
        16,     48,     16,     48,     16,     48,     16,     48,
        16,     48,     16,     48,     16,     48,     16,     48,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
        42,     46,     34,     38,     58,     62,     50,     54,
        10,     14,      2,      6,     26,     30,     18,     22,
        42,     46,     34,     38,     58,     62,     50,     54,
        10,     14,      2,      6,     26,     30,     18,     22,
        40,     56,      8,     24,     40,     56,      8,     24,
        40,     56,      8,     24,     40,     56,      8,     24,
        20,     28,      4,     12,     52,     60,     36,     44,
        20,     28,      4,     12,     52,     60,     36,     44,
        32,     32,     32,     32,     32,     32,     32,     32,
        32,     32,     32,     32,     32,     32,     32,     32,
        48,     16,     48,     16,     48,     16,     48,     16,
        48,     16,     48,     16,     48,     16,     48,     16,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
        22,     18,     30,     26,      6,      2,     14,     10,
        54,     50,     62,     58,     38,     34,     46,     42,
        22,     18,     30,     26,      6,      2,     14,     10,
        54,     50,     62,     58,     38,     34,     46,     42,
        24,      8,     56,     40,     24,      8,     56,     40,
        24,      8,     56,     40,     24,      8,     56,     40,
        44,     36,     60,     52,     12,      4,     28,     20,
        44,     36,     60,     52,     12,      4,     28,     20
};
#endif /* HAS_14BIT_TABLES */


/*** Translations ************************************************************/


#ifdef CONFIG_ATARI
static long ata_ct_law(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft);
static long ata_ct_s8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft);
static long ata_ct_u8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft);
static long ata_ct_s16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ata_ct_u16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ata_ct_s16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ata_ct_u16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ata_ctx_law(const u_char *userPtr, long userCount, u_char frame[],
			long *frameUsed, long frameLeft);
static long ata_ctx_s8(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft);
static long ata_ctx_u8(const u_char *userPtr, long userCount, u_char frame[],
		        long *frameUsed, long frameLeft);
static long ata_ctx_s16be(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft);
static long ata_ctx_u16be(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft);
static long ata_ctx_s16le(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft);
static long ata_ctx_u16le(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft);
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
static long ami_ct_law(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft);
static long ami_ct_s8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft);
static long ami_ct_u8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft);
static long ami_ct_s16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ami_ct_u16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ami_ct_s16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
static long ami_ct_u16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft);
#endif /* CONFIG_AMIGA */


/*** Machine definitions *****************************************************/


typedef struct {
    int type;
    void *(*dma_alloc)(unsigned int, int);
    void (*dma_free)(void *, unsigned int);
    int (*irqinit)(void);
    void (*init)(void);
    void (*silence)(void);
    int (*setFormat)(int);
    int (*setVolume)(int);
    int (*setBass)(int);
    int (*setTreble)(int);
    void (*play)(void);
} MACHINE;


/*** Low level stuff *********************************************************/


typedef struct {
    int format;		/* AFMT_* */
    int stereo;		/* 0 = mono, 1 = stereo */
    int size;		/* 8/16 bit*/
    int speed;		/* speed */
} SETTINGS;

typedef struct {
    long (*ct_ulaw)(const u_char *, long, u_char *, long *, long);
    long (*ct_alaw)(const u_char *, long, u_char *, long *, long);
    long (*ct_s8)(const u_char *, long, u_char *, long *, long);
    long (*ct_u8)(const u_char *, long, u_char *, long *, long);
    long (*ct_s16be)(const u_char *, long, u_char *, long *, long);
    long (*ct_u16be)(const u_char *, long, u_char *, long *, long);
    long (*ct_s16le)(const u_char *, long, u_char *, long *, long);
    long (*ct_u16le)(const u_char *, long, u_char *, long *, long);
} TRANS;

struct sound_settings {
    MACHINE mach;	/* machine dependent things */
    SETTINGS hard;	/* hardware settings */
    SETTINGS soft;	/* software settings */
    SETTINGS dsp;	/* /dev/dsp default settings */
    TRANS *trans;	/* supported translations */
    int volume_left;	/* volume (range is machine dependent) */
    int volume_right;
    int bass;		/* tone (range is machine dependent) */
    int treble;
    int minDev;		/* minor device number currently open */
#ifdef CONFIG_ATARI
    int bal;		/* balance factor for expanding (not volume!) */
    u_long data;	/* data for expanding */
#endif /* CONFIG_ATARI */
};

static struct sound_settings sound;


#ifdef CONFIG_ATARI
static void *AtaAlloc(unsigned int size, int flags);
static void AtaFree(void *, unsigned int size);
static int AtaIrqInit(void);
static int AtaSetBass(int bass);
static int AtaSetTreble(int treble);
static void TTSilence(void);
static void TTInit(void);
static int TTSetFormat(int format);
static int TTSetVolume(int volume);
static void FalconSilence(void);
static void FalconInit(void);
static int FalconSetFormat(int format);
static int FalconSetVolume(int volume);
static void ata_sq_play_next_frame(int index);
static void AtaPlay(void);
static void ata_sq_interrupt(int irq, struct pt_regs *fp, void *dummy);
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
static void *AmiAlloc(unsigned int size, int flags);
static void AmiFree(void *, unsigned int);
static int AmiIrqInit(void);
static void AmiSilence(void);
static void AmiInit(void);
static int AmiSetFormat(int format);
static int AmiSetVolume(int volume);
static int AmiSetTreble(int treble);
static void ami_sq_play_next_frame(int index);
static void AmiPlay(void);
static void ami_sq_interrupt(int irq, struct pt_regs *fp, void *dummy);
#endif /* CONFIG_AMIGA */


/*** Mid level stuff *********************************************************/


static void sound_silence(void);
static void sound_init(void);
static int sound_set_format(int format);
static int sound_set_speed(int speed);
static int sound_set_stereo(int stereo);
static int sound_set_volume(int volume);
#ifdef CONFIG_ATARI
static int sound_set_bass(int bass);
#endif /* CONFIG_ATARI */
static int sound_set_treble(int treble);
static long sound_copy_translate(const u_char *userPtr, long userCount,
				 u_char frame[], long *frameUsed,
				 long frameLeft);


/*
 * /dev/mixer abstraction
 */

struct sound_mixer {
    int busy;
};

static struct sound_mixer mixer;

static void mixer_init(void);
static int mixer_open(int open_mode);
static int mixer_release(void);
static int mixer_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg);


/*
 * Sound queue stuff, the heart of the driver
 */

struct sound_queue {
    int max_count, block_size;
    char **buffers;

    /* it shouldn't be necessary to declare any of these volatile */
    int front, rear, count;
    int rear_size;
    /*
     *	The use of the playing field depends on the hardware
     *
     *	Atari: The number of frames that are loaded/playing
     *
     *	Amiga: Bit 0 is set: a frame is loaded
     *	       Bit 1 is set: a frame is playing
     */
    int playing;
    struct wait_queue *write_queue, *open_queue, *sync_queue;
    int open_mode;
    int busy, syncing;
#ifdef CONFIG_ATARI
    int ignore_int;		/* ++TeSche: used for Falcon */
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
    int block_size_half, block_size_quarter;
#endif /* CONFIG_AMIGA */
};

static struct sound_queue sq;

#define sq_block_address(i)	(sq.buffers[i])
#define SIGNAL_RECEIVED	(current->signal & ~current->blocked)
#define NON_BLOCKING(open_mode)	(open_mode & O_NONBLOCK)
#define ONE_SECOND	HZ	/* in jiffies (100ths of a second) */
#define NO_TIME_LIMIT	0xffffffff
#define SLEEP(queue, time_limit) \
	current->timeout = jiffies+(time_limit); \
	interruptible_sleep_on(&queue);
#define WAKE_UP(queue)	(wake_up_interruptible(&queue))

static void sq_init(int numBufs, int bufSize, char **buffers);
static void sq_play(void);
static int sq_write(const char *src, int uLeft);
static int sq_open(int open_mode);
static void sq_reset(void);
static int sq_sync(void);
static int sq_release(void);


/*
 * /dev/sndstat
 */

struct sound_state {
    int busy;
    char buf[512];
    int len, ptr;
};

static struct sound_state state;

static void state_init(void);
static int state_open(int open_mode);
static int state_release(void);
static int state_read(char *dest, int count);


/*** High level stuff ********************************************************/


static int sound_open(struct inode *inode, struct file *file);
static int sound_fsync(struct inode *inode, struct file *filp);
static void sound_release(struct inode *inode, struct file *file);
static int sound_lseek(struct inode *inode, struct file *file, off_t offset,
		       int orig);
static int sound_read(struct inode *inode, struct file *file, char *buf,
		      int count);
static int sound_write(struct inode *inode, struct file *file, const char *buf,
		       int count);
static int ioctl_return(int *addr, int value);
static int unknown_minor_dev(char *fname, int dev);
static int sound_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg);


/*** Config & Setup **********************************************************/


void soundcard_init(void);
void dmasound_setup(char *str, int *ints);
void sound_setup(char *str, int *ints);		/* ++Martin: stub for now */


/*** Translations ************************************************************/


/* ++TeSche: radically changed for new expanding purposes...
 *
 * These two routines now deal with copying/expanding/translating the samples
 * from user space into our buffer at the right frequency. They take care about
 * how much data there's actually to read, how much buffer space there is and
 * to convert samples into the right frequency/encoding. They will only work on
 * complete samples so it may happen they leave some bytes in the input stream
 * if the user didn't write a multiple of the current sample size. They both
 * return the number of bytes they've used from both streams so you may detect
 * such a situation. Luckily all programs should be able to cope with that.
 *
 * I think I've optimized anything as far as one can do in plain C, all
 * variables should fit in registers and the loops are really short. There's
 * one loop for every possible situation. Writing a more generalized and thus
 * parameterized loop would only produce slower code. Feel free to optimize
 * this in assembler if you like. :)
 *
 * I think these routines belong here because they're not yet really hardware
 * independent, especially the fact that the Falcon can play 16bit samples
 * only in stereo is hardcoded in both of them!
 *
 * ++geert: split in even more functions (one per format)
 */

#ifdef CONFIG_ATARI
static long ata_ct_law(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft)
{
    char *table = sound.soft.format == AFMT_MU_LAW ? ulaw2dma8 : alaw2dma8;
    long count, used;
    u_char *p = &frame[*frameUsed];

    count = min(userCount, frameLeft);
    if (sound.soft.stereo)
	count &= ~1;
    used = count;
    while (count > 0) {
	*p++ = table[get_user(userPtr++)];
	count--;
    }
    *frameUsed += used;
    return(used);
}


static long ata_ct_s8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft)
{
    long count, used;
    void *p = &frame[*frameUsed];

    count = min(userCount, frameLeft);
    if (sound.soft.stereo)
	count &= ~1;
    used = count;
    memcpy_fromfs(p, userPtr, count);
    *frameUsed += used;
    return(used);
}


static long ata_ct_u8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft)
{
    long count, used;

    if (!sound.soft.stereo) {
	u_char *p = &frame[*frameUsed];
	count = min(userCount, frameLeft);
	used = count;
	while (count > 0) {
	    *p++ = get_user(userPtr++) ^ 0x80;
	    count--;
	}
    } else {
	u_short *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>1;
	used = count*2;
	while (count > 0) {
	    *p++ = get_user(((u_short *)userPtr)++) ^ 0x8080;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ata_ct_s16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    *p++ = data;
	    *p++ = data;
	    count--;
	}
	*frameUsed += used*2;
    } else {
	void *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft) & ~3;
	used = count;
	memcpy_fromfs(p, userPtr, count);
	*frameUsed += used;
    }
    return(used);
}


static long ata_ct_u16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++) ^ 0x8000;
	    *p++ = data;
	    *p++ = data;
	    count--;
	}
	*frameUsed += used*2;
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>2;
	used = count*4;
	while (count > 0) {
	    *p++ = get_user(((u_int *)userPtr)++) ^ 0x80008000;
	    count--;
	}
	*frameUsed += used;
    }
    return(used);
}


static long ata_ct_s16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    count = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data);
	    *p++ = data;
	    *p++ = data;
	    count--;
	}
	*frameUsed += used*2;
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>2;
	used = count*4;
	while (count > 0) {
	    data = get_user(((u_int *)userPtr)++);
	    data = le2be16dbl(data);
	    *p++ = data;
	    count--;
	}
	*frameUsed += used;
    }
    return(used);
}


static long ata_ct_u16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    count = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data) ^ 0x8000;
	    *p++ = data;
	    *p++ = data;
	}
	*frameUsed += used*2;
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	count = min(userCount, frameLeft)>>2;
	used = count;
	while (count > 0) {
	    data = get_user(((u_int *)userPtr)++);
	    data = le2be16dbl(data) ^ 0x80008000;
	    *p++ = data;
	    count--;
	}
	*frameUsed += used;
    }
    return(used);
}


static long ata_ctx_law(const u_char *userPtr, long userCount, u_char frame[],
			long *frameUsed, long frameLeft)
{
    char *table = sound.soft.format == AFMT_MU_LAW ? ulaw2dma8 : alaw2dma8;
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_char *p = &frame[*frameUsed];
	while (frameLeft) {
	    if (bal < 0) {
		if (!userCount)
		    break;
		data = table[get_user(userPtr++)];
		userCount--;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft--;
	    bal -= sSpeed;
	}
    } else {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 2) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = table[get_user(userPtr++)] << 8;
		data |= table[get_user(userPtr++)];
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 2;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_s8(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_char *p = &frame[*frameUsed];
	while (frameLeft) {
	    if (bal < 0) {
		if (!userCount)
		    break;
		data = get_user(userPtr++);
		userCount--;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft--;
	    bal -= sSpeed;
	}
    } else {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 2) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++);
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 2;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_u8(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_char *p = &frame[*frameUsed];
	while (frameLeft) {
	    if (bal < 0) {
		if (!userCount)
		    break;
		data = get_user(userPtr++) ^ 0x80;
		userCount--;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft--;
	    bal -= sSpeed;
	}
    } else {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 2) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++) ^ 0x8080;
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 2;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_s16be(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++);
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 4)
		    break;
		data = get_user(((u_int *)userPtr)++);
		userCount -= 4;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_u16be(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++) ^ 0x8000;
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 4)
		    break;
		data = get_user(((u_int *)userPtr)++) ^ 0x80008000;
		userCount -= 4;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_s16le(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++);
		data = le2be16(data);
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 4)
		    break;
		data = get_user(((u_int *)userPtr)++);
		data = le2be16dbl(data);
		userCount -= 4;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}


static long ata_ctx_u16le(const u_char *userPtr, long userCount, u_char frame[],
			  long *frameUsed, long frameLeft)
{
    /* this should help gcc to stuff everything into registers */
    u_long data = sound.data;
    long bal = sound.bal;
    long hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
    long used, usedf;

    used = userCount;
    usedf = frameLeft;
    if (!sound.soft.stereo) {
	u_short *p = (u_short *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 2)
		    break;
		data = get_user(((u_short *)userPtr)++);
		data = le2be16(data) ^ 0x8000;
		userCount -= 2;
		bal += hSpeed;
	    }
	    *p++ = data;
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    } else {
	u_long *p = (u_long *)&frame[*frameUsed];
	while (frameLeft >= 4) {
	    if (bal < 0) {
		if (userCount < 4)
		    break;
		data = get_user(((u_int *)userPtr)++);
		data = le2be16dbl(data) ^ 0x80008000;
		userCount -= 4;
		bal += hSpeed;
	    }
	    *p++ = data;
	    frameLeft -= 4;
	    bal -= sSpeed;
	}
    }
    sound.bal = bal;
    sound.data = data;
    used -= userCount;
    *frameUsed += usedf-frameLeft;
    return(used);
}
#endif /* CONFIG_ATARI */


#ifdef CONFIG_AMIGA
static long ami_ct_law(const u_char *userPtr, long userCount, u_char frame[],
		       long *frameUsed, long frameLeft)
{
    char *table = sound.soft.format == AFMT_MU_LAW ? ulaw2dma8 : alaw2dma8;
    long count, used;

    if (!sound.soft.stereo) {
	u_char *p = &frame[*frameUsed];
	count = min(userCount, frameLeft) & ~1;
	used = count;
	while (count > 0) {
	    *p++ = table[get_user(userPtr++)];
	    count--;
	}
    } else {
	u_char *left = &frame[*frameUsed>>1];
	u_char *right = left+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    *left++ = table[get_user(userPtr++)];
	    *right++ = table[get_user(userPtr++)];
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_s8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft)
{
    long count, used;

    if (!sound.soft.stereo) {
	void *p = &frame[*frameUsed];
	count = min(userCount, frameLeft) & ~1;
	used = count;
	memcpy_fromfs(p, userPtr, count);
    } else {
	u_char *left = &frame[*frameUsed>>1];
	u_char *right = left+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    *left++ = get_user(userPtr++);
	    *right++ = get_user(userPtr++);
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_u8(const u_char *userPtr, long userCount, u_char frame[],
		      long *frameUsed, long frameLeft)
{
    long count, used;

    if (!sound.soft.stereo) {
	char *p = &frame[*frameUsed];
	count = min(userCount, frameLeft) & ~1;
	used = count;
	while (count > 0) {
	    *p++ = get_user(userPtr++) ^ 0x80;
	    count--;
	}
    } else {
	u_char *left = &frame[*frameUsed>>1];
	u_char *right = left+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    *left++ = get_user(userPtr++) ^ 0x80;
	    *right++ = get_user(userPtr++) ^ 0x80;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_s16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_char *high = &frame[*frameUsed>>1];
	u_char *low = high+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    *high = data>>8;
	    *low = (data>>2) & 0x3f;
	    count--;
	}
    } else {
	u_char *lefth = &frame[*frameUsed>>2];
	u_char *leftl = lefth+sq.block_size_quarter;
	u_char *righth = lefth+sq.block_size_half;
	u_char *rightl = righth+sq.block_size_quarter;
	count = min(userCount, frameLeft)>>2 & ~1;
	used = count*4;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    *lefth = data>>8;
	    *leftl = (data>>2) & 0x3f;
	    data = get_user(((u_short *)userPtr)++);
	    *righth = data>>8;
	    *rightl = (data>>2) & 0x3f;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_u16be(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_char *high = &frame[*frameUsed>>1];
	u_char *low = high+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++) ^ 0x8000;
	    *high = data>>8;
	    *low = (data>>2) & 0x3f;
	    count--;
	}
    } else {
	u_char *lefth = &frame[*frameUsed>>2];
	u_char *leftl = lefth+sq.block_size_quarter;
	u_char *righth = lefth+sq.block_size_half;
	u_char *rightl = righth+sq.block_size_quarter;
	count = min(userCount, frameLeft)>>2 & ~1;
	used = count*4;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++) ^ 0x8000;
	    *lefth = data>>8;
	    *leftl = (data>>2) & 0x3f;
	    data = get_user(((u_short *)userPtr)++) ^ 0x8000;
	    *righth = data>>8;
	    *rightl = (data>>2) & 0x3f;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_s16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_char *high = &frame[*frameUsed>>1];
	u_char *low = high+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data);
	    *high = data>>8;
	    *low = (data>>2) & 0x3f;
	    count--;
	}
    } else {
	u_char *lefth = &frame[*frameUsed>>2];
	u_char *leftl = lefth+sq.block_size_quarter;
	u_char *righth = lefth+sq.block_size_half;
	u_char *rightl = righth+sq.block_size_quarter;
	count = min(userCount, frameLeft)>>2 & ~1;
	used = count*4;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data);
	    *lefth = data>>8;
	    *leftl = (data>>2) & 0x3f;
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data);
	    *righth = data>>8;
	    *rightl = (data>>2) & 0x3f;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}


static long ami_ct_u16le(const u_char *userPtr, long userCount, u_char frame[],
			 long *frameUsed, long frameLeft)
{
    long count, used;
    u_long data;

    if (!sound.soft.stereo) {
	u_char *high = &frame[*frameUsed>>1];
	u_char *low = high+sq.block_size_half;
	count = min(userCount, frameLeft)>>1 & ~1;
	used = count*2;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data) ^ 0x8000;
	    *high = data>>8;
	    *low = (data>>2) & 0x3f;
	    count--;
	}
    } else {
	u_char *lefth = &frame[*frameUsed>>2];
	u_char *leftl = lefth+sq.block_size_quarter;
	u_char *righth = lefth+sq.block_size_half;
	u_char *rightl = righth+sq.block_size_quarter;
	count = min(userCount, frameLeft)>>2 & ~1;
	used = count*4;
	while (count > 0) {
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data) ^ 0x8000;
	    *lefth = data>>8;
	    *leftl = (data>>2) & 0x3f;
	    data = get_user(((u_short *)userPtr)++);
	    data = le2be16(data) ^ 0x8000;
	    *righth = data>>8;
	    *rightl = (data>>2) & 0x3f;
	    count--;
	}
    }
    *frameUsed += used;
    return(used);
}
#endif /* CONFIG_AMIGA */


#ifdef CONFIG_ATARI
static TRANS transTTNormal = {
    ata_ct_law, ata_ct_law, ata_ct_s8, ata_ct_u8, NULL, NULL, NULL, NULL
};

static TRANS transTTExpanding = {
    ata_ctx_law, ata_ctx_law, ata_ctx_s8, ata_ctx_u8, NULL, NULL, NULL, NULL
};

static TRANS transFalconNormal = {
    ata_ct_law, ata_ct_law, ata_ct_s8, ata_ct_u8, ata_ct_s16be, ata_ct_u16be,
    ata_ct_s16le, ata_ct_u16le
};

static TRANS transFalconExpanding = {
    ata_ctx_law, ata_ctx_law, ata_ctx_s8, ata_ctx_u8, ata_ctx_s16be,
    ata_ctx_u16be, ata_ctx_s16le, ata_ctx_u16le
};
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
static TRANS transAmiga = {
    ami_ct_law, ami_ct_law, ami_ct_s8, ami_ct_u8, ami_ct_s16be, ami_ct_u16be,
    ami_ct_s16le, ami_ct_u16le
};
#endif /* CONFIG_AMIGA */


/*** Low level stuff *********************************************************/


#ifdef CONFIG_ATARI

/*
 * Atari (TT/Falcon)
 */

static void *AtaAlloc(unsigned int size, int flags)
{
    int order;
    unsigned int a_size;
    order = 0;
    a_size = PAGE_SIZE;
    while (a_size < size) {
	order++;
	a_size <<= 1;
    }
    return (void *) __get_dma_pages(flags, order);
}

static void AtaFree(void *obj, unsigned int size)
{
    int order;
    unsigned int a_size;
    order = 0;
    a_size = PAGE_SIZE;
    while (a_size < size) {
	order++;
	a_size <<= 1;
    }
    free_pages ((unsigned long) obj, order);
}

static int AtaIrqInit(void)
{
    /* Set up timer A. Timer A
    will receive a signal upon end of playing from the sound
    hardware. Furthermore Timer A is able to count events
    and will cause an interrupt after a programmed number
    of events. So all we need to keep the music playing is
    to provide the sound hardware with new data upon
    an interrupt from timer A. */
    mfp.tim_ct_a = 0;		/* ++roman: Stop timer before programming! */
    mfp.tim_dt_a = 1;		/* Cause interrupt after first event. */
    mfp.tim_ct_a = 8;		/* Turn on event counting. */
    /* Register interrupt handler. */
    add_isr(IRQ_MFP_TIMA, ata_sq_interrupt, IRQ_TYPE_SLOW, NULL, "DMA sound");
    mfp.int_en_a |= 0x20;	/* Turn interrupt on. */
    mfp.int_mk_a |= 0x20;
    return(1);
}


#define TONE_VOXWARE_TO_DB(v) \
	(((v) < 0) ? -12 : ((v) > 100) ? 12 : ((v) - 50) * 6 / 25)
#define TONE_DB_TO_VOXWARE(v) (((v) * 25 + ((v) > 0 ? 5 : -5)) / 6 + 50)


static int AtaSetBass(int bass)
{
    sound.bass = TONE_VOXWARE_TO_DB(bass);
    atari_microwire_cmd(MW_LM1992_BASS(sound.bass));
    return(TONE_DB_TO_VOXWARE(sound.bass));
}


static int AtaSetTreble(int treble)
{
    sound.treble = TONE_VOXWARE_TO_DB(treble);
    atari_microwire_cmd(MW_LM1992_TREBLE(sound.treble));
    return(TONE_DB_TO_VOXWARE(sound.treble));
}



/*
 * TT
 */


static void TTSilence(void)
{
    tt_dmasnd.ctrl = DMASND_CTRL_OFF;
    atari_microwire_cmd(MW_LM1992_PSG_HIGH); /* mix in PSG signal 1:1 */
}


static void TTInit(void)
{
    int mode, i, idx;
    const int freq[4] = {50066, 25033, 12517, 6258};

    /* search a frequency that fits into the allowed error range */

    idx = -1;
    for (i = 0; i < arraysize(freq); i++)
	/* this isn't as much useful for a TT than for a Falcon, but
	 * then it doesn't hurt very much to implement it for a TT too.
	 */
	if ((100 * abs(sound.soft.speed - freq[i]) / freq[i]) < catchRadius)
	    idx = i;
    if (idx > -1) {
	sound.soft.speed = freq[idx];
	sound.trans = &transTTNormal;
    } else
	sound.trans = &transTTExpanding;

    TTSilence();
    sound.hard = sound.soft;

    if (sound.hard.speed > 50066) {
	/* we would need to squeeze the sound, but we won't do that */
	sound.hard.speed = 50066;
	mode = DMASND_MODE_50KHZ;
	sound.trans = &transTTNormal;
    } else if (sound.hard.speed > 25033) {
	sound.hard.speed = 50066;
	mode = DMASND_MODE_50KHZ;
    } else if (sound.hard.speed > 12517) {
	sound.hard.speed = 25033;
	mode = DMASND_MODE_25KHZ;
    } else if (sound.hard.speed > 6258) {
	sound.hard.speed = 12517;
	mode = DMASND_MODE_12KHZ;
    } else {
	sound.hard.speed = 6258;
	mode = DMASND_MODE_6KHZ;
    }

    tt_dmasnd.mode = (sound.hard.stereo ?
		      DMASND_MODE_STEREO : DMASND_MODE_MONO) |
		     DMASND_MODE_8BIT | mode;

    sound.bal = -sound.soft.speed;
}


static int TTSetFormat(int format)
{
    /* TT sound DMA supports only 8bit modes */

    switch (format) {
	case AFMT_QUERY:
	    return(sound.soft.format);
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_S8:
	case AFMT_U8:
	    break;
	default:
	    format = AFMT_S8;
    }

    sound.soft.format = format;
    sound.soft.size = 8;
    if (sound.minDev == SND_DEV_DSP) {
	sound.dsp.format = format;
	sound.dsp.size = 8;
    }
    TTInit();

    return(format);
}


#define VOLUME_VOXWARE_TO_DB(v) \
	(((v) < 0) ? -40 : ((v) > 100) ? 0 : ((v) * 2) / 5 - 40)
#define VOLUME_DB_TO_VOXWARE(v) ((((v) + 40) * 5 + 1) / 2)


static int TTSetVolume(int volume)
{
    sound.volume_left = VOLUME_VOXWARE_TO_DB(volume & 0xff);
    atari_microwire_cmd(MW_LM1992_BALLEFT(sound.volume_left));
    sound.volume_right = VOLUME_VOXWARE_TO_DB((volume & 0xff00) >> 8);
    atari_microwire_cmd(MW_LM1992_BALRIGHT(sound.volume_right));
    return(VOLUME_DB_TO_VOXWARE(sound.volume_left) |
	   (VOLUME_DB_TO_VOXWARE(sound.volume_right) << 8));
}



/*
 * Falcon
 */


static void FalconSilence(void)
{
    /* stop playback, set sample rate 50kHz for PSG sound */
    tt_dmasnd.ctrl = DMASND_CTRL_OFF;
    tt_dmasnd.mode = DMASND_MODE_50KHZ | DMASND_MODE_STEREO | DMASND_MODE_8BIT;
    tt_dmasnd.int_div = 0; /* STE compatible divider */
    tt_dmasnd.int_ctrl = 0x0;
    tt_dmasnd.cbar_src = 0x0000; /* no matrix inputs */
    tt_dmasnd.cbar_dst = 0x0000; /* no matrix outputs */
    tt_dmasnd.dac_src = 1; /* connect ADC to DAC, disconnect matrix */
    tt_dmasnd.adc_src = 3; /* ADC Input = PSG */
}


static void FalconInit(void)
{
    int divider, i, idx;
    const int freq[8] = {49170, 32780, 24585, 19668, 16390, 12292, 9834, 8195};

    /* search a frequency that fits into the allowed error range */

    idx = -1;
    for (i = 0; i < arraysize(freq); i++)
	/* if we will tolerate 3% error 8000Hz->8195Hz (2.38%) would
	 * be playable without expanding, but that now a kernel runtime
	 * option
	 */
	if ((100 * abs(sound.soft.speed - freq[i]) / freq[i]) < catchRadius)
	    idx = i;
    if (idx > -1) {
	sound.soft.speed = freq[idx];
	sound.trans = &transFalconNormal;
    } else
	sound.trans = &transFalconExpanding;

    FalconSilence();
    sound.hard = sound.soft;

    if (sound.hard.size == 16) {
	/* the Falcon can play 16bit samples only in stereo */
	sound.hard.stereo = 1;
    }

    if (sound.hard.speed > 49170) {
	/* we would need to squeeze the sound, but we won't do that */
	sound.hard.speed = 49170;
	divider = 1;
	sound.trans = &transFalconNormal;
    } else if (sound.hard.speed > 32780) {
	sound.hard.speed = 49170;
	divider = 1;
    } else if (sound.hard.speed > 24585) {
	sound.hard.speed = 32780;
	divider = 2;
    } else if (sound.hard.speed > 19668) {
	sound.hard.speed = 24585;
	divider = 3;
    } else if (sound.hard.speed > 16390) {
	sound.hard.speed = 19668;
	divider = 4;
    } else if (sound.hard.speed > 12292) {
	sound.hard.speed = 16390;
	divider = 5;
    } else if (sound.hard.speed > 9834) {
	sound.hard.speed = 12292;
	divider = 7;
    } else if (sound.hard.speed > 8195) {
	sound.hard.speed = 9834;
	divider = 9;
    } else {
	sound.hard.speed = 8195;
	divider = 11;
    }
    tt_dmasnd.int_div = divider;

    /* Setup Falcon sound DMA for playback */
    tt_dmasnd.int_ctrl = 0x4; /* Timer A int at play end */
    tt_dmasnd.track_select = 0x0; /* play 1 track, track 1 */
    tt_dmasnd.cbar_src = 0x0001; /* DMA(25MHz) --> DAC */
    tt_dmasnd.cbar_dst = 0x0000;
    tt_dmasnd.rec_track_select = 0;
    tt_dmasnd.dac_src = 2; /* connect matrix to DAC */
    tt_dmasnd.adc_src = 0; /* ADC Input = Mic */

    tt_dmasnd.mode = (sound.hard.stereo ?
		      DMASND_MODE_STEREO : DMASND_MODE_MONO) |
		     ((sound.hard.size == 8) ?
		       DMASND_MODE_8BIT : DMASND_MODE_16BIT) |
		     DMASND_MODE_6KHZ;

    sound.bal = -sound.soft.speed;
}


static int FalconSetFormat(int format)
{
    int size;
    /* Falcon sound DMA supports 8bit and 16bit modes */

    switch (format) {
	case AFMT_QUERY:
	    return(sound.soft.format);
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
	    size = 8;
	    break;
	case AFMT_S16_BE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_U16_LE:
	    size = 16;
	    break;
	default: /* :-) */
	    size = 8;
	    format = AFMT_S8;
    }

    sound.soft.format = format;
    sound.soft.size = size;
    if (sound.minDev == SND_DEV_DSP) {
	sound.dsp.format = format;
	sound.dsp.size = sound.soft.size;
    }

    FalconInit();

    return(format);
}


/* This is for the Falcon output *attenuation* in 1.5dB steps,
 * i.e. output level from 0 to -22.5dB in -1.5dB steps.
 */
#define VOLUME_VOXWARE_TO_ATT(v) \
	((v) < 0 ? 15 : (v) > 100 ? 0 : 15 - (v) * 3 / 20)
#define VOLUME_ATT_TO_VOXWARE(v) (100 - (v) * 20 / 3)


static int FalconSetVolume(int volume)
{
    sound.volume_left = VOLUME_VOXWARE_TO_ATT(volume & 0xff);
    sound.volume_right = VOLUME_VOXWARE_TO_ATT((volume & 0xff00) >> 8);
    tt_dmasnd.output_atten = sound.volume_left << 8 | sound.volume_right << 4;
    return(VOLUME_ATT_TO_VOXWARE(sound.volume_left) |
	   VOLUME_ATT_TO_VOXWARE(sound.volume_right) << 8);
}


static void ata_sq_play_next_frame(int index)
{
    char *start, *end;

    /* used by AtaPlay() if all doubts whether there really is something
     * to be played are already wiped out.
     */
    start = sq_block_address(sq.front);
    end = start+((sq.count == index) ? sq.rear_size : sq.block_size);
    /* end might not be a legal virtual address. */
    DMASNDSetEnd(VTOP(end - 1) + 1);
    DMASNDSetBase(VTOP(start));
	/* Since only an even number of samples per frame can
	be played, we might lose one byte here. (TO DO) */
    sq.front = (sq.front+1) % sq.max_count;
    sq.playing++;
    tt_dmasnd.ctrl = DMASND_CTRL_ON | DMASND_CTRL_REPEAT;
}


static void AtaPlay(void)
{
    /* ++TeSche: Note that sq.playing is no longer just a flag but holds
     * the number of frames the DMA is currently programmed for instead,
     * may be 0, 1 (currently being played) or 2 (pre-programmed).
     *
     * Changes done to sq.count and sq.playing are a bit more subtle again
     * so now I must admit I also prefer disabling the irq here rather
     * than considering all possible situations. But the point is that
     * disabling the irq doesn't have any bad influence on this version of
     * the driver as we benefit from having pre-programmed the DMA
     * wherever possible: There's no need to reload the DMA at the exact
     * time of an interrupt but only at some time while the pre-programmed
     * frame is playing!
     */
    atari_disable_irq(IRQ_MFP_TIMA);

    if (sq.playing == 2 ||	/* DMA is 'full' */
	sq.count <= 0) {	/* nothing to do */
	atari_enable_irq(IRQ_MFP_TIMA);
	return;
    }

    if (sq.playing == 0) {
	/* looks like there's nothing 'in' the DMA yet, so try
	 * to put two frames into it (at least one is available).
	 */
	if (sq.count == 1 && sq.rear_size < sq.block_size && !sq.syncing) {
	    /* hmmm, the only existing frame is not
	     * yet filled and we're not syncing?
	     */
	    atari_enable_irq(IRQ_MFP_TIMA);
	    return;
	}
	ata_sq_play_next_frame(1);
	if (sq.count == 1) {
	    /* no more frames */
	    atari_enable_irq(IRQ_MFP_TIMA);
	    return;
	}
	if (sq.count == 2 && sq.rear_size < sq.block_size && !sq.syncing) {
	    /* hmmm, there were two frames, but the second
	     * one is not yet filled and we're not syncing?
	     */
	    atari_enable_irq(IRQ_MFP_TIMA);
	    return;
	}
	ata_sq_play_next_frame(2);
    } else {
	/* there's already a frame being played so we may only stuff
	 * one new into the DMA, but even if this may be the last
	 * frame existing the previous one is still on sq.count.
	 */
	if (sq.count == 2 && sq.rear_size < sq.block_size && !sq.syncing) {
	    /* hmmm, the only existing frame is not
	     * yet filled and we're not syncing?
	     */
	    atari_enable_irq(IRQ_MFP_TIMA);
	    return;
	}
	ata_sq_play_next_frame(2);
    }
    atari_enable_irq(IRQ_MFP_TIMA);
}


static void ata_sq_interrupt(int irq, struct pt_regs *fp, void *dummy)
{
#if 0
    /* ++TeSche: if you should want to test this... */
    static int cnt = 0;
    if (sq.playing == 2)
	if (++cnt == 10) {
	    /* simulate losing an interrupt */
	    cnt = 0;
	    return;
	}
#endif

    if (sq.ignore_int && (sound.mach.type == DMASND_FALCON)) {
	/* ++TeSche: Falcon only: ignore first irq because it comes
	 * immediately after starting a frame. after that, irqs come
	 * (almost) like on the TT.
	 */
	sq.ignore_int = 0;
	return;
    }

    if (!sq.playing) {
	/* playing was interrupted and sq_reset() has already cleared
	 * the sq variables, so better don't do anything here.
	 */
	WAKE_UP(sq.sync_queue);
	return;
    }

    /* Probably ;) one frame is finished. Well, in fact it may be that a
     * pre-programmed one is also finished because there has been a long
     * delay in interrupt delivery and we've completely lost one, but
     * there's no way to detect such a situation. In such a case the last
     * frame will be played more than once and the situation will recover
     * as soon as the irq gets through.
     */
    sq.count--;
    sq.playing--;

    if (!sq.playing) {
	tt_dmasnd.ctrl = DMASND_CTRL_OFF;
	sq.ignore_int = 1;
    }

    WAKE_UP(sq.write_queue);
	/* At least one block of the queue is free now
	so wake up a writing process blocked because
	of a full queue. */

    if ((sq.playing != 1) || (sq.count != 1))
	/* We must be a bit carefully here: sq.count indicates the
	 * number of buffers used and not the number of frames to
	 * be played. If sq.count==1 and sq.playing==1 that means
	 * the only remaining frame was already programmed earlier
	 * (and is currently running) so we mustn't call AtaPlay()
	 * here, otherwise we'll play one frame too much.
	 */
	AtaPlay();

    if (!sq.playing) WAKE_UP(sq.sync_queue);
	/* We are not playing after AtaPlay(), so there
	is nothing to play any more. Wake up a process
	waiting for audio output to drain. */
}
#endif /* CONFIG_ATARI */


#ifdef CONFIG_AMIGA

/*
 * Amiga
 */


static void *AmiAlloc(unsigned int size, int flags)
{
    return(amiga_chip_alloc((long)size));
}

static void AmiFree(void *obj, unsigned int size)
{
    amiga_chip_free (obj);
}

static int AmiIrqInit(void)
{
    /* turn off DMA for audio channels */
    custom.dmacon = AMI_AUDIO_OFF;

    /* Register interrupt handler. */
    if (!add_isr(IRQ_AMIGA_AUD0, ami_sq_interrupt, 0, NULL, "DMA sound"))
	panic("Couldn't add audio interrupt");
    return(1);
}


static void AmiSilence(void)
{
    /* turn off DMA for audio channels */
    custom.dmacon = AMI_AUDIO_OFF;
}


static void AmiInit(void)
{
    int period, i;

    AmiSilence();

    if (sound.soft.speed)
	period = amiga_colorclock/sound.soft.speed-1;
    else
	period = amiga_audio_min_period;
    sound.hard = sound.soft;
    sound.trans = &transAmiga;

    if (period < amiga_audio_min_period) {
	/* we would need to squeeze the sound, but we won't do that */
	period = amiga_audio_min_period;
	sound.hard.speed = amiga_colorclock/(period+1);
    } else if (period > 65535) {
	period = 65535;
	sound.hard.speed = amiga_colorclock/(period+1);
    }
    for (i = 0; i < 4; i++)
	custom.aud[i].audper = period;
    amiga_audio_period = period;
}


static int AmiSetFormat(int format)
{
    int size;

    /* Amiga sound DMA supports 8bit and 16bit (pseudo 14 bit) modes */

    switch (format) {
	case AFMT_QUERY:
	    return(sound.soft.format);
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
	    size = 8;
	    break;
	case AFMT_S16_BE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_U16_LE:
	    size = 16;
	    break;
	default: /* :-) */
	    size = 8;
	    format = AFMT_S8;
    }

    sound.soft.format = format;
    sound.soft.size = size;
    if (sound.minDev == SND_DEV_DSP) {
	sound.dsp.format = format;
	sound.dsp.size = sound.soft.size;
    }
    AmiInit();

    return(format);
}


#define VOLUME_VOXWARE_TO_AMI(v) \
	(((v) < 0) ? 0 : ((v) > 100) ? 64 : ((v) * 64)/100)
#define VOLUME_AMI_TO_VOXWARE(v) ((v)*100/64)

static int AmiSetVolume(int volume)
{
    sound.volume_left = VOLUME_VOXWARE_TO_AMI(volume & 0xff);
    custom.aud[0].audvol = sound.volume_left;
    sound.volume_right = VOLUME_VOXWARE_TO_AMI((volume & 0xff00) >> 8);
    custom.aud[1].audvol = sound.volume_right;
    return(VOLUME_AMI_TO_VOXWARE(sound.volume_left) |
	   (VOLUME_AMI_TO_VOXWARE(sound.volume_right) << 8));
}

static int AmiSetTreble(int treble)
{
    sound.treble = treble;
    if (treble > 50)
	ciaa.pra |= 0x02;
    else
	ciaa.pra &= ~0x02;
    return(treble);
}


#define AMI_PLAY_LOADED		1
#define AMI_PLAY_PLAYING	2
#define AMI_PLAY_MASK		3


static void ami_sq_play_next_frame(int index)
{
    u_char *start, *ch0, *ch1, *ch2, *ch3;
    u_long size;

    /* used by AmiPlay() if all doubts whether there really is something
     * to be played are already wiped out.
     */
    start = sq_block_address(sq.front);
    size = (sq.count == index ? sq.rear_size : sq.block_size)>>1;

    if (sound.hard.stereo) {
	ch0 = start;
	ch1 = start+sq.block_size_half;
	size >>= 1;
    } else {
	ch0 = start;
	ch1 = start;
    }
    if (sound.hard.size == 8) {
	custom.aud[0].audlc = (u_short *)ZTWO_PADDR(ch0);
	custom.aud[0].audlen = size;
	custom.aud[1].audlc = (u_short *)ZTWO_PADDR(ch1);
	custom.aud[1].audlen = size;
	custom.dmacon = AMI_AUDIO_8;
    } else {
	size >>= 1;
	custom.aud[0].audlc = (u_short *)ZTWO_PADDR(ch0);
	custom.aud[0].audlen = size;
	custom.aud[1].audlc = (u_short *)ZTWO_PADDR(ch1);
	custom.aud[1].audlen = size;
	if (sound.volume_left == 64 && sound.volume_right == 64) {
	    /* We can play pseudo 14-bit only with the maximum volume */
	    ch3 = ch0+sq.block_size_quarter;
	    ch2 = ch1+sq.block_size_quarter;
	    custom.aud[2].audlc = (u_short *)ZTWO_PADDR(ch2);
	    custom.aud[2].audlen = size;
	    custom.aud[3].audlc = (u_short *)ZTWO_PADDR(ch3);
	    custom.aud[3].audlen = size;
	    custom.dmacon = AMI_AUDIO_14;
	} else
	    custom.dmacon = AMI_AUDIO_8;
    }
    sq.front = (sq.front+1) % sq.max_count;
    sq.playing |= AMI_PLAY_LOADED;
}


static void AmiPlay(void)
{
    int minframes = 1;

    custom.intena = IF_AUD0;

    if (sq.playing & AMI_PLAY_LOADED) {
	/* There's already a frame loaded */
	custom.intena = IF_SETCLR | IF_AUD0;
	return;
    }

    if (sq.playing & AMI_PLAY_PLAYING)
	/* Increase threshold: frame 1 is already being played */
	minframes = 2;

    if (sq.count < minframes) {
	/* Nothing to do */
	custom.intena = IF_SETCLR | IF_AUD0;
	return;
    }

    if (sq.count <= minframes && sq.rear_size < sq.block_size && !sq.syncing) {
	/* hmmm, the only existing frame is not
	 * yet filled and we're not syncing?
	 */
	custom.intena = IF_SETCLR | IF_AUD0;
	return;
    }

    ami_sq_play_next_frame(minframes);

    custom.intena = IF_SETCLR | IF_AUD0;
}


static void ami_sq_interrupt(int irq, struct pt_regs *fp, void *dummy)
{
    int minframes = 1;

    if (!sq.playing) {
	/* Playing was interrupted and sq_reset() has already cleared
	 * the sq variables, so better don't do anything here.
	 */
	WAKE_UP(sq.sync_queue);
	return;
    }

    if (sq.playing & AMI_PLAY_PLAYING) {
	/* We've just finished a frame */
	sq.count--;
	WAKE_UP(sq.write_queue);
    }

    if (sq.playing & AMI_PLAY_LOADED)
	/* Increase threshold: frame 1 is already being played */
	minframes = 2;

    /* Shift the flags */
    sq.playing = (sq.playing<<1) & AMI_PLAY_MASK;

    if (!sq.playing)
	/* No frame is playing, disable audio DMA */
	custom.dmacon = AMI_AUDIO_OFF;

    if (sq.count >= minframes)
    	/* Try to play the next frame */
	AmiPlay();

    if (!sq.playing)
	/* Nothing to play anymore.
	   Wake up a process waiting for audio output to drain. */
	WAKE_UP(sq.sync_queue);
}
#endif /* CONFIG_AMIGA */


/*** Machine definitions *****************************************************/


#ifdef CONFIG_ATARI
static MACHINE machTT = {
    DMASND_TT, AtaAlloc, AtaFree, AtaIrqInit, TTInit, TTSilence, TTSetFormat,
    TTSetVolume, AtaSetBass, AtaSetTreble, AtaPlay
};

static MACHINE machFalcon = {
    DMASND_FALCON, AtaAlloc, AtaFree, AtaIrqInit, FalconInit, FalconSilence,
    FalconSetFormat, FalconSetVolume, AtaSetBass, AtaSetTreble, AtaPlay
};
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
static MACHINE machAmiga = {
    DMASND_AMIGA, AmiAlloc, AmiFree, AmiIrqInit, AmiInit, AmiSilence,
    AmiSetFormat, AmiSetVolume, NULL, AmiSetTreble, AmiPlay
};
#endif /* CONFIG_AMIGA */


/*** Mid level stuff *********************************************************/


static void sound_silence(void)
{
    /* update hardware settings one more */
    (*sound.mach.init)();

    (*sound.mach.silence)();
}


static void sound_init(void)
{
    (*sound.mach.init)();
}


static int sound_set_format(int format)
{
    return(*sound.mach.setFormat)(format);
}


static int sound_set_speed(int speed)
{
    if (speed < 0)
	return(sound.soft.speed);

    sound.soft.speed = speed;
    (*sound.mach.init)();
    if (sound.minDev == SND_DEV_DSP)
	sound.dsp.speed = sound.soft.speed;

    return(sound.soft.speed);
}


static int sound_set_stereo(int stereo)
{
    if (stereo < 0)
	return(sound.soft.stereo);

    stereo = !!stereo;    /* should be 0 or 1 now */

    sound.soft.stereo = stereo;
    if (sound.minDev == SND_DEV_DSP)
	sound.dsp.stereo = stereo;
    (*sound.mach.init)();

    return(stereo);
}


static int sound_set_volume(int volume)
{
    return(*sound.mach.setVolume)(volume);
}


#ifdef CONFIG_ATARI
static int sound_set_bass(int bass)
{
    return(sound.mach.setBass ? (*sound.mach.setBass)(bass) : 50);
}
#endif /* CONFIG_ATARI */


static int sound_set_treble(int treble)
{
    return(sound.mach.setTreble ? (*sound.mach.setTreble)(treble) : 50);
}


static long sound_copy_translate(const u_char *userPtr, long userCount,
				 u_char frame[], long *frameUsed,
				 long frameLeft)
{
    long (*ct_func)(const u_char *, long, u_char *, long *, long) = NULL;

    switch (sound.soft.format) {
	case AFMT_MU_LAW:
	    ct_func = sound.trans->ct_ulaw;
	    break;
	case AFMT_A_LAW:
	    ct_func = sound.trans->ct_alaw;
	    break;
	case AFMT_S8:
	    ct_func = sound.trans->ct_s8;
	    break;
	case AFMT_U8:
	    ct_func = sound.trans->ct_u8;
	    break;
	case AFMT_S16_BE:
	    ct_func = sound.trans->ct_s16be;
	    break;
	case AFMT_U16_BE:
	    ct_func = sound.trans->ct_u16be;
	    break;
	case AFMT_S16_LE:
	    ct_func = sound.trans->ct_s16le;
	    break;
	case AFMT_U16_LE:
	    ct_func = sound.trans->ct_u16le;
	    break;
    }
    if (ct_func)
	return(ct_func(userPtr, userCount, frame, frameUsed, frameLeft));
    else
	return(0);
}


/*
 * /dev/mixer abstraction
 */


#define RECLEVEL_VOXWARE_TO_GAIN(v) \
	((v) < 0 ? 0 : (v) > 100 ? 15 : (v) * 3 / 20)
#define RECLEVEL_GAIN_TO_VOXWARE(v) (((v) * 20 + 2) / 3)


static void mixer_init(void)
{
    mixer.busy = 0;
    sound.treble = 0;
    sound.bass = 0;
    switch (sound.mach.type) {
#ifdef CONFIG_ATARI
	case DMASND_TT:
	    atari_microwire_cmd(MW_LM1992_VOLUME(0));
	    sound.volume_left = 0;
	    atari_microwire_cmd(MW_LM1992_BALLEFT(0));
	    sound.volume_right = 0;
	    atari_microwire_cmd(MW_LM1992_BALRIGHT(0));
	    atari_microwire_cmd(MW_LM1992_TREBLE(0));
	    atari_microwire_cmd(MW_LM1992_BASS(0));
	    break;
	case DMASND_FALCON:
	    sound.volume_left = (tt_dmasnd.output_atten & 0xf00) >> 8;
	    sound.volume_right = (tt_dmasnd.output_atten & 0xf0) >> 4;
	    break;
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
	case DMASND_AMIGA:
	    sound.volume_left = 64;
	    sound.volume_right = 64;
	    custom.aud[0].audvol = sound.volume_left;
	    custom.aud[3].audvol = 1;			/* For pseudo 14bit */
	    custom.aud[1].audvol = sound.volume_right;
	    custom.aud[2].audvol = 1;			/* For pseudo 14bit */
	    sound.treble = 50;
	    break;
#endif /* CONFIG_AMIGA */
    }
}


static int mixer_open(int open_mode)
{
    if (mixer.busy)
	return(-EBUSY);
    mixer.busy = 1;
    return(0);
}


static int mixer_release(void)
{
    mixer.busy = 0;
    return(0);
}


static int mixer_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg)
{
    switch (sound.mach.type) {
#ifdef CONFIG_ATARI
	case DMASND_FALCON:
	    switch (cmd) {
		case SOUND_MIXER_READ_DEVMASK:
		    return(IOCTL_OUT(arg, SOUND_MASK_VOLUME | SOUND_MASK_MIC | SOUND_MASK_SPEAKER));
		case SOUND_MIXER_READ_RECMASK:
		    return(IOCTL_OUT(arg, SOUND_MASK_MIC));
		case SOUND_MIXER_READ_STEREODEVS:
		    return(IOCTL_OUT(arg, SOUND_MASK_VOLUME | SOUND_MASK_MIC));
		case SOUND_MIXER_READ_CAPS:
		    return(IOCTL_OUT(arg, SOUND_CAP_EXCL_INPUT));
		case SOUND_MIXER_READ_VOLUME:
		    return(IOCTL_OUT(arg,
			VOLUME_ATT_TO_VOXWARE(sound.volume_left) |
			VOLUME_ATT_TO_VOXWARE(sound.volume_right) << 8));
		case SOUND_MIXER_WRITE_MIC:
		    tt_dmasnd.input_gain =
			RECLEVEL_VOXWARE_TO_GAIN(IOCTL_IN(arg) & 0xff) << 4 |
			RECLEVEL_VOXWARE_TO_GAIN(IOCTL_IN(arg) >> 8 & 0xff);
		    /* fall thru, return set value */
		case SOUND_MIXER_READ_MIC:
		    return(IOCTL_OUT(arg,
			RECLEVEL_GAIN_TO_VOXWARE(tt_dmasnd.input_gain >> 4 & 0xf) |
			RECLEVEL_GAIN_TO_VOXWARE(tt_dmasnd.input_gain & 0xf) << 8));
		case SOUND_MIXER_READ_SPEAKER:
		    {
			int porta;
			cli();
			sound_ym.rd_data_reg_sel = 14;
			porta = sound_ym.rd_data_reg_sel;
			sti();
			return(IOCTL_OUT(arg, porta & 0x40 ? 0 : 100));
		    }
		case SOUND_MIXER_WRITE_VOLUME:
		    return(IOCTL_OUT(arg, sound_set_volume(IOCTL_IN(arg))));
		case SOUND_MIXER_WRITE_SPEAKER:
		    {
			int porta;
			cli();
			sound_ym.rd_data_reg_sel = 14;
			porta = (sound_ym.rd_data_reg_sel & ~0x40) |
				(IOCTL_IN(arg) < 50 ? 0x40 : 0);
			sound_ym.wd_data = porta;
			sti();
			return(IOCTL_OUT(arg, porta & 0x40 ? 0 : 100));
		    }
	    }
	    break;

	case DMASND_TT:
	    switch (cmd) {
		case SOUND_MIXER_READ_DEVMASK:
		    return(IOCTL_OUT(arg,
			SOUND_MASK_VOLUME | SOUND_MASK_TREBLE | SOUND_MASK_BASS |
			((boot_info.bi_atari.mch_cookie >> 16) == ATARI_MCH_TT ?
			    SOUND_MASK_SPEAKER : 0)));
		case SOUND_MIXER_READ_RECMASK:
		    return(IOCTL_OUT(arg, 0));
		case SOUND_MIXER_READ_STEREODEVS:
		    return(IOCTL_OUT(arg, SOUND_MASK_VOLUME));
		case SOUND_MIXER_READ_VOLUME:
		    return(IOCTL_OUT(arg,
			VOLUME_DB_TO_VOXWARE(sound.volume_left) |
			(VOLUME_DB_TO_VOXWARE(sound.volume_right) << 8)));
		case SOUND_MIXER_READ_BASS:
		    return(IOCTL_OUT(arg, TONE_DB_TO_VOXWARE(sound.bass)));
		case SOUND_MIXER_READ_TREBLE:
		    return(IOCTL_OUT(arg, TONE_DB_TO_VOXWARE(sound.treble)));
		case SOUND_MIXER_READ_SPEAKER:
		    {
			int porta;
			if ((boot_info.bi_atari.mch_cookie >> 16) == ATARI_MCH_TT) {
			    cli();
			    sound_ym.rd_data_reg_sel = 14;
			    porta = sound_ym.rd_data_reg_sel;
			    sti();
			    return(IOCTL_OUT(arg, porta & 0x40 ? 0 : 100));
			} else
			    return(-EINVAL);
		    }
		case SOUND_MIXER_WRITE_VOLUME:
		    return(IOCTL_OUT(arg, sound_set_volume(IOCTL_IN(arg))));
		case SOUND_MIXER_WRITE_BASS:
		    return(IOCTL_OUT(arg, sound_set_bass(IOCTL_IN(arg))));
		case SOUND_MIXER_WRITE_TREBLE:
		    return(IOCTL_OUT(arg, sound_set_treble(IOCTL_IN(arg))));
		case SOUND_MIXER_WRITE_SPEAKER:
		    if ((boot_info.bi_atari.mch_cookie >> 16) == ATARI_MCH_TT) {
			int porta;
			cli();
			sound_ym.rd_data_reg_sel = 14;
			porta = (sound_ym.rd_data_reg_sel & ~0x40) |
				(IOCTL_IN(arg) < 50 ? 0x40 : 0);
			sound_ym.wd_data = porta;
			sti();
			return(IOCTL_OUT(arg, porta & 0x40 ? 0 : 100));
		    } else
			return(-EINVAL);
	    }
	    break;
#endif /* CONFIG_ATARI */

#ifdef CONFIG_AMIGA
	case DMASND_AMIGA:
	    switch (cmd) {
		case SOUND_MIXER_READ_DEVMASK:
		    return(IOCTL_OUT(arg, SOUND_MASK_VOLUME | SOUND_MASK_TREBLE));
		case SOUND_MIXER_READ_RECMASK:
		    return(IOCTL_OUT(arg, 0));
		case SOUND_MIXER_READ_STEREODEVS:
		    return(IOCTL_OUT(arg, SOUND_MASK_VOLUME));
		case SOUND_MIXER_READ_VOLUME:
		    return(IOCTL_OUT(arg,
			VOLUME_AMI_TO_VOXWARE(sound.volume_left) |
			VOLUME_AMI_TO_VOXWARE(sound.volume_right) << 8));
		case SOUND_MIXER_WRITE_VOLUME:
		    return(IOCTL_OUT(arg, sound_set_volume(IOCTL_IN(arg))));
		case SOUND_MIXER_READ_TREBLE:
		    return(IOCTL_OUT(arg, sound.treble));
		case SOUND_MIXER_WRITE_TREBLE:
		    return(IOCTL_OUT(arg, sound_set_treble(IOCTL_IN(arg))));
	    }
	    break;
#endif /* CONFIG_AMIGA */
    }

    return(-EINVAL);
}



/*
 * Sound queue stuff, the heart of the driver
 */


static void sq_init(int numBufs, int bufSize, char **buffers)
{
    sq.max_count = numBufs;
    sq.block_size = bufSize;
    sq.buffers = buffers;

    sq.front = sq.count = 0;
    sq.rear = -1;
    sq.write_queue = sq.open_queue = sq.sync_queue = 0;
    sq.busy = 0;
    sq.syncing = 0;

    sq.playing = 0;

#ifdef CONFIG_ATARI
    sq.ignore_int = 0;
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
    sq.block_size_half = sq.block_size>>1;
    sq.block_size_quarter = sq.block_size_half>>1;
#endif /* CONFIG_AMIGA */

    sound_silence();

    /* whatever you like as startup mode for /dev/dsp,
     * (/dev/audio hasn't got a startup mode). note that
     * once changed a new open() will *not* restore these!
     */
    sound.dsp.format = AFMT_S8;
    sound.dsp.stereo = 0;
    sound.dsp.size = 8;

    /* set minimum rate possible without expanding */
    switch (sound.mach.type) {
#ifdef CONFIG_ATARI
	case DMASND_TT:
	    sound.dsp.speed = 6258;
	    break;
	case DMASND_FALCON:
	    sound.dsp.speed = 8195;
	    break;
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
	case DMASND_AMIGA:
	    sound.dsp.speed = 8000;
	    break;
#endif /* CONFIG_AMIGA */
    }

    /* before the first open to /dev/dsp this wouldn't be set */
    sound.soft = sound.dsp;
}


static void sq_play(void)
{
    (*sound.mach.play)();
}


/* ++TeSche: radically changed this one too */

static int sq_write(const char *src, int uLeft)
{
    int uWritten = 0;
    u_char *dest;
    long uUsed, bUsed, bLeft;

    /* ++TeSche: Is something like this necessary?
     * Hey, that's an honest question! Or does any other part of the
     * filesystem already checks this situation? I really don't know.
     */
    if (uLeft < 1)
	return(0);

    /* The interrupt doesn't start to play the last, incomplete frame.
     * Thus we can append to it without disabling the interrupts! (Note
     * also that sq.rear isn't affected by the interrupt.)
     */

    if (sq.count > 0 && (bLeft = sq.block_size-sq.rear_size) > 0) {
	dest = sq_block_address(sq.rear);
	bUsed = sq.rear_size;
	uUsed = sound_copy_translate(src, uLeft, dest, &bUsed, bLeft);
	src += uUsed;
	uWritten += uUsed;
	uLeft -= uUsed;
	sq.rear_size = bUsed;
    }

    do {
	if (sq.count == sq.max_count) {
	    sq_play();
	    if (NON_BLOCKING(sq.open_mode))
		return(uWritten > 0 ? uWritten : -EAGAIN);
	    SLEEP(sq.write_queue, ONE_SECOND);
	    if (SIGNAL_RECEIVED)
		return(uWritten > 0 ? uWritten : -EINTR);
	}

	/* Here, we can avoid disabling the interrupt by first
	 * copying and translating the data, and then updating
	 * the sq variables. Until this is done, the interrupt
	 * won't see the new frame and we can work on it
	 * undisturbed.
	 */

	dest = sq_block_address((sq.rear+1) % sq.max_count);
	bUsed = 0;
	bLeft = sq.block_size;
	uUsed = sound_copy_translate(src, uLeft, dest, &bUsed, bLeft);
	src += uUsed;
	uWritten += uUsed;
	uLeft -= uUsed;
	if (bUsed) {
	    sq.rear = (sq.rear+1) % sq.max_count;
	    sq.rear_size = bUsed;
	    sq.count++;
	}
    } while (bUsed);   /* uUsed may have been 0 */

    sq_play();

    return(uWritten);
}


static int sq_open(int open_mode)
{
    if (sq.busy) {
	if (NON_BLOCKING(open_mode))
	    return(-EBUSY);
	while (sq.busy) {
	    SLEEP(sq.open_queue, ONE_SECOND);
	    if (SIGNAL_RECEIVED)
		return(-EINTR);
	}
    }
    sq.open_mode = open_mode;
    sq.busy = 1;
#ifdef CONFIG_ATARI
    sq.ignore_int = 1;
#endif /* CONFIG_ATARI */
    return(0);
}


static void sq_reset(void)
{
    sound_silence();
    sq.playing = 0;
    sq.count = 0;
    sq.front = (sq.rear+1) % sq.max_count;
}


static int sq_sync(void)
{
    int rc = 0;

    sq.syncing = 1;
    sq_play();	/* there may be an incomplete frame waiting */

    while (sq.playing) {
	SLEEP(sq.sync_queue, ONE_SECOND);
	if (SIGNAL_RECEIVED) {
	    /* While waiting for audio output to drain, an interrupt occurred.
	       Stop audio output immediately and clear the queue. */
	    sq_reset();
	    rc = -EINTR;
	    break;
	}
    }

    sq.syncing = 0;
    return(rc);
}


static int sq_release(void)
{
    int rc = 0;
    if (sq.busy) {
	rc = sq_sync();
	sq.busy = 0;
	WAKE_UP(sq.open_queue);
	/* Wake up a process waiting for the queue being released.
	   Note: There may be several processes waiting for a call to open()
		 returning. */
    }
    return(rc);
}



/*
 * /dev/sndstat
 */


static void state_init(void)
{
    state.busy = 0;
}


/* state.buf should not overflow! */

static int state_open(int open_mode)
{
    char *buffer = state.buf, *mach = "";
    int len = 0;

    if (state.busy)
	return(-EBUSY);

    state.ptr = 0;
    state.busy = 1;

    switch (sound.mach.type) {
#ifdef CONFIG_ATARI
	case DMASND_TT:
	case DMASND_FALCON:
	    mach = "Atari ";
	    break;
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
	case DMASND_AMIGA:
	    mach = "Amiga ";
	    break;
#endif /* CONFIG_AMIGA */
    }
    len += sprintf(buffer+len, "%sDMA sound driver:\n", mach);

    len += sprintf(buffer+len, "\tsound.format = 0x%x", sound.soft.format);
    switch (sound.soft.format) {
	case AFMT_MU_LAW:
	    len += sprintf(buffer+len, " (mu-law)");
	    break;
	case AFMT_A_LAW:
	    len += sprintf(buffer+len, " (A-law)");
	    break;
	case AFMT_U8:
	    len += sprintf(buffer+len, " (unsigned 8 bit)");
	    break;
	case AFMT_S8:
	    len += sprintf(buffer+len, " (signed 8 bit)");
	    break;
	case AFMT_S16_BE:
	    len += sprintf(buffer+len, " (signed 16 bit big)");
	    break;
	case AFMT_U16_BE:
	    len += sprintf(buffer+len, " (unsigned 16 bit big)");
	    break;
	case AFMT_S16_LE:
	    len += sprintf(buffer+len, " (signed 16 bit little)");
	    break;
	case AFMT_U16_LE:
	    len += sprintf(buffer+len, " (unsigned 16 bit little)");
	    break;
    }
    len += sprintf(buffer+len, "\n");
    len += sprintf(buffer+len, "\tsound.speed = %dHz (phys. %dHz)\n",
		   sound.soft.speed, sound.hard.speed);
    len += sprintf(buffer+len, "\tsound.stereo = 0x%x (%s)\n",
		   sound.soft.stereo, sound.soft.stereo ? "stereo" : "mono");
    switch (sound.mach.type) {
#ifdef CONFIG_ATARI
	case DMASND_TT:
	    len += sprintf(buffer+len, "\tsound.volume_left = %ddB [-40...0]\n",
			   sound.volume_left);
	    len += sprintf(buffer+len, "\tsound.volume_right = %ddB [-40...0]\n",
			   sound.volume_right);
	    len += sprintf(buffer+len, "\tsound.bass = %ddB [-12...+12]\n",
			   sound.bass);
	    len += sprintf(buffer+len, "\tsound.treble = %ddB [-12...+12]\n",
			   sound.treble);
	    break;
	case DMASND_FALCON:
	    len += sprintf(buffer+len, "\tsound.volume_left = %ddB [-22.5...0]\n",
			   sound.volume_left);
	    len += sprintf(buffer+len, "\tsound.volume_right = %ddB [-22.5...0]\n",
			   sound.volume_right);
	    break;
#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
	case DMASND_AMIGA:
	    len += sprintf(buffer+len, "\tsound.volume_left = %d [0...64]\n",
			   sound.volume_left);
	    len += sprintf(buffer+len, "\tsound.volume_right = %d [0...64]\n",
			   sound.volume_right);
	    break;
#endif /* CONFIG_AMIGA */
    }
    len += sprintf(buffer+len, "\tsq.block_size = %d sq.max_count = %d\n",
		   sq.block_size, sq.max_count);
    len += sprintf(buffer+len, "\tsq.count = %d sq.rear_size = %d\n", sq.count,
		   sq.rear_size);
    len += sprintf(buffer+len, "\tsq.playing = %d sq.syncing = %d\n",
		   sq.playing, sq.syncing);
    state.len = len;
    return(0);
}


static int state_release(void)
{
    state.busy = 0;
    return(0);
}


static int state_read(char *dest, int count)
{
    int n = state.len-state.ptr;
    if (n > count)
	n = count;
    if (n <= 0)
	return(0);
    memcpy_tofs(dest, &state.buf[state.ptr], n);
    state.ptr += n;
    return(n);
}



/*** High level stuff ********************************************************/


static int sound_open(struct inode *inode, struct file *file)
{
    int dev = MINOR(inode->i_rdev) & 0x0f;

    switch (dev) {
	case SND_DEV_STATUS:
	    return(state_open(file->f_flags));
	case SND_DEV_CTL:
	    return(mixer_open(file->f_flags));
	case SND_DEV_DSP:
	case SND_DEV_AUDIO:
	    {
		int rc = sq_open(file->f_flags);
		if (rc == 0) {
		    sound.minDev = dev;
		    sound.soft = sound.dsp;
		    sound_init();
		    if (dev == SND_DEV_AUDIO) {
			sound_set_speed(8000);
			sound_set_stereo(0);
			sound_set_format(AFMT_MU_LAW);
		    }
		}
		return(rc);
	    }
	default:
	    return(-ENXIO);
    }
}


static int sound_fsync(struct inode *inode, struct file *filp)
{
    int dev = MINOR(inode->i_rdev) & 0x0f;

    switch (dev) {
	case SND_DEV_STATUS:
	case SND_DEV_CTL:
	    return(0);
	case SND_DEV_DSP:
	case SND_DEV_AUDIO:
	    return(sq_sync());
	default:
	    return(unknown_minor_dev("sound_fsync", dev));
    }
}


static void sound_release(struct inode *inode, struct file *file)
{
    int dev = MINOR(inode->i_rdev);

    switch (dev & 0x0f) {
	case SND_DEV_STATUS: state_release(); return;
	case SND_DEV_CTL: mixer_release(); return;
	case SND_DEV_DSP:
	case SND_DEV_AUDIO:
	    sq_release(); sound.soft = sound.dsp; sound_silence();
	    return;
	default:
	    unknown_minor_dev("sound_release", dev);
    }
}


static int sound_lseek(struct inode *inode, struct file *file, off_t offset,
		       int orig)
{
    return(-EPERM);
}


static int sound_read(struct inode *inode, struct file *file, char *buf,
		      int count)
{
    int dev = MINOR(inode->i_rdev);

    switch (dev & 0x0f) {
	case SND_DEV_STATUS:
	    return(state_read(buf, count));
	case SND_DEV_CTL:
	case SND_DEV_DSP:
	case SND_DEV_AUDIO:
	    return(-EPERM);
	default:
	    return(unknown_minor_dev("sound_read", dev));
    }
}


static int sound_write(struct inode *inode, struct file *file, const char *buf,
		       int count)
{
    int dev = MINOR(inode->i_rdev);

    switch (dev & 0x0f) {
	case SND_DEV_STATUS:
	case SND_DEV_CTL:
	    return(-EPERM);
	case SND_DEV_DSP:
	case SND_DEV_AUDIO:
	    return(sq_write(buf, count));
	default:
	    return(unknown_minor_dev("sound_write", dev));
    }
}


static int ioctl_return(int *addr, int value)
{
    int error;

    if (value < 0)
	return(value);

    error = verify_area(VERIFY_WRITE, addr, sizeof(int));
    if (error)
	return(error);

    put_user(value, addr);
    return(0);
}


static int unknown_minor_dev(char *fname, int dev)
{
    /* printk("%s: Unknown minor device %d\n", fname, dev); */
    return(-ENXIO);
}


static int sound_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg)
{
    int dev = MINOR(inode->i_rdev);
    u_long fmt;

    switch (dev & 0x0f) {
	case SND_DEV_STATUS:
	    return(-EPERM);
	case SND_DEV_CTL:
	    return(mixer_ioctl(inode, file, cmd, arg));
	case SND_DEV_AUDIO:
	case SND_DEV_DSP:
	    switch (cmd) {
		case SNDCTL_DSP_RESET:
		    sq_reset();
		    return(0);
		case SNDCTL_DSP_POST:
		case SNDCTL_DSP_SYNC:
		    return(sound_fsync(inode, file));

		/* ++TeSche: before changing any of these it's probably wise to
		 * wait until sound playing has settled down
		 */
		case SNDCTL_DSP_SPEED:
		    sound_fsync(inode, file);
		    return(IOCTL_OUT(arg, sound_set_speed(IOCTL_IN(arg))));
		case SNDCTL_DSP_STEREO:
		    sound_fsync(inode, file);
		    return(IOCTL_OUT(arg, sound_set_stereo(IOCTL_IN(arg))));
		case SOUND_PCM_WRITE_CHANNELS:
		    sound_fsync(inode, file);
		    return(IOCTL_OUT(arg, sound_set_stereo(IOCTL_IN(arg)-1)+1));
		case SNDCTL_DSP_SETFMT:
		    sound_fsync(inode, file);
		    return(IOCTL_OUT(arg, sound_set_format(IOCTL_IN(arg))));
		case SNDCTL_DSP_GETFMTS:
		    fmt = 0;
		    if (sound.trans) {
			if (sound.trans->ct_ulaw)
			    fmt |= AFMT_MU_LAW;
			if (sound.trans->ct_alaw)
			    fmt |= AFMT_A_LAW;
			if (sound.trans->ct_s8)
			    fmt |= AFMT_S8;
			if (sound.trans->ct_u8)
			    fmt |= AFMT_U8;
			if (sound.trans->ct_s16be)
			    fmt |= AFMT_S16_BE;
			if (sound.trans->ct_u16be)
			    fmt |= AFMT_U16_BE;
			if (sound.trans->ct_s16le)
			    fmt |= AFMT_S16_LE;
			if (sound.trans->ct_u16le)
			    fmt |= AFMT_U16_LE;
		    }
		    return(IOCTL_OUT(arg, fmt));
		case SNDCTL_DSP_GETBLKSIZE:
		    return(IOCTL_OUT(arg, 10240));
		case SNDCTL_DSP_SUBDIVIDE:
		case SNDCTL_DSP_SETFRAGMENT:
		    break;

		default:
		    return(mixer_ioctl(inode, file, cmd, arg));
	    }
	    break;

	default:
	    return(unknown_minor_dev("sound_ioctl", dev));
    }
    return(-EINVAL);
}


static struct file_operations sound_fops =
{
    sound_lseek,
    sound_read,
    sound_write,
    NULL,
    NULL,                      /* select */
    sound_ioctl,
    NULL,
    sound_open,
    sound_release,
    sound_fsync
};



/*** Config & Setup **********************************************************/


void soundcard_init(void)
{
    int has_sound = 0;
    char **buffers;
    int i;

    switch (boot_info.machtype) {
#ifdef CONFIG_ATARI
	case MACH_ATARI:
	    if (ATARIHW_PRESENT(PCM_8BIT)) {
		if (ATARIHW_PRESENT(CODEC))
		    sound.mach = machFalcon;
		else if (ATARIHW_PRESENT(MICROWIRE))
		    sound.mach = machTT;
		else
		    break;
		if ((mfp.int_en_a & mfp.int_mk_a & 0x20) == 0)
		    has_sound = 1;
		else
		    printk("DMA sound driver: Timer A interrupt already in use\n");
	    }
	    break;

#endif /* CONFIG_ATARI */
#ifdef CONFIG_AMIGA
	case MACH_AMIGA:
	    if (AMIGAHW_PRESENT(AMI_AUDIO)) {
		sound.mach = machAmiga;
		has_sound = 1;
	    }
	    break;
#endif /* CONFIG_AMIGA */
    }
    if (!has_sound)
	return;

    /* Set up sound queue, /dev/audio and /dev/dsp. */
    buffers = kmalloc (numBufs * sizeof(char *), GFP_KERNEL);
    if (!buffers) {
    out_of_memory:
	printk("DMA sound driver: Not enough buffer memory, driver disabled!\n");
	return;
    }
    for (i = 0; i < numBufs; i++) {
	buffers[i] = sound.mach.dma_alloc (bufSize << 10, GFP_KERNEL);
	if (!buffers[i]) {
	    while (i--)
		sound.mach.dma_free (buffers[i], bufSize << 10);
	    kfree (buffers);
	    goto out_of_memory;
        }
    }

    /* Register driver with the VFS. */
    register_chrdev(SOUND_MAJOR, "sound", &sound_fops);

    sq_init(numBufs, bufSize << 10, buffers);

    /* Set up /dev/sndstat. */
    state_init();

    /* Set up /dev/mixer. */
    mixer_init();

    if (!sound.mach.irqinit()) {
	printk("DMA sound driver: Interrupt initialization failed\n");
	return;
    }

    printk("DMA sound driver installed, using %d buffers of %dk.\n", numBufs,
	   bufSize);

    return;
}

void sound_setup(char *str, int *ints)
{
    /* ++Martin: stub, could possibly be merged with soundcard.c et al later */
}

void dmasound_setup(char *str, int *ints)
{
    /* check the bootstrap parameter for "dmasound=" */

    switch (ints[0]) {
	case 3:
	    if ((ints[3] < 0) || (ints[3] > MAX_CATCH_RADIUS))
		printk("dmasound_setup: illegal catch radius, using default = %d\n", catchRadius);
	    else
		catchRadius = ints[3];
	    /* fall through */
	case 2:
	    if (ints[1] < MIN_BUFFERS)
		printk("dmasound_setup: illegal number of buffers, using default = %d\n", numBufs);
	    else
		numBufs = ints[1];
	    if (ints[2] < MIN_BUFSIZE || ints[2] > MAX_BUFSIZE)
		printk("dmasound_setup: illegal buffer size, using default = %d\n", bufSize);
	    else
		bufSize = ints[2];
	    break;
	case 0:
	    break;
	default:
	    printk("dmasound_setup: illegal number of arguments\n");
    }
}
