#define FALSE 0
#define TRUE  1
#include <stdarg.h>

extern void cnputc(char c);
char cngetc(void);
int cntstc(void);
void _cnpause(void);
void cnpause(void);
void video_on(void);
int CRT_init(void);
int kbd(int noblock);
int scankbd(void);
static char *_sprintk_ptr;
void kbdreset(void);
int CRT_test(void);
int CRT_putc(int , unsigned char );
/*int CRT_putc(int port, u_char c)*/
int CRT_getc(void);
int _vprintk(   int (*putc)(), const char *fmt0, va_list ap);
static _cvt(unsigned long val, char *buf, long radix, char *digits);
static void cursor(void);
static void initscreen(void );

/*
 * COM1 NS16550 support
 */

struct NS16550
	{
		unsigned char rbr;  /* 0 */
		unsigned char ier;  /* 1 */
		unsigned char fcr;  /* 2 */
		unsigned char lcr;  /* 3 */
		unsigned char mcr;  /* 4 */
		unsigned char lsr;  /* 5 */
		unsigned char msr;  /* 6 */
		unsigned char scr;  /* 7 */
	};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

#define COM1	0x800003F8
#define COM2	0x800002F8

typedef struct NS16550 *NS16550_t;

const NS16550_t COM_PORTS[] = { COM1,COM2};

volatile struct NS16550 *NS16550_init(int chan);
void NS16550_putc(volatile struct NS16550 *com_port, unsigned char c);
unsigned char NS16550_getc(volatile struct NS16550 *com_port);
static _sputc(char c)
{
   *_sprintk_ptr++ = c;
   *_sprintk_ptr = '\0';
}

_sprintk(char *buf, char const *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	_sprintk_ptr = buf;
	ret = _vprintk(_sputc, fmt, ap);
	va_end(ap);
	return (ret);
}

_vsprintk(char *buf, char const *fmt, va_list ap)
{
	int ret;

	_sprintk_ptr = buf;
	ret = _vprintk(_sputc, fmt, ap);
	return (ret);
}

_printk(char const *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = _vprintk(cnputc, fmt, ap);
	va_end(ap);
	return (ret);
}

#define is_digit(c) ((c >= '0') && (c <= '9'))

int _vprintk(   int (*putc)(), const char *fmt0, va_list ap)
{
   char c, sign, *cp;
   int left_prec, right_prec, zero_fill, length, pad, pad_on_right;
   char buf[32];
   long val;
   while (c = *fmt0++)
   {
      if (c == '%')
      {
         c = *fmt0++;
         left_prec = right_prec = pad_on_right = 0;
         if (c == '-')
         {
            c = *fmt0++;
            pad_on_right++;
         }
         if (c == '0')
         {
            zero_fill = TRUE;
            c = *fmt0++;
         } else
         {
            zero_fill = FALSE;
         }
         while (is_digit(c))
         {
            left_prec = (left_prec * 10) + (c - '0');
            c = *fmt0++;
         }
         if (c == '.')
         {
            c = *fmt0++;
            zero_fill++;
            while (is_digit(c))
            {
               right_prec = (right_prec * 10) + (c - '0');
               c = *fmt0++;
            }
         } else
         {
            right_prec = left_prec;
         }
         sign = '\0';
         switch (c)
         {
            case 'd':
            case 'x':
            case 'X':
               val = va_arg(ap, long);
               switch (c)
               {
                  case 'd':
                     if (val < 0)
                     {
                        sign = '-';
                        val = -val;
                     }
                     length = _cvt(val, buf, 10, "0123456789");
                     break;
                  case 'x':
                     length = _cvt(val, buf, 16, "0123456789abcdef");
                     break;
                  case 'X':
                     length = _cvt(val, buf, 16, "0123456789ABCDEF");
                     break;
               }
               cp = buf;
               break;
            case 's':
               cp = va_arg(ap, char *);
               length = strlen(cp);
               break;
            case 'c':
               c = va_arg(ap, long /*char*/);
               (*putc)(c);
               continue;
            default:
               (*putc)('?');
         }
         pad = left_prec - length;
         if (sign != '\0')
         {
            pad--;
         }
         if (zero_fill)
         {
            c = '0';
            if (sign != '\0')
            {
               (*putc)(sign);
               sign = '\0';
            }
         } else
         {
            c = ' ';
         }
         if (!pad_on_right)
         {
            while (pad-- > 0)
            {
               (*putc)(c);
            }
         }
         if (sign != '\0')
         {
            (*putc)(sign);
         }
         while (length-- > 0)
         {
            (*putc)(c = *cp++);
            if (c == '\n')
            {
               (*putc)('\r');
            }
         }
         if (pad_on_right)
         {
            while (pad-- > 0)
            {
               (*putc)(c);
            }
         }
      } else
      {
         (*putc)(c);
         if (c == '\n')
         {
            (*putc)('\r');
         }
      }
   }
}

