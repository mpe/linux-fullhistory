/*
 *  arch/m68k/kernel/kgdb.c  --  Stub for GDB remote debugging protocol
 *
 *  Originally written by Glenn Engel, Lake Stevens Instrument Division
 *
 *  Contributed by HP Systems
 *
 *  Modified for SPARC by Stu Grossman, Cygnus Support.
 *  Modified for Linux/MIPS (and MIPS in general) by Andreas Busse
 *  Modified and extended for Linux/68k by Roman Hodek
 *
 *  Send complaints, suggestions etc. to
 *  <Roman.Hodek@informatik.uni-erlangen.de>
 *
 *  Copyright (C) 1996-97 Roman Hodek
 */

/*
 *  kgdb usage notes:
 *  -----------------
 *
 * If you select CONFIG_KGDB in the configuration, the kernel will be built
 * with different gccc flags: "-g" is added to get debug infos, and
 * "-fomit-frame-pointer" is omitted to make debugging easier. Since the
 * resulting kernel will be quite big (approx. > 7 MB), it will be stripped
 * before compresion. Such a kernel will behave just as usually, except if
 * given a "debug=<device>" command line option. (Only serial devices are
 * allowed for <device>, i.e. no printers or the like; possible values are
 * machine depedend and are the same as for the usual debug device, the one
 * for logging kernel messages.) If that option is given and the device can be
 * initialized, the kernel will connect to the remote gdb in trap_init(). The
 * serial parameters are fixed to 8N1 and 9600bps, for easyness of
 * implementation.
 *
 * Of course, you need a remote machine a suitable gdb there. I.e., it must
 * have support for a m68k-linux target built in. If the remote machine
 * doesn't run Linux/68k itself, you have to build a cross gdb. This is done
 * by
 *    ./configure --target=m68k-linux
 * in the gdb source directory. Until gdb comes with m68k-linux support by
 * default, you have to apply some patches before. The remote debugging
 * protocol itself is always built into gdb anyway, so you don't have to take
 * special care about it.
 *
 * To start a debugging session, start that gdb with the debugging kernel
 * image (the one with the symbols, vmlinux.debug) named on the command line.
 * This file will be used by gdb to get symbol and debugging infos about the
 * kernel. Next, select remote debug mode by
 *    target remote <device>
 * where <device> is the name of the serial device over which the debugged
 * machine is connected. Maybe you have to adjust the baud rate by
 *    set remotebaud <rate>
 * or also other parameters with stty:
 *    shell stty ... </dev/...
 * If the kernel to debug has already booted, it waited for gdb and now
 * connects, and you'll see a breakpoint being reported. If the kernel isn't
 * running yet, start it now. The order of gdb and the kernel doesn't matter.
 * Another thing worth knowing about in the getting-started phase is how to
 * debug the remote protocol itself. This is activated with
 *    set remotedebug 1
 * gdb will then print out each packet sent or received. You'll also get some
 * messages about the gdb stub on the console of the debugged machine.
 *
 * If all that works, you can use lots of the usual debugging techniques on
 * the kernel, e.g. inspecting and changing variables/memory, setting
 * breakpoints, single stepping and so on. It's also possible to interrupt the
 * debugged kernel by pressing C-c in gdb. Have fun! :-)
 *
 * The gdb stub is entered (and thus the remote gdb gets control) in the
 * following situations:
 *
 *  - If breakpoint() is called. This is just after kgdb initialization, or if
 *    a breakpoint() call has been put somewhere into the kernel source.
 *    (Breakpoints can of course also be set the usual way in gdb.)
 *
 *  - If there is a kernel exception, i.e. bad_super_trap() or die_if_kernel()
 *    are entered. All the CPU exceptions are mapped to (more or less..., see
 *    the hard_trap_info array below) appropriate signal, which are reported
 *    to gdb. die_if_kernel() is usually called after some kind of access
 *    error and thus is reported as SIGSEGV.
 *
 *  - When panic() is called. This is reported as SIGABRT.
 *
 *  - If C-c is received over the serial line, which is treated as
 *    SIGINT.
 *
 * Of course, all these signals are just faked for gdb, since there is no
 * signal concept as such for the kernel. It also isn't possible --obviously--
 * to set signal handlers from inside gdb, or restart the kernel with a
 * signal.
 *
 * Current limitations:
 *
 *  - While the kernel is stopped, interrupts are disabled for safety reasons
 *    (i.e., variables not changing magically or the like). But this also
 *    means that the clock isn't running anymore, and that interrupts from the
 *    hardware may get lost/not be served in time. This can cause some device
 *    errors...
 *
 *  - When single-stepping, only one instruction of the current thread is
 *    executed, but interrupts are allowed for that time and will be serviced
 *    if pending. Be prepared for that.
 *
 *  - All debugging happens in kernel virtual address space. There's no way to
 *    access physical memory not mapped in kernel space, or to access user
 *    space. A way to work around this is using get_user_long & Co. in gdb
 *    expressions, but only for the current process.
 *
 *  - Interrupting the kernel only works if interrupts are currently allowed,
 *    and the interrupt of the serial line isn't blocked by some other means
 *    (IPL too high, disabled, ...)
 *
 *  - The gdb stub is currently not reentrant, i.e. errors that happen therein
 *    (e.g. accesing invalid memory) may not be caught correctly. This could
 *    be removed in future by introducing a stack of struct registers.
 *
 */

