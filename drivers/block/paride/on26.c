/* 
        on26.c    (c) 1997  Grant R. Guenther <grant@torque.net>
                            Under the terms of the GNU public license.

        on26.c is a low-level protocol driver for the 
        OnSpec 90c26 parallel to IDE adapter chip.

*/

#define ON26_VERSION      "1.0"

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>

#include "paride.h"

/* mode codes:  0  nybble reads, 8-bit writes
                1  8-bit reads and writes
                2  8-bit EPP mode
		3  EPP-16
		4  EPP-32
*/

#define j44(a,b)  (((a>>4)&0x0f)|(b&0xf0))

#define P1	w2(5);w2(0xd);w2(5);w2(0xd);w2(5);w2(4);
#define P2	w2(5);w2(7);w2(5);w2(4);

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int on26_read_regr( PIA *pi, int cont, int regr )

{       int     a, b, r;

	r = (regr<<2) + 1 + cont;

        switch (pi->mode)  {

        case 0: w0(1); P1; w0(r); P2; w0(0); P1; 
		w2(6); a = r1(); w2(4);
		w2(6); b = r1(); w2(4);
		w2(6); w2(4); w2(6); w2(4);
                return j44(a,b);

        case 1: w0(1); P1; w0(r); P2; w0(0); P1;
		w2(0x26); a = r0(); w2(4); w2(0x26); w2(4);
                return a;

	case 2:
	case 3:
        case 4: w3(1); w3(1); w2(5); w4(r); w2(4);
		w3(0); w3(0); w2(0x24); a = r4(); w2(4);
		w2(0x24); r4(); w2(4);
                return a;

        }
        return -1;
}       

static void on26_write_regr( PIA *pi, int cont, int regr, int val )

{       int  r;

        r = (regr<<2) + 1 + cont;

        switch (pi->mode)  {

        case 0:
        case 1: w0(1); P1; w0(r); P2; w0(0); P1;
		w0(val); P2; w0(val); P2;
		break;

	case 2:
	case 3:
        case 4: w3(1); w3(1); w2(5); w4(r); w2(4);
		w3(0); w3(0); 
		w2(5); w4(val); w2(4);
		w2(5); w4(val); w2(4);
                break;
        }
}

#define  CCP(x)  w0(0xff);w0(0xaa);w0(0x55);w0(0);w0(0xff);\
		 w0(0x87);w0(0x78);w0(x);w2(4);

static void on26_connect ( PIA *pi )

{       int	x;

	pi->saved_r0 = r0();
        pi->saved_r2 = r2();

        CCP(0x20);
	w2(0xcd); w2(0xcc); w0(0xff);
	x = 8; if (pi->mode) x = 9;

	w0(2); P1; w0(8); P2;
	w0(2); P1; w0(x); P2;
}

static void on26_disconnect ( PIA *pi )

{       if (pi->mode >= 2) { w3(4); w3(4); w3(4); w3(4); }
	              else { w0(4); P1; w0(4); P1; }
	CCP(0x30);
        w2(0xcd); w2(0xcc); w0(0xff);
        w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void on26_read_block( PIA *pi, char * buf, int count )

{       int     k, a, b;

        switch (pi->mode) {

        case 0: w0(1); P1; w0(1); P2; w0(2); P1; w0(0x18); P2; w0(0); P1;
		udelay(10);
		for (k=0;k<count;k++) {
                        w2(6); a = r1();
                        w2(4); b = r1();
                        buf[k] = j44(a,b);
                }
		w0(2); P1; w0(8); P2; 
                break;

        case 1: w0(1); P1; w0(1); P2; w0(2); P1; w0(0x19); P2; w0(0); P1;
		udelay(10);
                for (k=0;k<count/2;k++) {
                        w2(0x26); buf[2*k] = r0(); 
			w2(0x24); buf[2*k+1] = r0();
                }
                w0(2); P1; w0(9); P2;
                break;

        case 2: w3(1); w3(1); w2(5); w4(1); w2(4);
		w3(0); w3(0); w2(0x24);
		udelay(10);
                for (k=0;k<count;k++) buf[k] = r4();
                w2(4);
                break;

        case 3: w3(1); w3(1); w2(5); w4(1); w2(4);
                w3(0); w3(0); w2(0x24);
                udelay(10);
                for (k=0;k<count/2;k++) ((u16 *)buf)[k] = r4w();
                w2(4);
                break;

        case 4: w3(1); w3(1); w2(5); w4(1); w2(4);
                w3(0); w3(0); w2(0x24);
                udelay(10);
                for (k=0;k<count/4;k++) ((u32 *)buf)[k] = r4l();
                w2(4);
                break;

        }
}

static void on26_write_block( PIA *pi, char * buf, int count )

{       int	k;

        switch (pi->mode) {

        case 0: 
        case 1: w0(1); P1; w0(1); P2; 
		w0(2); P1; w0(0x18+pi->mode); P2; w0(0); P1;
		udelay(10);
		for (k=0;k<count/2;k++) {
                        w2(5); w0(buf[2*k]); 
			w2(7); w0(buf[2*k+1]);
                }
                w2(5); w2(4);
		w0(2); P1; w0(8+pi->mode); P2;
                break;

        case 2: w3(1); w3(1); w2(5); w4(1); w2(4);
		w3(0); w3(0); w2(0xc5);
		udelay(10);
                for (k=0;k<count;k++) w4(buf[k]);
		w2(0xc4);
                break;

        case 3: w3(1); w3(1); w2(5); w4(1); w2(4);
                w3(0); w3(0); w2(0xc5);
                udelay(10);
                for (k=0;k<count/2;k++) w4w(((u16 *)buf)[k]);
                w2(0xc4);
                break;

        case 4: w3(1); w3(1); w2(5); w4(1); w2(4);
                w3(0); w3(0); w2(0xc5);
                udelay(10);
                for (k=0;k<count/4;k++) w4l(((u32 *)buf)[k]);
                w2(0xc4);
                break;

        }

}

static void on26_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[5] = {"4-bit","8-bit","EPP-8",
				   "EPP-16","EPP-32"};

        printk("%s: on26 %s, OnSpec 90c26 at 0x%x, ",
                pi->device,ON26_VERSION,pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static void on26_inc_use ( void )

{       MOD_INC_USE_COUNT;
}

static void on26_dec_use ( void )

{       MOD_DEC_USE_COUNT;
}

struct pi_protocol on26 = {"on26",0,5,2,1,1,
                           on26_write_regr,
                           on26_read_regr,
                           on26_write_block,
                           on26_read_block,
                           on26_connect,
                           on26_disconnect,
                           0,
                           0,
                           0,
                           on26_log_adapter,
                           on26_inc_use, 
                           on26_dec_use 
                          };


#ifdef MODULE

int     init_module(void)

{       return pi_register( &on26 ) - 1;
}

void    cleanup_module(void)

{       pi_unregister( &on26 );
}

#endif

/* end of on26.c */

