/*
 * linux/arch/m68k/mac/debug.c
 *
 * Shamelessly stolen (SCC code and general framework) from:
 *
 * linux/arch/m68k/atari/debug.c
 *
 * Atari debugging and serial console stuff
 *
 * Assembled of parts of former atari/config.c 97-12-18 by Roman Hodek
 *  
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/delay.h>

#define BOOTINFO_COMPAT_1_0
#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/machw.h>
#include <asm/macints.h>

extern char m68k_debug_device[];

extern struct compat_bootinfo compat_boot_info;

extern unsigned long mac_videobase;
extern unsigned long mac_videodepth;
extern unsigned long mac_rowbytes;

#define DEBUG_SCREEN
#define DEBUG_SERIAL

/*
 * These two auxiliary debug functions should go away ASAP. Only usage: 
 * before the console output is up (after head.S come some other crucial
 * setup routines :-) it permits writing 'data' to the screen as bit patterns
 * (good luck reading those). Helped to figure that the bootinfo contained
 * garbage data on the amount and size of memory chunks ...
 *
 * The 'pos' argument now simply means 'linefeed after print' ...
 */

#ifdef DEBUG_SCREEN
static int peng=0, line=0;
#endif

void mac_debugging_short(int pos, short num)
{
#ifdef DEBUG_SCREEN
	unsigned char *pengoffset;
	unsigned char *pptr;
	int i;
#endif

#ifdef DEBUG_SERIAL
	printk("debug: %d !\n", num);
#endif

#ifdef DEBUG_SCREEN
	if (!MACH_IS_MAC) {
		/* printk("debug: %d !\n", num); */
		return;
	}

	/* calculate current offset */
	pengoffset=(unsigned char *)(mac_videobase+(150+line*2)*mac_rowbytes)
		    +80*peng;
	
	pptr=pengoffset;
	
	for(i=0;i<8*sizeof(short);i++) /* # of bits */
	{
		/*        value        mask for bit i, reverse order */
		*pptr++ = (num & ( 1 << (8*sizeof(short)-i-1) ) ? 0xFF : 0x00);
	}

	peng++;

	if (pos) {
		line++;
		peng = 0;
	}
#endif
}

void mac_debugging_long(int pos, long addr)
{
#ifdef DEBUG_SCREEN
	unsigned char *pengoffset;
	unsigned char *pptr;
	int i;
#endif

#ifdef DEBUG_SERIAL
	printk("debug: #%ld !\n", addr);
#endif

#ifdef DEBUG_SCREEN
	if (!MACH_IS_MAC) {
		/* printk("debug: #%ld !\n", addr); */
		return;
	}
	
	pengoffset=(unsigned char *)(mac_videobase+(150+line*2)*mac_rowbytes)
		    +80*peng;
	
	pptr=pengoffset;
	
	for(i=0;i<8*sizeof(long);i++) /* # of bits */
	{
		*pptr++ = (addr & ( 1 << (8*sizeof(long)-i-1) ) ? 0xFF : 0x00);
	}

	peng++;

	if (pos) {
		line++;
		peng = 0;
	}
#endif
}

/*
 * Penguin - used by head.S console; obsolete
 */
char that_penguin[]={
#include "that_penguin.h"
};

#ifdef DEBUG_SCREEN
/* 
 * B/W version of penguin, unfinished - any takers??
 */
static char bw_penguin[]={
#include "bw_penguin.h"
};
#endif

void mac_debugging_penguin(int peng)
{
#ifdef DEBUG_SCREEN
	unsigned char *pengoffset;
	unsigned char *pptr;
	unsigned char *bwpdptr=bw_penguin;
	int i;
#endif

#ifdef DEBUG_SERIAL
	printk("Penguin: #%d !\n", peng);
#endif

#ifdef DEBUG_SCREEN
	if (!MACH_IS_MAC) 
		return;

	if (mac_videodepth ==1) 
		pengoffset=(unsigned char *)(mac_videobase+80*mac_rowbytes)
			   +5*peng;
	else
		pengoffset=(unsigned char *)(mac_videobase+80*mac_rowbytes)
			   +20*peng;
	
	pptr=pengoffset;
	
	for(i=0;i<36;i++)
	{
		memcpy(pptr,bwpdptr,4);
		bwpdptr+=4;
		pptr+=mac_rowbytes;
	}
#endif
}

#ifdef DEBUG_SCREEN
/*
 * B/W version of flaming Mac, unfinished (see above).
 */
static char bw_kaboom_map[]={
#include "bw_mac.h"
};
#endif

#ifdef DEBUG_SCREEN
static void mac_boom_boom(void)
{
	static unsigned char *boomoffset=NULL;
	unsigned char *pptr;
	unsigned char *bwpdptr=bw_kaboom_map;
	int i;
	
#ifdef DEBUG_SERIAL
	printk("BOOM !\n");
#endif

	if (!MACH_IS_MAC) 
		return;

	if(!boomoffset)
		if (mac_videodepth == 1) {
			boomoffset=(unsigned char *)(mac_videobase+160*mac_rowbytes);
		} else {
			boomoffset=(unsigned char *)(mac_videobase+256*mac_rowbytes);
		}
	else
		if (mac_videodepth == 1)
			boomoffset+=5;
		else
			boomoffset+=32;	

	pptr=boomoffset;
	
	for(i=0;i<36;i++)
	{
		memcpy(pptr,bwpdptr,4);
		bwpdptr+=4;
		pptr+=mac_rowbytes;
	}
}
#endif