/*
 *  To enable debugger support, two things need to happen.  One, a
 *  call to kgdb_init() is necessary in order to allow any breakpoints
 *  or error conditions to be properly intercepted and reported to gdb.
 *  (Linux/68k note: Due to the current design, kgdb has to be initialized
 *  after traps and interrupts.)
 *  Two, a breakpoint needs to be generated to begin communication.  This
 *  is most easily accomplished by a call to breakpoint().  Breakpoint()
 *  simulates a breakpoint by executing a TRAP #15 instruction.
 *
 *
 *    The following gdb commands are supported:
 *
 * command          function                               Return value
 *
 *    g             return the value of the CPU registers  hex data or ENN
 *    G             set the value of the CPU registers     OK or ENN
 *
 *    mAA..AA,LLLL  Read LLLL bytes at address AA..AA      hex data or ENN
 *    MAA..AA,LLLL: Write LLLL bytes at address AA.AA      OK or ENN
 *
 *    c             Resume at current address              SNN   ( signal NN)
 *    cAA..AA       Continue at address AA..AA             SNN
 *
 *    s             Step one instruction                   SNN
 *    sAA..AA       Step one instruction from AA..AA       SNN
 *
 *    k             kill
 *
 *    ?             What was the last sigval ?             SNN   (signal NN)
 *
 *    bBB..BB	    Set baud rate to BB..BB		   OK or BNN, then sets
 *							   baud rate
 *
 * All commands and responses are sent with a packet which includes a
 * checksum.  A packet consists of
 *
 * $<packet info>#<checksum>.
 *
 * where
 * <packet info> :: <characters representing the command or response>
 * <checksum>    :: < two hex digits computed as modulo 256 sum of <packetinfo>>
 *
 * When a packet is received, it is first acknowledged with either '+' or '-'.
 * '+' indicates a successful transfer.  '-' indicates a failed transfer.
 *
 * Example:
 *
 * Host:                  Reply:
 * $m0,10#2a               +$00010203040506070809101112131415#42
 *
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/linkage.h>

#include <asm/setup.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/kgdb.h>
#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atariints.h>
#endif
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif


#undef DEBUG

/*
 * global variable: register structure
 */

struct gdb_regs kgdb_registers;

/*
 * serial i/o functions
 */

static int (*serial_out)( unsigned char c );
static unsigned char (*serial_in)( void );
static unsigned char (*serial_intr)( void );

#define	putDebugChar(c)		serial_out(c)
#define	getDebugChar()		serial_in()


/***************************** Prototypes *****************************/

static int hex( unsigned char ch);
static void getpacket( char *buffer);
static void putpacket( char *buffer, int expect_ack);
static inline unsigned long *get_vbr( void );
static int protected_read( char *p, unsigned long *vbr );
static int protected_write( char *p, char val, unsigned long *vbr );
static unsigned char *mem2hex( char *mem, char *buf, int count, int
                               may_fault);
static char *hex2mem( char *buf, char *mem, int count, int may_fault);
static int computeSignal( int tt);
static int hexToInt( char **ptr, int *intValue);
extern asmlinkage void kgdb_intr( int intno, void *data, struct pt_regs *fp );
static asmlinkage void handle_exception( void );
static void show_gdbregs( void );
#ifdef CONFIG_ATARI
static int atari_mfp_out( unsigned char c );
static unsigned char atari_mfp_in( void );
static unsigned char atari_mfp_intr( void );
static int atari_scc_out( unsigned char c );
static unsigned char atari_scc_in( void );
static unsigned char atari_scc_intr( void );
#endif
#ifdef CONFIG_AMIGA
extern int amiga_ser_out( unsigned char c );
extern unsigned char amiga_ser_in( void );
#endif

/************************* End of Prototypes **************************/


int kgdb_initialized = 0;	/* !0 means we've been initialized */

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 2048

static char input_buffer[BUFMAX];
static char output_buffer[BUFMAX];
static const char hexchars[]="0123456789abcdef";
/* debug > 0 prints ill-formed commands in valid packets & checksum errors */ 
static int remote_debug = 0;

/* sizes (in bytes) of CPU stack frames */
static int frame_sizes[16] = {
	8, 8, 12, 12,				/* $0..$3 */
	16, 8, 8, 60,				/* $4..$7 */
	8, 20, 32, 92,				/* $8..$B */
	12, 4, 4, 4					/* $C..$F */
};

/*
 * Convert ch from a hex digit to an int
 */
