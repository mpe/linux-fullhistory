#include <linux/scc.h>

/********* CONFIGURATION PARAMATERES; PLEASE CHANGE THIS TO YOUR OWN SITUATION **********/

/* SCC hardware parameters */

/* use the following board types: 
 *
 *	PA0HZP		OptoSCC (PA0HZP)
 *	EAGLE         	EAGLE
 *	PC100         	PC100 
 *	PRIMUS        	PRIMUS-PC (DG9BL)
 *	DRSI          	DRSI PC*Packet
 *	BAYCOM        	BayCom (U)SCC
 *	
 */

int     Nchips	     = 2	; /* number of chips */
io_port Vector_Latch = 0	; /* addr. of INTACK-Latch (0 for poll mode) */
int     Ivec	     = 7	; /* interrupt vector */
long    Clock	     = 4915200	; /* frequency of the scc clock */
char	Board	     = BAYCOM	; /* what type of SCC card do you use? */
int	Option	     = 0	; /* command for extra hardware */
io_port Special_Port = 0	; /* port address for special hardware */
				  /* (for EAGLE, PC100, PRIMUS, DRSI) */

			/*      ^  never remove the semicolon !! */
			


/* 			Channel    A      B	    Chip	*/
/*			         ============	  ========	*/
/* Control ports:						*/

io_port SCC_ctrl[MAXSCC * 2] = 	{0x304, 0x305,  /* ...one... 	*/
				 0x306, 0x307,  /* ...two...	*/
				     0,     0,  /* ...three...	*/
				     0,     0}; /* ...four...	*/

/* Data ports:							*/

io_port SCC_data[MAXSCC * 2] =  {0x300, 0x301,	/* ...one...	*/
				 0x302, 0x303,	/* ...two...	*/
				     0,     0,	/* ...three...	*/
				     0,     0};	/* ...four...	*/


/* set to '1' if you have and want ESCC chip (8580/85180/85280) support */

/*					      Chip	*/
/*				            ========   	*/
int SCC_Enhanced[MAXSCC] =	{0,	/* ...one...	*/
				 0,  	/* ...two...	*/
				 0,  	/* ...three...	*/
				 0};	/* ...four...	*/

/* some useful #defines. You might need them or not */

#define VERBOSE_BOOTMSG 1
#undef  SCC_DELAY		/* perhaps a 486DX2 is a *bit* too fast */
#undef  SCC_LDELAY		/* slow it even a bit more down */
#undef  DONT_CHECK		/* don't look if the SCCs you specified are available */


/* The external clocking, nrz and fullduplex divider configuration is gone */
/* you can set these parameters in /etc/z8530drv.rc and initialize the  */
/* driver with sccinit */