void mac_boom(int booms)
{
#ifdef DEBUG_SCREEN
	int i;
#endif

	if (!MACH_IS_MAC) 
		return;

#ifdef DEBUG_SCREEN
	for(i=0;i<booms;i++)
		mac_boom_boom();
	while(1);
#endif
}


#ifdef DEBUG_SERIAL
/*
 * TODO: serial debug code
 */

struct SCC
 {
  u_char cha_b_ctrl;
  u_char char_dummy1;
  u_char cha_a_ctrl;
  u_char char_dummy2;
  u_char cha_b_data;
  u_char char_dummy3;
  u_char cha_a_data;
 };

# define scc (*((volatile struct SCC*)mac_bi_data.sccbase))

/* Flag that serial port is already initialized and used */
int mac_SCC_init_done = 0;
/* Can be set somewhere, if a SCC master reset has already be done and should
 * not be repeated; used by kgdb */
int mac_SCC_reset_done = 0;

static int scc_port = -1;

static struct console mac_console_driver = {
	"debug",
	NULL,			/* write */
	NULL,			/* read */
	NULL,			/* device */
	NULL,			/* wait_key */
	NULL,			/* unblank */
	NULL,			/* setup */
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};

/* Mac: loops_per_sec min. 1900000 ^= .5 us; MFPDELAY was 0.6 us*/

#define uSEC 1

static inline void mac_sccb_out (char c)
{
    int i;
    do {
	for( i = uSEC; i > 0; --i )
		barrier();
    } while (!(scc.cha_b_ctrl & 0x04)); /* wait for tx buf empty */
    for( i = uSEC; i > 0; --i )
	barrier();
    scc.cha_b_data = c;
}

static inline void mac_scca_out (char c)
{
    int i;
    do {
	for( i = uSEC; i > 0; --i )
		barrier();
    } while (!(scc.cha_a_ctrl & 0x04)); /* wait for tx buf empty */
    for( i = uSEC; i > 0; --i )
	barrier();
    scc.cha_a_data = c;
}

void mac_sccb_console_write (struct console *co, const char *str,
			      unsigned int count)
{
    while (count--) {
	if (*str == '\n')
	    mac_sccb_out( '\r' );
	mac_sccb_out( *str++ );
    }
}

void mac_scca_console_write (struct console *co, const char *str,
			      unsigned int count)
{
    while (count--) {
	if (*str == '\n')
	    mac_scca_out( '\r' );
	mac_scca_out( *str++ );
    }
}

#ifdef CONFIG_SERIAL_CONSOLE
int mac_sccb_console_wait_key(struct console *co)
{
    int i;
    do {
	for( i = uSEC; i > 0; --i )
		barrier();
    } while( !(scc.cha_b_ctrl & 0x01) ); /* wait for rx buf filled */
    for( i = uSEC; i > 0; --i )
	barrier();
    return( scc.cha_b_data );
}

int mac_scca_console_wait_key(struct console *co)
{
    int i;
    do {
	for( i = uSEC; i > 0; --i )
		barrier();
    } while( !(scc.cha_a_ctrl & 0x01) ); /* wait for rx buf filled */
    for( i = uSEC; i > 0; --i )
	barrier();
    return( scc.cha_a_data );
}
#endif

/* The following two functions do a quick'n'dirty initialization of the MFP or
 * SCC serial ports. They're used by the debugging interface, kgdb, and the
 * serial console code. */
#define SCCB_WRITE(reg,val)				\
    do {						\
	int i;						\
	scc.cha_b_ctrl = (reg);				\
	for( i = uSEC; i > 0; --i )			\
		barrier();				\
	scc.cha_b_ctrl = (val);				\
	for( i = uSEC; i > 0; --i )			\
		barrier();				\
    } while(0)

#define SCCA_WRITE(reg,val)				\
    do {						\
	int i;						\
	scc.cha_a_ctrl = (reg);				\
	for( i = uSEC; i > 0; --i )			\
		barrier();				\
	scc.cha_a_ctrl = (val);				\
	for( i = uSEC; i > 0; --i )			\
		barrier();				\
    } while(0)

/* loops_per_sec isn't initialized yet, so we can't use udelay(). This does a
 * delay of ~ 60us. */
/* Mac: loops_per_sec min. 1900000 ^= .5 us; MFPDELAY was 0.6 us*/
#define LONG_DELAY()				\
    do {					\
	int i;					\
	for( i = 60*uSEC; i > 0; --i )		\
	    barrier();				\
    } while(0)
    