static int hex(unsigned char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

/*
 * scan for the sequence $<data>#<checksum>
 */
static void getpacket(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int i;
	int count;
	unsigned char ch;

	do {
		/*
		 * wait around for the start character,
		 * ignore all other characters
		 */
		while ((ch = (getDebugChar() & 0x7f)) != '$') ;

		checksum = 0;
		xmitcsum = -1;
		count = 0;
	
		/*
		 * now, read until a # or end of buffer is found
		 */
		while (count < BUFMAX) {
			ch = getDebugChar() & 0x7f;
			if (ch == '#')
				break;
			checksum = checksum + ch;
			buffer[count] = ch;
			count = count + 1;
		}

		if (count >= BUFMAX)
			continue;

		buffer[count] = 0;
#ifdef DEBUG
		printk( "kgdb: received packet %s\n", buffer );
#endif
		
		if (ch == '#') {
			xmitcsum = hex(getDebugChar() & 0x7f) << 4;
			xmitcsum |= hex(getDebugChar() & 0x7f);

			if (checksum != xmitcsum) {
				if (remote_debug)
					printk( "kgdb: bad checksum. count = 0x%x sent=0x%x "
							"buf=%s\n", checksum, xmitcsum, buffer );
				putDebugChar('-');	/* failed checksum */
			}
			else {
				putDebugChar('+'); /* successful transfer */

				/*
				 * if a sequence char is present,
				 * reply the sequence ID
				 */
				if (buffer[2] == ':') {
					putDebugChar(buffer[0]);
					putDebugChar(buffer[1]);

					/*
					 * remove sequence chars from buffer
					 */
					count = strlen(buffer);
					for (i=3; i <= count; i++)
						buffer[i-3] = buffer[i];
				}
			}
		}
	}
	while (checksum != xmitcsum);
}

/*
 * send the packet in buffer.
 */
static void putpacket(char *buffer, int expect_ack)
{
	unsigned char checksum;
	int count;
	unsigned char ch;

	/*
	 * $<packet info>#<checksum>.
	 */

#ifdef DEBUG
	printk( "kgdb: sending packet %s\n", buffer );
#endif
	do {
		putDebugChar('$');
		checksum = 0;
		count = 0;

		while ((ch = buffer[count]) != 0) {
			if (!(putDebugChar(ch)))
				return;
			checksum += ch;
			count += 1;
		}

		putDebugChar('#');
		putDebugChar(hexchars[checksum >> 4]);
		putDebugChar(hexchars[checksum & 0xf]);

	}
	while (expect_ack && (getDebugChar() & 0x7f) != '+');
}


static inline unsigned long *get_vbr( void )

{	unsigned long *vbr;
	
	__asm__ __volatile__ ( "movec	%/vbr,%0" : "=d" (vbr) : );
	return( vbr );
}

static int protected_read( char *p, unsigned long *vbr )

{	unsigned char val;
	int rv;

	__asm__ __volatile__
		( "movel	%3@(8),%/a0\n\t"
		  "movel	#Lberr1,%3@(8)\n\t"
		  "movel	%/sp,%/a1\n\t"
		  "moveq	#1,%1\n\t"
		  "moveb	%2@,%0\n"
		  "nop		\n\t"
		  "moveq	#0,%1\n\t"
	  "Lberr1:\t"
		  "movel	%/a1,%/sp\n\t"
		  "movel	%/a0,%3@(8)"
		  : "=&d" (val), "=&r" (rv)
		  : "a" (p), "a" (vbr)
		  : "a0", "a1" );

	return( rv ? -1 : val );
}

static int protected_write( char *p, char val, unsigned long *vbr )

{	int rv;

	__asm__ __volatile__
		( "movel	%3@(8),%/a0\n\t"
		  "movel	#Lberr2,%3@(8)\n\t"
		  "movel	%/sp,%/a1\n\t"
		  "moveq	#1,%0\n\t"
		  "moveb	%2,%1@\n"
		  "nop		\n\t"
		  "moveq	#0,%0\n\t"
	  "Lberr2:\t"
		  "movel	%/a1,%/sp\n\t"
		  "movel	%/a0,%3@(8)"
		  : "=&r" (rv)
		  : "a" (p), "d" (val), "a" (vbr)
		  : "a0", "a1" );

	return( rv );
}

/*
 * Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null), in case of mem fault,
 * return 0.
 * If MAY_FAULT is non-zero, then we will handle memory faults by returning
 * a 0, else treat a fault like any other fault in the stub.
 */
static unsigned char *mem2hex(char *mem, char *buf, int count, int may_fault)
{
	int ch;
	unsigned long *vbr = get_vbr();
	
	for( ; count-- > 0; ++mem ) {
		if ((ch = protected_read( mem, vbr )) < 0) {
			/* bus error happened */
			if (may_fault)
				return 0;
			else {
				/* ignore, but print a warning */
				printk( "Bus error on read from %p\n", mem );
				ch = 0;
			}
		}
		*buf++ = hexchars[(ch >> 4) & 0xf];
		*buf++ = hexchars[ch & 0xf];
	}

	*buf = 0;

	return buf;
}

/*
 * convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written
 */
static char *hex2mem(char *buf, char *mem, int count, int may_fault)
{
	int i;
	unsigned char ch;
	unsigned long *vbr = get_vbr();

	for( i = 0; i < count; i++, mem++ ) {
		ch = hex(*buf++) << 4;
		ch |= hex(*buf++);
		if (protected_write( mem, ch, vbr )) {
			/* bus error happened */
			if (may_fault)
				return 0;
			else
				/* ignore, but print a warning */
				printk( "Bus error on write to %p\n", mem );
		}
	}

	return mem;
}

/*
 * This table contains the mapping between SPARC hardware trap types, and
 * signals, which are primarily what GDB understands.  It also indicates
 * which hardware traps we need to commandeer when initializing the stub.
 */
static struct hard_trap_info
{
	unsigned char tt;			/* Trap type code for MIPS R3xxx and R4xxx */
	unsigned char signo;		/* Signal that we map this trap into */
} hard_trap_info[] = {
	{ 1,			SIGINT },	/* excep. 1 is used to fake SIGINT */
	{ VEC_BUSERR,	SIGSEGV },	/* bus/access error */
	{ VEC_ADDRERR,	SIGBUS },	/* address error */
	{ VEC_ILLEGAL,	SIGILL },	/* illegal insn */
	{ VEC_ZERODIV,	SIGFPE },	/* (integer) divison by zero */
	{ VEC_CHK,		SIGILL },	/* CHK insn */
	{ VEC_TRAP,		SIGFPE },	/* [F]TRAPcc insn */
	{ VEC_PRIV,		SIGILL },	/* priviledge violation (cannot happen) */
	{ VEC_TRACE,	SIGTRAP },	/* trace trap (single-stepping) */
	{ VEC_LINE10,	SIGILL },	/* A-line insn */
	{ VEC_LINE11,	SIGILL },	/* F-line insn */
	{ VEC_COPROC,	SIGIOT },	/* coprocessor protocol error */
	{ VEC_FORMAT,	SIGIOT },	/* frame format error */
	{ VEC_UNINT,	SIGIOT },	/* uninitialized intr. (should not happen) */
	{ VEC_SYS,		SIGILL },	/* TRAP #0 = system call (illegal in kernel) */
	{ VEC_TRAP1,	SIGILL },	/* TRAP #1 */
	{ VEC_TRAP2,	SIGILL },	/* TRAP #2 */
	{ VEC_TRAP3,	SIGILL },	/* TRAP #3 */
	{ VEC_TRAP4,	SIGILL },	/* TRAP #4 */
	{ VEC_TRAP5,	SIGILL },	/* TRAP #5 */
	{ VEC_TRAP6,	SIGILL },	/* TRAP #6 */
	{ VEC_TRAP7,	SIGILL },	/* TRAP #7 */
	{ VEC_TRAP8,	SIGILL },	/* TRAP #8 */
	{ VEC_TRAP9,	SIGILL },	/* TRAP #9 */
	{ VEC_TRAP10,	SIGILL },	/* TRAP #10 */
	{ VEC_TRAP11,	SIGILL },	/* TRAP #11 */
	{ VEC_TRAP12,	SIGILL },	/* TRAP #12 */
	{ VEC_TRAP13,	SIGILL },	/* TRAP #13 */
	{ VEC_TRAP14,	SIGABRT },	/* TRAP #14 (used by kgdb_abort) */
	{ VEC_TRAP15,	SIGTRAP },	/* TRAP #15 (breakpoint) */
	{ VEC_FPBRUC,	SIGFPE },	/* FPU */
	{ VEC_FPIR,		SIGFPE },	/* FPU */
	{ VEC_FPDIVZ,	SIGFPE },	/* FPU */
	{ VEC_FPUNDER,	SIGFPE },	/* FPU */
	{ VEC_FPOE,		SIGFPE },	/* FPU */
	{ VEC_FPOVER,	SIGFPE },	/* FPU */
	{ VEC_FPNAN,	SIGFPE },	/* FPU */
	{ VEC_FPUNSUP,	SIGFPE },	/* FPU */
	{ VEC_UNIMPEA,	SIGILL },	/* unimpl. effective address */
	{ VEC_UNIMPII,	SIGILL },	/* unimpl. integer insn */

	{ 0, 0 }					/* Must be last */
};


/*
 * Set up exception handlers for tracing and breakpoints
 */
void kgdb_init(void)
{
    extern char m68k_debug_device[];

	/* fake usage to avoid gcc warnings about unused stuff (they're used in
	 * assembler code) The local variables will be optimized away... */
	void (*fake1)(void) = handle_exception;
	int *fake2 = frame_sizes;
	(void)fake1; (void)fake2;
	
	/* We don't modify the real exception vectors here for the m68k.
	 * handle_exception() will be called from bad_kernel_trap() or
	 * die_if_kernel() as needed. */

	/*
	 * Initialize the serial port (name in 'm68k_debug_device')
	 */

	serial_in = NULL;
	serial_out = NULL;
	serial_intr = NULL;
	
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (!strcmp( m68k_debug_device, "ser" )) {
			/* defaults to ser2 for a Falcon and ser1 otherwise */
			strcpy( m68k_debug_device, 
					((atari_mch_cookie>>16) == ATARI_MCH_FALCON) ?
					"ser2" : "ser1" );
		}

		if (!strcmp( m68k_debug_device, "ser1" )) {
			/* ST-MFP Modem1 serial port init */
			mfp.trn_stat  &= ~0x01; /* disable TX */
			mfp.rcv_stat  &= ~0x01; /* disable RX */
			mfp.usart_ctr  = 0x88;  /* clk 1:16, 8N1 */
			mfp.tim_ct_cd &= 0x70;  /* stop timer D */
			mfp.tim_dt_d   = 2;     /* 9600 bps */
			mfp.tim_ct_cd |= 0x01;  /* start timer D, 1:4 */
			mfp.trn_stat  |= 0x01;  /* enable TX */
			mfp.rcv_stat  |= 0x01;  /* enable RX */

			/* set function pointers */
			serial_in = atari_mfp_in;
			serial_out = atari_mfp_out;
			serial_intr = atari_mfp_intr;

			/* allocate interrupt */
			request_irq( IRQ_MFP_RECFULL, kgdb_intr, IRQ_TYPE_FAST, "kgdb",
						 NULL );
	    }
		else if (!strcmp( m68k_debug_device, "ser2" )) {
			extern int atari_SCC_reset_done;

			/* SCC Modem2 serial port init */
			static unsigned char *p, scc_table[] = {
				9, 0xc0,		/* Reset */
				4, 0x44,		/* x16, 1 stopbit, no parity */
				3, 0xc0,		/* receiver: 8 bpc */
				5, 0xe2,		/* transmitter: 8 bpc, assert dtr/rts */
				2, 0x60,		/* base int vector */
				9, 0x09,		/* int enab, with status low */
				10, 0,			/* NRZ */
				11, 0x50,		/* use baud rate generator */
				12, 24, 13, 0,	/* 9600 baud */
				14, 2, 14, 3,	/* use master clock for BRG, enable */
				3, 0xc1,		/* enable receiver */
				5, 0xea,		/* enable transmitter */
				15, 0,			/* no stat ints */
				1, 0x10,		/* Rx int every char, other ints off */
				0
			};
	    
			(void)scc.cha_b_ctrl; /* reset reg pointer */
			MFPDELAY();
			for( p = scc_table; *p != 0; ) {
				scc.cha_b_ctrl = *p++;
				MFPDELAY();
				scc.cha_b_ctrl = *p++;
				MFPDELAY();
				if (p[-2] == 9)
					udelay(40);	/* extra delay after WR9 access */
			}
			/* avoid that atari_SCC.c resets the whole SCC again */
			atari_SCC_reset_done = 1;

			/* set function pointers */
			serial_in = atari_scc_in;
			serial_out = atari_scc_out;
			serial_intr = atari_scc_intr;

			/* allocate rx and spcond ints */
			request_irq( IRQ_SCCB_RX, kgdb_intr, IRQ_TYPE_FAST, "kgdb", NULL );
			request_irq( IRQ_SCCB_SPCOND, kgdb_intr, IRQ_TYPE_FAST, "kgdb",
						 NULL );
		}
	}
#endif
	
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA) {
		/* always use built-in serial port, no init required */
		serial_in = amiga_ser_in;
		serial_out = amiga_ser_out;
	}