static _cvt(unsigned long val, char *buf, long radix, char *digits)
{
   char temp[80];
   char *cp = temp;
   int length = 0;
   if (val == 0)
   { /* Special case */
      *cp++ = '0';
   } else
   while (val)
   {
      *cp++ = digits[val % radix];
      val /= radix;
   }
   while (cp != temp)
   {
      *buf++ = *--cp;
      length++;
   }
   *buf = '\0';
   return (length);
}

/*
 * Console I/O interface
 */
typedef const (*proc)();
typedef int dev_t;

#define FALSE 0
#define TRUE  1

#define CRT_PORT 0x3D4  /* Pick one */

static int init = FALSE;
static int is_crt = 0;
static int port = 0;
static int line_num = 0;
#define MAX_LINES 24

char cngetc(void)
{
   int s = _disable_interrupts();
   char c = '\0';
   if (port == CRT_PORT)
   {
/*      c = CRT_getc(port);*/
     c = CRT_getc();
   } else
   if (port)
   {
      c = NS16550_getc((struct NS16550 *)port);
   }
   _enable_interrupts(s);
   return (c);
}

int cntstc(void)
{
   return (0);
}

char _cn_trace[1024];
char *_cnp = _cn_trace;

/*
 * Console kernel output character routine.
 */
void
cnputc(char c)
{
   *_cnp++ = c;
   if (_cnp == &_cn_trace[sizeof(_cn_trace)])
   {
      _cnp = _cn_trace;
   }
   if (!init)
   {
      if (is_crt = CRT_init())
      {
         port = CRT_PORT;
      } else
      {
         port =(int) NS16550_init(0);
      }
      init = TRUE;
   }
   if (port == CRT_PORT)
   {
      CRT_putc(port, c);
   } else
   if (port)
   {
      NS16550_putc((struct NS16550 *)port, c);
   }
   if (c == '\n')
   {
      if (line_num >= 0) line_num++;
   }
   if (c == '\r')
   {
      if (line_num >= MAX_LINES)
      {
         line_num = 0;
         _cnpause();
      }
   }
}

void _cnpause(void)
{
   int c;
   int s = _disable_interrupts();
   _printk("-- More? ");
   while ((c = cngetc()) == 0);
   _printk("\r         \r");  /* Erase prompt */
   if (c == ' ')
   {
      line_num = 0;
   } else
   if (c == 'n')
   {
      line_num = -1;  /* Turn off pause */
   } else
   if ((c == '\r') || (c == '\n'))
   {
      line_num = MAX_LINES-1;
   } else
   if (c == 0x03) /* ^C */
   {
   	abort();
   } else
   {
      line_num = MAX_LINES - (MAX_LINES/3);
   }
   _enable_interrupts(s);
}

void cnpause(void)
{
   int c;
   int s = _disable_interrupts();
   printk("-- More? ");
   while ((c = cngetc()) == 0);
   printk("\r         \r");  /* Erase prompt */
   _enable_interrupts(s);
}

volatile struct NS16550 *NS16550_init(int chan)
{
	volatile struct NS16550 *com_port;
	volatile unsigned char xx;
	com_port = (struct NS16550 *) COM_PORTS[chan];
	/* See if port is present */
	com_port->lcr = 0x00;
	com_port->ier = 0xFF;
#if 0	
	if (com_port->ier != 0x0F) return ((struct NS16550 *)0);
#endif	
	com_port->ier = 0x00;
	com_port->lcr = 0x80;  /* Access baud rate */
	com_port->dll = 12;  /* 9600 baud */
	com_port->dlm = 12 >> 8;
	com_port->lcr = 0x03;  /* 8 data, 1 stop, no parity */
	com_port->mcr = 0x03;  /* RTS/DTR */
	com_port->fcr = 0x07;  /* Clear & enable FIFOs */
	return (com_port);
}