#ifndef CONFIG_SERIAL_CONSOLE
__initfunc(static void mac_init_scc_port( int cflag, int port ))
#else
void mac_init_scc_port( int cflag, int port )
#endif
{
	extern int mac_SCC_reset_done;

	/*
	 * baud rates: 1200, 1800, 2400, 4800, 9600, 19.2k, 38.4k, 57.6k, 115.2k
	 */

	static int clksrc_table[9] =
		/* reg 11: 0x50 = BRG, 0x00 = RTxC, 0x28 = TRxC */
    		{ 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x00, 0x00 };
	static int clkmode_table[9] =
		/* reg 4: 0x40 = x16, 0x80 = x32, 0xc0 = x64 */
    		{ 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0xc0, 0x80 };
	static int div_table[9] =
		/* reg12 (BRG low) */
    		{ 94, 62, 46, 22, 10, 4, 1, 0, 0 };

    int baud = cflag & CBAUD;
    int clksrc, clkmode, div, reg3, reg5;
    
    if (cflag & CBAUDEX)
	baud += B38400;
    if (baud < B1200 || baud > B38400+2)
	baud = B9600; /* use default 9600bps for non-implemented rates */
    baud -= B1200; /* tables starts at 1200bps */

    clksrc  = clksrc_table[baud];
    clkmode = clkmode_table[baud];
    div     = div_table[baud];

    reg3 = (((cflag & CSIZE) == CS8) ? 0xc0 : 0x40);
    reg5 = (((cflag & CSIZE) == CS8) ? 0x60 : 0x20) | 0x82 /* assert DTR/RTS */;

    if (port == 1) {
	    (void)scc.cha_b_ctrl;	/* reset reg pointer */
	    SCCB_WRITE( 9, 0xc0 );	/* reset */
	    LONG_DELAY();		/* extra delay after WR9 access */
	    SCCB_WRITE( 4, (cflag & PARENB) ? ((cflag & PARODD) ? 0x01 : 0x03) : 0 |
			  0x04 /* 1 stopbit */ |
			  clkmode );
	    SCCB_WRITE( 3, reg3 );
	    SCCB_WRITE( 5, reg5 );
	    SCCB_WRITE( 9, 0 );		/* no interrupts */
	    LONG_DELAY();		/* extra delay after WR9 access */
	    SCCB_WRITE( 10, 0 );	/* NRZ mode */
	    SCCB_WRITE( 11, clksrc );	/* main clock source */
	    SCCB_WRITE( 12, div );	/* BRG value */
	    SCCB_WRITE( 13, 0 );		/* BRG high byte */
	    SCCB_WRITE( 14, 1 );
	    SCCB_WRITE( 3, reg3 | 1 );
	    SCCB_WRITE( 5, reg5 | 8 );
    } else if (port == 0) {
	    (void)scc.cha_a_ctrl;	/* reset reg pointer */
	    SCCA_WRITE( 9, 0xc0 );	/* reset */
	    LONG_DELAY();		/* extra delay after WR9 access */
	    SCCA_WRITE( 4, (cflag & PARENB) ? ((cflag & PARODD) ? 0x01 : 0x03) : 0 |
			  0x04 /* 1 stopbit */ |
			  clkmode );
	    SCCA_WRITE( 3, reg3 );
	    SCCA_WRITE( 5, reg5 );
	    SCCA_WRITE( 9, 0 );		/* no interrupts */
	    LONG_DELAY();		/* extra delay after WR9 access */
	    SCCA_WRITE( 10, 0 );	/* NRZ mode */
	    SCCA_WRITE( 11, clksrc );	/* main clock source */
	    SCCA_WRITE( 12, div );	/* BRG value */
	    SCCA_WRITE( 13, 0 );		/* BRG high byte */
	    SCCA_WRITE( 14, 1 );
	    SCCA_WRITE( 3, reg3 | 1 );
	    SCCA_WRITE( 5, reg5 | 8 );
    }

    mac_SCC_reset_done = 1;
    mac_SCC_init_done = 1;
}
#endif /* DEBUG_SERIAL */

__initfunc(void mac_debug_init(void))
{
#ifdef CONFIG_KGDB
    /* the m68k_debug_device is used by the GDB stub, do nothing here */
    return;
#endif
#ifdef DEBUG_SERIAL
    if (   !strcmp( m68k_debug_device, "ser"  )
        || !strcmp( m68k_debug_device, "ser1" )) {
	/* Mac modem port */
	mac_init_scc_port( B9600|CS8, 0 );
	mac_console_driver.write = mac_scca_console_write;
	mac_console_driver.wait_key = mac_scca_console_wait_key;
	scc_port = 0;
    }
    else if (!strcmp( m68k_debug_device, "ser2" )) {
	/* Mac printer port */
	mac_init_scc_port( B9600|CS8, 1 );
	mac_console_driver.write = mac_sccb_console_write;
	mac_console_driver.wait_key = mac_sccb_console_wait_key;
	scc_port = 1;
    }
    if (mac_console_driver.write)
	register_console(&mac_console_driver);
#endif
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