#endif

#ifdef CONFIG_ATARI
	if (!serial_in || !serial_out) {
		if (*m68k_debug_device)
			printk( "kgdb_init failed: no valid serial device!\n" );
		else
			printk( "kgdb not enabled\n" );
		return;
	}
#endif

	/*
	 * In case GDB is started before us, ack any packets
	 * (presumably "$?#xx") sitting there.
	 */

	putDebugChar ('+');
	kgdb_initialized = 1;
	printk( KERN_INFO "kgdb initialized.\n" );
}


/*
 * Convert the MIPS hardware trap type code to a unix signal number.
 */
static int computeSignal(int tt)
{
	struct hard_trap_info *ht;

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		if (ht->tt == tt)
			return ht->signo;

	return SIGHUP;		/* default for things we don't know about */
}

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int hexToInt(char **ptr, int *intValue)
{
	int numChars = 0;
	int hexValue;

	*intValue = 0;

	while (**ptr)
	{
		hexValue = hex(**ptr);
		if (hexValue < 0)
			break;

		*intValue = (*intValue << 4) | hexValue;
		numChars ++;

		(*ptr)++;
	}

	return (numChars);
}


/*
 * This assembler stuff copies a struct frame (passed as argument) into struct
 * gdb_regs registers and then calls handle_exception. After return from
 * there, register and the like are restored from 'registers', the stack is
 * set up and execution is continued where registers->pc tells us.
 */