void NS16550_putc(volatile struct NS16550 *com_port, unsigned char c)
{
	volatile int i;
	while ((com_port->lsr & LSR_THRE) == 0) ;
	com_port->thr = c;
}


unsigned char NS16550_getc(volatile struct NS16550 *com_port)
{
	while ((com_port->lsr & LSR_DR) == 0) ;
	return (com_port->rbr);
}

NS16550_test(volatile struct NS16550 *com_port)
{
	return ((com_port->lsr & LSR_DR) != 0);
}

typedef unsigned short u_short;
typedef unsigned char  u_char;

#define	COL		80
#define	ROW		25
#define	CHR		2
#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000
#define ISA_mem		((unsigned char *)0xC0000000)
#define ISA_io		((unsigned char *)0x80000000)

unsigned char background = 0;
unsigned char foreground = 6;

unsigned int addr_6845;
unsigned short *Crtat;
int lastpos;
int scroll;

static void
outb(int port, unsigned char c)
{
	ISA_io[port] = c;
}

static unsigned char
inb(int port)
{
	return (ISA_io[port]);
}

/*
 * The current state of virtual displays
 */
struct screen {
	u_short *cp;		/* the current character address */
	enum state {
		NORMAL,			/* no pending escape */
		ESC,			/* saw ESC */
		EBRAC,			/* saw ESC[ */
		EBRACEQ			/* saw ESC[= */
	} state;		/* command parser state */
	int	cx;		/* the first escape seq argument */
	int	cy;		/* the second escap seq argument */
	int	*accp;		/* pointer to the current processed argument */
	int	row;		/* current column */
	int	so;		/* standout mode */
	u_short color;		/* normal character color */
	u_short color_so;	/* standout color */
	u_short save_color;	/* saved normal color */
	u_short save_color_so;	/* saved standout color */
} screen;

/*
 * Color and attributes for normal, standout and kernel output
 * are stored in the least-significant byte of a u_short
 * so they don't have to be shifted for use.
 * This is all byte-order dependent.
 */
#define	CATTR(x) (x)		/* store color/attributes un-shifted */
#define	ATTR_ADDR(which) (((u_char *)&(which))+1) /* address of attributes */

unsigned short	pccolor;		/* color/attributes for tty output */
unsigned short	pccolor_so;		/* color/attributes, standout mode */

/*
 * cursor() sets an offset (0-1999) into the 80x25 text area   
 */
static void cursor(void)
{
 	int pos = screen.cp - Crtat;

	if (lastpos != pos) {
		outb(addr_6845, 14);
		outb(addr_6845+1, pos >> 8);
		outb(addr_6845, 15);
		outb(addr_6845+1, pos);
		lastpos = pos;
	}
}

static void initscreen(void )
{
	struct screen *d = &screen;

	pccolor = CATTR((background<<4)|foreground);
	pccolor_so = CATTR((foreground<<4)|background);
	d->color = pccolor;
	d->save_color = pccolor;
	d->color_so = pccolor_so;
	d->save_color_so = pccolor_so;
}


#define	wrtchar(c, d) { \
	*(d->cp) = c; \
	d->cp++; \
	d->row++; \
}

fillw(unsigned short val, unsigned short *buf, int num)
{
	/* Need to byte swap value */
	unsigned short tmp;
	tmp = val;
	while (num-- > 0)
	{
		*buf++ = tmp;
	}
}

/*
 * CRT_putc (nee sput) has support for emulation of the 'ibmpc' termcap entry.
 * This is a bare-bones implementation of a bare-bones entry
 * One modification: Change li#24 to li#25 to reflect 25 lines
 * "ca" is the color/attributes value (left-shifted by 8)
 * or 0 if the current regular color for that screen is to be used.
 */