/* offsets in struct frame */
#define FRAMEOFF_D1		"0"		/* d1..d5 */
#define FRAMEOFF_A0		"5*4"	/* a0..a2 */
#define FRAMEOFF_D0		"8*4"
#define FRAMEOFF_SR		"11*4"
#define FRAMEOFF_PC		"11*4+2"
#define FRAMEOFF_VECTOR	"12*4+2"

/* offsets in struct gdb_regs */
#define GDBOFF_D0		"0"
#define GDBOFF_D1		"1*4"
#define GDBOFF_D6		"6*4"
#define GDBOFF_A0		"8*4"
#define GDBOFF_A3		"11*4"
#define GDBOFF_A7		"15*4"
#define GDBOFF_VECTOR	"16*4"
#define GDBOFF_SR		"16*4+2"
#define GDBOFF_PC		"17*4"
#define GDBOFF_FP0		"18*4"
#define GDBOFF_FPCTL	"42*4"

__asm__
( "	.globl " SYMBOL_NAME_STR(enter_kgdb) "\n"
  SYMBOL_NAME_STR(enter_kgdb) ":\n"
  /* return if not initialized */
  "		tstl	"SYMBOL_NAME_STR(kgdb_initialized)"\n"
  "		bne		1f\n"
  "		rts		\n"
  "1:	orw		#0x700,%sr\n"		/* disable interrupts while in stub */
  "		tstl	%sp@+\n"			/* pop off return address */
  "		movel	%sp@+,%a0\n"		/* get pointer to fp->ptregs (param) */
  "		movel	#"SYMBOL_NAME_STR(kgdb_registers)",%a1\n" /* destination */
  /* copy d0-d5/a0-a1 into gdb_regs */
  "		movel	%a0@("FRAMEOFF_D0"),%a1@("GDBOFF_D0")\n"
  "		moveml	%a0@("FRAMEOFF_D1"),%d1-%d5\n"
  "		moveml	%d1-%d5,%a1@("GDBOFF_D1")\n"
  "		moveml	%a0@("FRAMEOFF_A0"),%d0-%d2\n"
  "		moveml	%d0-%d2,%a1@("GDBOFF_A0")\n"
  /* copy sr and pc */
  "		movel	%a0@("FRAMEOFF_PC"),%a1@("GDBOFF_PC")\n"
  "		movew	%a0@("FRAMEOFF_SR"),%a1@("GDBOFF_SR")\n"
  /* copy format/vector word */
  "		movew	%a0@("FRAMEOFF_VECTOR"),%a1@("GDBOFF_VECTOR")\n"
  /* save FPU regs */
  "		fmovemx	%fp0-%fp7,%a1@("GDBOFF_FP0")\n"
  "		fmoveml	%fpcr/%fpsr/%fpiar,%a1@("GDBOFF_FPCTL")\n"

  /* set stack to CPU frame */
  "		addl	#"FRAMEOFF_SR",%a0\n"
  "		movel	%a0,%sp\n"
  "		movew	%sp@(6),%d0\n"
  "		andl	#0xf000,%d0\n"
  "		lsrl	#8,%d0\n"
  "		lsrl	#2,%d0\n"		/* get frame format << 2 */
  "		lea		"SYMBOL_NAME_STR(frame_sizes)",%a2\n"
  "		addl	%a2@(%d0),%sp\n"
  "		movel	%sp,%a1@("GDBOFF_A7")\n" /* save a7 now */

  /* call handle_exception() now that the stack is set up */
  "Lcall_handle_excp:"
  "		jsr		"SYMBOL_NAME_STR(handle_exception)"\n"

  /* after return, first restore FPU registers */
  "		movel	#"SYMBOL_NAME_STR(kgdb_registers)",%a0\n" /* source */
  "		fmovemx	%a0@("GDBOFF_FP0"),%fp0-%fp7\n"
  "		fmoveml	%a0@("GDBOFF_FPCTL"),%fpcr/%fpsr/%fpiar\n"
  /* set new stack pointer */
  "		movel	%a0@("GDBOFF_A7"),%sp\n"
  "		clrw	%sp@-\n"		/* fake format $0 frame */
  "		movel	%a0@("GDBOFF_PC"),%sp@-\n" /* new PC into frame */
  "		movew	%a0@("GDBOFF_SR"),%sp@-\n" /* new SR into frame */
  /* restore general registers */
  "		moveml	%a0@("GDBOFF_D0"),%d0-%d7/%a0-%a6\n"
  /* and jump to new PC */
  "		rte"
  );