int CRT_putc(int port, unsigned char c)
{
	struct screen *d = &screen;
	u_short *base;
	int i, j;
	u_short *pp;

	base = Crtat;

	switch (d->state) {
	case NORMAL:
		switch (c) {
		case 0x0:		/* Ignore pad characters */
			return;

		case 0x1B:
			d->state = ESC;
			break;

		case '\t':
			do {
				wrtchar(d->color | ' ', d);
			} while (d->row % 8);
			break;

		case '\b':  /* non-destructive backspace */
			if (d->cp > base) {
				d->cp--;
				d->row--;
				if (d->row < 0)
					d->row += COL;	/* prev column */
			}
			break;

		case '\r':
			d->cp -= d->row;
			d->row = 0;
			break;

		case '\n':
			d->cp += COL;
			break;

		case '\007':
			break;

		default:
			if (d->so) {
				wrtchar(d->color_so|(c<<8), d); 
			} else {
				wrtchar(d->color | (c<<8), d); 
			}
			if (d->row >= COL)
				d->row = 0;
			break;
		}
		break;

	case EBRAC:
		/*
		 * In this state, the action at the end of the switch
		 * on the character type is to go to NORMAL state,
		 * and intermediate states do a return rather than break.
		 */
		switch (c) {
		case 'm':
			d->so = d->cx;
			break;

		case 'A': /* back one row */
			if (d->cp >= base + COL)
				d->cp -= COL;
			break;

		case 'B': /* down one row */
			d->cp += COL;
			break;

		case 'C': /* right cursor */
			d->cp++;
			d->row++;
			break;

		case 'D': /* left cursor */
			if (d->cp > base) {
				d->cp--;
				d->row--;
				if (d->row < 0)
					d->row += COL;	/* prev column ??? */
			}
			break;

		case 'J': /* Clear to end of display */
			fillw(d->color|(' '<<8), d->cp, base + COL * ROW - d->cp);
			break;

		case 'K': /* Clear to EOL */
			fillw(d->color|(' '<<8), d->cp, COL - (d->cp - base) % COL);
			break;

		case 'H': /* Cursor move */
			if (d->cx > ROW)
				d->cx = ROW;
			if (d->cy > COL)
				d->cy = COL;
			if (d->cx == 0 || d->cy == 0) {
				d->cp = base;
				d->row = 0;
			} else {
				d->cp = base + (d->cx - 1) * COL + d->cy - 1;
				d->row = d->cy - 1;
			}
			break;

		case '_': /* set cursor */
			if (d->cx)
				d->cx = 1;		/* block */
			else
				d->cx = 12;	/* underline */
			outb(addr_6845, 10);
			outb(addr_6845+1, d->cx);
			outb(addr_6845, 11);
			outb(addr_6845+1, 13);
			break;

		case ';': /* Switch params in cursor def */
			d->accp = &d->cy;
			return;

		case '=': /* ESC[= color change */
			d->state = EBRACEQ;
			return;

		case 'L':	/* Insert line */
			i = (d->cp - base) / COL;
			/* avoid deficiency of bcopy implementation */
			pp = base + COL * (ROW-2);
			for (j = ROW - 1 - i; j--; pp -= COL)
				bcopy(pp, pp + COL, COL * CHR);
			fillw(d->color|(' '<<8), base + i * COL, COL);
			break;
			
		case 'M':	/* Delete line */
			i = (d->cp - base) / COL;
			pp = base + i * COL;
			bcopy(pp + COL, pp, (ROW-1 - i)*COL*CHR);
			fillw(d->color|(' '<<8), base + COL * (ROW - 1), COL);
			break;

		default: /* Only numbers valid here */
			if ((c >= '0') && (c <= '9')) {
				*(d->accp) *= 10;
				*(d->accp) += c - '0';
				return;
			} else
				break;
		}
		d->state = NORMAL;
		break;

	case EBRACEQ: {
		/*
		 * In this state, the action at the end of the switch
		 * on the character type is to go to NORMAL state,
		 * and intermediate states do a return rather than break.
		 */
		u_char *colp;

		/*
		 * Set foreground/background color
		 * for normal mode, standout mode
		 * or kernel output.
		 * Based on code from kentp@svmp03.
		 */
		switch (c) {
		case 'F':
			colp = ATTR_ADDR(d->color);
	do_fg:
			*colp = (*colp & 0xf0) | (d->cx);
			break;

		case 'G':
			colp = ATTR_ADDR(d->color);
	do_bg:
			*colp = (*colp & 0xf) | (d->cx << 4);
			break;

		case 'H':
			colp = ATTR_ADDR(d->color_so);
			goto do_fg;

		case 'I':
			colp = ATTR_ADDR(d->color_so);
			goto do_bg;

		case 'S':
			d->save_color = d->color;
			d->save_color_so = d->color_so;
			break;

		case 'R':
			d->color = d->save_color;
			d->color_so = d->save_color_so;
			break;

		default: /* Only numbers valid here */
			if ((c >= '0') && (c <= '9')) {
				d->cx *= 10;
				d->cx += c - '0';
				return;
			} else
				break;
		}
		d->state = NORMAL;
	    }
	    break;

	case ESC:
		switch (c) {
		case 'c':	/* Clear screen & home */
			fillw(d->color|(' '<<8), base, COL * ROW);
			d->cp = base;
			d->row = 0;
			d->state = NORMAL;
			break;
		case '[':	/* Start ESC [ sequence */
			d->state = EBRAC;
			d->cx = 0;
			d->cy = 0;
			d->accp = &d->cx;
			break;
		default: /* Invalid, clear state */
			d->state = NORMAL;
			break;
		}
		break;
	}
	if (d->cp >= base + (COL * ROW)) { /* scroll check */
		bcopy(base + COL, base, COL * (ROW - 1) * CHR);
		fillw(d->color|(' '<<8), base + COL * (ROW - 1), COL);
		d->cp -= COL;
	}	
	cursor();
}

void video_on(void)
{ /* Enable video */
	outb(0x3C4, 0x01);
	outb(0x3C5, inb(0x3C5)&~20);
}

int CRT_init(void)
{
	unsigned long *PCI_base = (unsigned long *)0x80808010;  /* Magic */
	struct screen *d = &screen;
	if (*PCI_base)
	{ /* No CRT configured */
		return (0);
	}
	video_on();
	d->cp = Crtat = (u_short *)&ISA_mem[0x0B8000];
	addr_6845 = CGA_BASE;
	initscreen();
	fillw(pccolor|(' '<<8), d->cp, COL * ROW);
	return (1);
}

/* Keyboard handler */

#define	L		0x0001	/* locking function */
#define	SHF		0x0002	/* keyboard shift */
#define	ALT		0x0004	/* alternate shift -- alternate chars */
#define	NUM		0x0008	/* numeric shift  cursors vs. numeric */
#define	CTL		0x0010	/* control shift  -- allows ctl function */
#define	CPS		0x0020	/* caps shift -- swaps case of letter */
#define	ASCII		0x0040	/* ascii code for this key */
#define	STP		0x0080	/* stop output */
#define	FUNC		0x0100	/* function key */
#define	SCROLL		0x0200	/* scroll lock key */

/* #include "pcconstab.US" */
/*	BSDI $Id: pcconstab.US,v 1.1.1.1 1994/03/31 13:29:09 gary Exp $	*/
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from @(#)pccons.c	5.11 (Berkeley) 5/21/91
 */

/*
 *	US Keyboard mapping tables
 */

const unsigned short action[] = {
	0,     ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan  0- 7 */
	ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan  8-15 */
	ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan 16-23 */
	ASCII, ASCII, ASCII, ASCII, ASCII,   CTL, ASCII, ASCII,	/* scan 24-31 */
	ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan 32-39 */
	ASCII, ASCII,   SHF, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan 40-47 */
	ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,   SHF, ASCII,	/* scan 48-55 */
	  ALT, ASCII,   CPS,  FUNC,  FUNC,  FUNC,  FUNC,  FUNC,	/* scan 56-63 */
	 FUNC,  FUNC,  FUNC,  FUNC,  FUNC,   NUM,SCROLL, ASCII,	/* scan 64-71 */
	ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII, ASCII,	/* scan 72-79 */
	ASCII, ASCII, ASCII, ASCII,     0,     0,     0,     0,	/* scan 80-87 */
	0,0,0,0,0,0,0,0,	/* scan 88-95 */
	0,0,0,0,0,0,0,0,	/* scan 96-103 */
	0,0,0,0,0,0,0,0,	/* scan 104-111 */
	0,0,0,0,0,0,0,0,	/* scan 112-119 */
	0,0,0,0,0,0,0,0,	/* scan 120-127 */
};

const unsigned char unshift[] = {	/* no shift */
	  0,   033,   '1',   '2',   '3',   '4',   '5',   '6',	/* scan  0- 7 */
	'7',   '8',   '9',   '0',   '-',   '=',   010,  '\t',	/* scan  8-15 */
	'q',   'w',   'e',   'r',   't',   'y',   'u',   'i',	/* scan 16-23 */
	'o',   'p',   '[',   ']',  '\r',   CTL,   'a',   's',	/* scan 24-31 */
	'd',   'f',   'g',   'h',   'j',   'k',   'l',   ';',	/* scan 32-39 */
	'\'',  '`',   SHF,   '\\',  'z',   'x',   'c',   'v',	/* scan 40-47 */
	'b',   'n',   'm',   ',',   '.',   '/',   SHF,   '*',	/* scan 48-55 */
	ALT,   ' ',   CPS,     1,     2,     3,     4,     5,	/* scan 56-63 */
	  6,     7,     8,     9,    10,   NUM,   STP,   '7',	/* scan 64-71 */
	'8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',	/* scan 72-79 */
	'2',   '3',   '0',   0177,    0,     0,     0,     0,	/* scan 80-87 */
	0,0,0,0,0,0,0,0,	/* scan 88-95 */
	0,0,0,0,0,0,0,0,	/* scan 96-103 */
	0,0,0,0,0,0,0,0,	/* scan 104-111 */
	0,0,0,0,0,0,0,0,	/* scan 112-119 */
	0,0,0,0,0,0,0,0,	/* scan 120-127 */
};

const unsigned char shift[] = {	/* shift shift */
	  0,   033,   '!',   '@',   '#',   '$',   '%',   '^',	/* scan  0- 7 */
	'&',   '*',   '(',   ')',   '_',   '+',   010,  '\t',	/* scan  8-15 */
	'Q',   'W',   'E',   'R',   'T',   'Y',   'U',   'I',	/* scan 16-23 */
	'O',   'P',   '{',   '}',  '\r',   CTL,   'A',   'S',	/* scan 24-31 */
	'D',   'F',   'G',   'H',   'J',   'K',   'L',   ':',	/* scan 32-39 */
	'"',   '~',   SHF,   '|',   'Z',   'X',   'C',   'V',	/* scan 40-47 */
	'B',   'N',   'M',   '<',   '>',   '?',   SHF,   '*',	/* scan 48-55 */
	ALT,   ' ',   CPS,     0,     0,   ' ',     0,     0,	/* scan 56-63 */
	  0,     0,     0,     0,     0,   NUM,   STP,   '7',	/* scan 64-71 */
	'8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',	/* scan 72-79 */
	'2',   '3',   '0',  0177,     0,     0,     0,     0,	/* scan 80-87 */
	0,0,0,0,0,0,0,0,	/* scan 88-95 */
	0,0,0,0,0,0,0,0,	/* scan 96-103 */
	0,0,0,0,0,0,0,0,	/* scan 104-111 */
	0,0,0,0,0,0,0,0,	/* scan 112-119 */
	0,0,0,0,0,0,0,0,	/* scan 120-127 */
};

const unsigned char ctl[] = {	/* CTL shift */
	  0,   033,   '!',   000,   '#',   '$',   '%',   036,	/* scan  0- 7 */
	'&',   '*',   '(',   ')',   037,   '+',   034,'\177',	/* scan  8-15 */
	021,   027,   005,   022,   024,   031,   025,   011,	/* scan 16-23 */
	017,   020,   033,   035,  '\r',   CTL,   001,   023,	/* scan 24-31 */
	004,   006,   007,   010,   012,   013,   014,   ';',	/* scan 32-39 */
	'\'',  '`',   SHF,   034,   032,   030,   003,   026,	/* scan 40-47 */
	002,   016,   015,   '<',   '>',   '?',   SHF,   '*',	/* scan 48-55 */
	ALT,   ' ',   CPS,     0,     0,   ' ',     0,     0,	/* scan 56-63 */
	CPS,     0,     0,     0,     0,     0,     0,     0,	/* scan 64-71 */
	  0,     0,     0,     0,     0,     0,     0,     0,	/* scan 72-79 */
	  0,     0,     0,  0177,     0,     0,     0,     0,	/* scan 80-87 */
	  0,     0,   033,   '7',   '4',   '1',     0,   NUM,	/* scan 88-95 */
	'8',   '5',   '2',     0,   STP,   '9',   '6',   '3',	/* scan 96-103*/
	'.',     0,   '*',   '-',   '+',     0,     0,     0,	/*scan 104-111*/
	0,0,0,0,0,0,0,0,	/* scan 112-119 */
	0,0,0,0,0,0,0,0,	/* scan 120-127 */
};


unsigned char shfts, ctls, alts, caps, num, stp;

#define	KBDATAP		0x60	/* kbd data port */
#define	KBSTATUSPORT	0x61	/* kbd status */
#define	KBSTATP		0x64	/* kbd status port */
#define	KBINRDY		0x01
#define	KBOUTRDY	0x02

#define _x__ 0x00  /* Unknown / unmapped */

const unsigned char keycode[] = {
	_x__, 0x43, 0x41, 0x3F, 0x3D, 0x3B, 0x3C, _x__, /* 0x00-0x07 */
	_x__, 0x44, 0x42, 0x40, 0x3E, 0x0F, 0x29, _x__, /* 0x08-0x0F */
	_x__, 0x38, 0x2A, _x__, 0x1D, 0x10, 0x02, _x__, /* 0x10-0x17 */
	_x__, _x__, 0x2C, 0x1F, 0x1E, 0x11, 0x03, _x__, /* 0x18-0x1F */
	_x__, 0x2E, 0x2D, 0x20, 0x12, 0x05, 0x04, _x__, /* 0x20-0x27 */
	_x__, 0x39, 0x2F, 0x21, 0x14, 0x13, 0x06, _x__, /* 0x28-0x2F */
	_x__, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, _x__, /* 0x30-0x37 */
	_x__, _x__, 0x32, 0x24, 0x16, 0x08, 0x09, _x__, /* 0x38-0x3F */
	_x__, 0x33, 0x25, 0x17, 0x18, 0x0B, 0x0A, _x__, /* 0x40-0x47 */
	_x__, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0C, _x__, /* 0x48-0x4F */
	_x__, _x__, 0x28, _x__, 0x1A, 0x0D, _x__, _x__, /* 0x50-0x57 */
	0x3A, 0x36, 0x1C, 0x1B, _x__, 0x2B, _x__, _x__, /* 0x58-0x5F */
	_x__, _x__, _x__, _x__, _x__, _x__, 0x0E, _x__, /* 0x60-0x67 */
	_x__, 0x4F, _x__, 0x4B, 0x47, _x__, _x__, _x__, /* 0x68-0x6F */
	0x52, 0x53, 0x50, 0x4C, 0x4D, 0x48, 0x01, 0x45, /* 0x70-0x77 */
	_x__, 0x4E, 0x51, 0x4A, _x__, 0x49, 0x46, 0x54, /* 0x78-0x7F */
};

int kbd(int noblock)
{
	unsigned char dt, brk, act;
	int first = 1;	
loop:
	if (noblock) {
		if ((inb(KBSTATP) & KBINRDY) == 0)
			return (-1);
	} else while((inb(KBSTATP) & KBINRDY) == 0)
		;
	dt = inb(KBDATAP);

	brk = dt & 0x80;	/* brk == 1 on key release */
	dt = dt & 0x7f;		/* keycode */

	act = action[dt];
	if (act&SHF)
		shfts = brk ? 0 : 1;
	if (act&ALT)
		alts = brk ? 0 : 1;
	if (act&NUM)
		if (act&L) {
			/* NUM lock */
			if(!brk)
				num = !num;
		} else
			num = brk ? 0 : 1;
	if (act&CTL)
		ctls = brk ? 0 : 1;
	if (act&CPS)
		if (act&L) {
			/* CAPS lock */
			if(!brk)
				caps = !caps;
		} else
			caps = brk ? 0 : 1;
	if (act&STP)
		if (act&L) {
			if(!brk)
				stp = !stp;
		} else
			stp = brk ? 0 : 1;

	if ((act&ASCII) && !brk) {
		unsigned char chr;

		if (shfts)
			chr = shift[dt];
		else if (ctls)
			chr = ctl[dt];
		else
			chr = unshift[dt];

		if (alts)
			chr |= 0x80;

		if (caps && (chr >= 'a' && chr <= 'z'))
			chr -= 'a' - 'A' ;
#define CTRL(s) (s & 0x1F)			
		if ((chr == '\r') || (chr == '\n') || (chr == CTRL('A')) || (chr == CTRL('S')))
		{
			/* Wait for key up */
			while (1)
			{
				while((inb(KBSTATP) & KBINRDY) == 0) ;
				dt = inb(KBDATAP);
				if (dt & 0x80) /* key up */ break;
			}
		}
		return (chr);
	}
	if (first && brk) return (0);  /* Ignore initial 'key up' codes */
	goto loop;
}

int scankbd(void)
{
	return (kbd(1) != -1);
}

void kbdreset(void)
{
	unsigned char c;

	/* Enable interrupts and keyboard controller */
	while (inb(KBSTATP) & KBOUTRDY)
		;
	outb(KBSTATP,0x60);
	while (inb(KBSTATP) & KBOUTRDY)
		;
	outb(KBDATAP,0x4D);

	/* Start keyboard stuff RESET */
	while (inb(KBSTATP) & KBOUTRDY)
		;	/* wait input ready */
	outb(KBDATAP,0xFF);	/* RESET */

	while ((c = inb(KBDATAP)) != 0xFA)
		;
}

int CRT_getc(void)
{
	int c;
	while ((c = kbd(0)) == 0) ;
	return(c);
}

int CRT_test(void)
{
	return ((inb(KBSTATP) & KBINRDY) != 0);
}


_dump_buf_with_offset(unsigned char *p, int s, unsigned char *base)
{
   int i, c;
   if ((unsigned int)s > (unsigned int)p)
   {
   	s = (unsigned int)s - (unsigned int)p;
   }
   while (s > 0)
   {
      if (base)
      {
         _printk("%06X: ", (int)p - (int)base);
      } else
      {
         _printk("%06X: ", p);
      }
      for (i = 0;  i < 16;  i++)
      {
         if (i < s)
         {
            _printk("%02X", p[i] & 0xFF);
         } else
         {
            _printk("  ");
         }
         if ((i % 2) == 1) _printk(" ");
         if ((i % 8) == 7) _printk(" ");
      }
      _printk(" |");
      for (i = 0;  i < 16;  i++)
      {
         if (i < s)
         {
            c = p[i] & 0xFF;
            if ((c < 0x20) || (c >= 0x7F)) c = '.';
         } else
         {
            c = ' ';
         }
         _printk("%c", c);
      }
      _printk("|\n");
      s -= 16;
      p += 16;
   }
}

_dump_buf(unsigned char *p, int s)
{
   _dump_buf_with_offset(p, s, 0);
}


dump_buf_with_offset(unsigned char *p, int s, unsigned char *base)
{
   int i, c;
   if ((unsigned int)s > (unsigned int)p)
   {
   	s = (unsigned int)s - (unsigned int)p;
   }
   while (s > 0)
   {
      if (base)
      {
         printk("%06X: ", (int)p - (int)base);
      } else
      {
         printk("%06X: ", p);
      }
      for (i = 0;  i < 16;  i++)
      {
         if (i < s)
         {
            printk("%02X", p[i] & 0xFF);
         } else
         {
            printk("  ");
         }
         if ((i % 2) == 1) printk(" ");
         if ((i % 8) == 7) printk(" ");
      }
      printk(" |");
      for (i = 0;  i < 16;  i++)
      {
         if (i < s)
         {
            c = p[i] & 0xFF;
            if ((c < 0x20) || (c >= 0x7F)) c = '.';
         } else
         {
            c = ' ';
         }
         printk("%c", c);
      }
      printk("|\n");
      s -= 16;
      p += 16;
   }
}

dump_buf(unsigned char *p, int s)
{
   dump_buf_with_offset(p, s, 0);
}