/*
 * This is the entry point for the serial interrupt handler. It calls the
 * machine specific function pointer 'serial_intr' to get the char that
 * interrupted. If that was C-c, the stub is entered as above, but based on
 * just a struct intframe, not a struct frame.
 */

__asm__
( SYMBOL_NAME_STR(kgdb_intr) ":\n"
  /* return if not initialized */
  "		tstl	"SYMBOL_NAME_STR(kgdb_initialized)"\n"
  "		bne		1f\n"
  "2:	rts		\n"
  "1:	movel	"SYMBOL_NAME_STR(serial_intr)",%a0\n"
  "		jsr		(%a0)\n"			/* get char from serial */
  "		cmpb	#3,%d0\n"			/* is it C-c ? */
  "		bne		2b\n"				/* no -> just ignore  */
  "		orw		#0x700,%sr\n"		/* disable interrupts */
  "		subql	#1,"SYMBOL_NAME_STR(local_irq_count)"\n"
  "		movel	%sp@(12),%sp\n"		/* revert stack to where 'inthandler' set
									 * it up */
  /* restore regs from frame */
  "		moveml	%sp@+,%d1-%d5/%a0-%a2\n"
  "		movel	%sp@+,%d0\n"
  "		addql	#8,%sp\n"			/* throw away orig_d0 and stkadj */
  /* save them into 'registers' */
  "		moveml	%d0-%d7/%a0-%a6,"SYMBOL_NAME_STR(kgdb_registers)"\n"
  "		movel	#"SYMBOL_NAME_STR(kgdb_registers)",%a1\n" /* destination */
  /* copy sr and pc */
  "		movel	%sp@(2),%a1@("GDBOFF_PC")\n"
  "		movew	%sp@,%a1@("GDBOFF_SR")\n"
  /* fake format 0 and vector 1 (translated to SIGINT) */
  "		movew	#4,%a1@("GDBOFF_VECTOR")\n"
  /* save FPU regs */
  "		fmovemx	%fp0-%fp7,%a1@("GDBOFF_FP0")\n"
  "		fmoveml	%fpcr/%fpsr/%fpiar,%a1@("GDBOFF_FPCTL")\n"
  /* pop off the CPU stack frame */
  "		addql	#8,%sp\n"
  "		movel	%sp,%a1@("GDBOFF_A7")\n" /* save a7 now */
  /* proceed as in enter_kgdb */
  "		jbra	Lcall_handle_excp\n"
  );

/*
 * This function does all command processing for interfacing to gdb.  It
 * returns 1 if you should skip the instruction at the trap address, 0
 * otherwise.
 */
static asmlinkage void handle_exception( void )
{
	int trap;			/* Trap type */
	int sigval;
	int addr;
	int length;
	char *ptr;

	trap = kgdb_registers.vector >> 2;
	sigval = computeSignal(trap);
	/* clear upper half of vector/sr word */
	kgdb_registers.vector = 0;
	kgdb_registers.format = 0;
	
#ifndef DEBUG
	if (remote_debug) {
#endif
		printk("in handle_exception() trap=%d sigval=%d\n", trap, sigval );
		show_gdbregs();
#ifndef DEBUG
	}
#endif

	/*
	 * reply to host that an exception has occurred
	 */
	ptr = output_buffer;

	/*
	 * Send trap type (converted to signal)
	 */
	*ptr++ = 'T';
	*ptr++ = hexchars[sigval >> 4];
	*ptr++ = hexchars[sigval & 0xf];

	/*
	 * Send Error PC
	 */
	*ptr++ = hexchars[GDBREG_PC >> 4];
	*ptr++ = hexchars[GDBREG_PC & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&kgdb_registers.pc, ptr, 4, 0);
	*ptr++ = ';';

	/*
	 * Send frame pointer
	 */
	*ptr++ = hexchars[GDBREG_A6 >> 4];
	*ptr++ = hexchars[GDBREG_A6 & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&kgdb_registers.regs[GDBREG_A6], ptr, 4, 0);
	*ptr++ = ';';

	/*
	 * Send stack pointer
	 */
	*ptr++ = hexchars[GDBREG_SP >> 4];
	*ptr++ = hexchars[GDBREG_SP & 0xf];
	*ptr++ = ':';
	ptr = mem2hex((char *)&kgdb_registers.regs[GDBREG_SP], ptr, 4, 0);
	*ptr++ = ';';

	*ptr++ = 0;
	putpacket(output_buffer,1);	/* send it off... */

	/*
	 * Wait for input from remote GDB
	 */
	for(;;) {
		output_buffer[0] = 0;
		getpacket(input_buffer);

		switch (input_buffer[0]) {
		  case '?':
			output_buffer[0] = 'S';
			output_buffer[1] = hexchars[sigval >> 4];
			output_buffer[2] = hexchars[sigval & 0xf];
			output_buffer[3] = 0;
			break;

		  case 'd':
			/* toggle debug flag */
			remote_debug = !remote_debug;
			break;

			/*
			 * Return the value of the CPU registers
			 */
		  case 'g':
			ptr = output_buffer;
			ptr = mem2hex((char *)&kgdb_registers, ptr, NUMREGSBYTES, 0);
			break;
	  
			/*
			 * set the value of the CPU registers - return OK
			 */
		  case 'G':
			ptr = &input_buffer[1];
			ptr = hex2mem(ptr, (char *)&kgdb_registers, NUMREGSBYTES, 0);
			strcpy(output_buffer,"OK");
			break;

			/*
			 * Pn...=r...    Write register n
			 */
		  case 'P':
			ptr = &input_buffer[1];
			if (hexToInt(&ptr, &addr) && *ptr++ == '=') {
				if (addr >= 0 && addr <= GDBREG_PC)
					hex2mem(ptr, (char *)&kgdb_registers.regs[addr], 4, 0);
				else if (addr >= GDBREG_FP0 && addr <= GDBREG_FP7)
					hex2mem(ptr, (char *)&kgdb_registers.fpregs[addr-GDBREG_FP0],
							12, 0);
				else if (addr >= GDBREG_FPCR && addr <= GDBREG_FPIAR)
					hex2mem(ptr, (char *)&kgdb_registers.fpcntl[addr-GDBREG_FPCR],
							4, 0);
			}
			else
				strcpy(output_buffer,"E01");
			break;
			
			/*
			 * mAA..AA,LLLL  Read LLLL bytes at address AA..AA
			 */
		  case 'm':
			ptr = &input_buffer[1];

			if (hexToInt(&ptr, &addr)
				&& *ptr++ == ','
				&& hexToInt(&ptr, &length)) {
				if (mem2hex((char *)addr, output_buffer, length, 1))
					break;
				strcpy (output_buffer, "E03");
			} else
				strcpy(output_buffer,"E01");
			break;

			/*
			 * MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK
			 */
		  case 'M': 
			ptr = &input_buffer[1];

			if (hexToInt(&ptr, &addr)
				&& *ptr++ == ','
				&& hexToInt(&ptr, &length)
				&& *ptr++ == ':') {
				if (hex2mem(ptr, (char *)addr, length, 1))
					strcpy(output_buffer, "OK");
				else
					strcpy(output_buffer, "E03");
			}
			else
				strcpy(output_buffer, "E02");
			break;

			/*
			 * cAA..AA    Continue at address AA..AA(optional)
			 * sAA..AA    Step one instruction from AA..AA(optional)
			 */
		  case 'c':    
		  case 's':    
			/* try to read optional parameter, pc unchanged if no parm */

			ptr = &input_buffer[1];
			if (hexToInt(&ptr, &addr))
				kgdb_registers.pc = addr;

			kgdb_registers.sr &= 0x7fff;	/* clear Trace bit */
			if (input_buffer[0] == 's')
				kgdb_registers.sr |= 0x8000;	/* set it if step command */
			
			if (remote_debug)
				printk( "cont; new PC=0x%08lx SR=0x%04x\n",
						kgdb_registers.pc, kgdb_registers.sr );
			
			/*
			 * Need to flush the instruction cache here, as we may
			 * have deposited a breakpoint, and the icache probably
			 * has no way of knowing that a data ref to some location
			 * may have changed something that is in the instruction
			 * cache.
			 */

			if (m68k_is040or060)
				__asm__ __volatile__
					( ".word 0xf4f8\n\t" /* CPUSHA I/D */
					  ".word 0xf498" 	 /* CINVA  I */
					);
			else
				__asm__ __volatile__
					( "movec %/cacr,%/d0\n\t"
					  "oriw  #0x0008,%/d0\n\t"
					  "movec %/d0,%/cacr"
					  : : : "d0" );

			return;

			/*
			 * kill the program means reset the machine
			 */
		  case 'k' :
		  case 'r':
			if (mach_reset) {
				/* reply OK before actual reset */
				strcpy(output_buffer,"OK");
				putpacket(output_buffer,0);
				mach_reset();
			}
			else
				strcpy(output_buffer,"E01");
			break;

			/*
			 * Set baud rate (bBB)
			 * FIXME: Needs to be written (in gdb, too...)
			 */
		  case 'b':
			strcpy(output_buffer,"E01");
			break;

		}			/* switch */

		/*
		 * reply to the request
		 */

		putpacket(output_buffer,1);

	}
}


/*
 * Print registers (on target console)
 * Used only to debug the stub...
 */
static void show_gdbregs( void )
{
	printk( "d0: %08lx d1: %08lx d2: %08lx d3: %08lx\n",
			kgdb_registers.regs[0], kgdb_registers.regs[1],
			kgdb_registers.regs[2], kgdb_registers.regs[3] );
	printk( "d4: %08lx d5: %08lx d6: %08lx d7: %08lx\n",
			kgdb_registers.regs[4], kgdb_registers.regs[5],
			kgdb_registers.regs[6], kgdb_registers.regs[7] );
	printk( "a0: %08lx a1: %08lx a2: %08lx a3: %08lx\n",
			kgdb_registers.regs[8], kgdb_registers.regs[9],
			kgdb_registers.regs[10], kgdb_registers.regs[11] );
	printk( "a4: %08lx a5: %08lx a6: %08lx a7: %08lx\n",
			kgdb_registers.regs[12], kgdb_registers.regs[13],
			kgdb_registers.regs[14], kgdb_registers.regs[15] );
	printk( "pc: %08lx sr: %04x\n", kgdb_registers.pc, kgdb_registers.sr );
}


/* -------------------- Atari serial I/O -------------------- */

#ifdef CONFIG_ATARI

static int atari_mfp_out( unsigned char c )

{
    while( !(mfp.trn_stat & 0x80) ) /* wait for tx buf empty */
		barrier();
    mfp.usart_dta = c;
	return( 1 );
}


static unsigned char atari_mfp_in( void )

{
    while( !(mfp.rcv_stat & 0x80) ) /* wait for rx buf filled */
		barrier();
    return( mfp.usart_dta );
}


static unsigned char atari_mfp_intr( void )

{
    return( mfp.usart_dta );
}


static int atari_scc_out( unsigned char c )

{
    do {
		MFPDELAY();
    } while( !(scc.cha_b_ctrl & 0x04) ); /* wait for tx buf empty */
	MFPDELAY();
    scc.cha_b_data = c;
	return( 1 );
}


static unsigned char atari_scc_in( void )

{
    do {
		MFPDELAY();
    } while( !(scc.cha_b_ctrl & 0x01) ); /* wait for rx buf filled */
	MFPDELAY();
    return( scc.cha_b_data );
}


static unsigned char atari_scc_intr( void )

{	unsigned char c, stat;
	
	MFPDELAY();
	scc.cha_b_ctrl = 1; /* RR1 */
	MFPDELAY();
	stat = scc.cha_b_ctrl;
	MFPDELAY();
    c = scc.cha_b_data;
	MFPDELAY();
	if (stat & 0x30) {
		scc.cha_b_ctrl = 0x30; /* error reset for overrun and parity */
		MFPDELAY();
	}
	scc.cha_b_ctrl = 0x38; /* reset highest IUS */
	MFPDELAY();
	return( c );
}

#endif
