/* Keyboard handler */

#include <../drivers/char/defkeymap.c> /* yeah I know it's bad */

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

unsigned char shfts, ctls, alts, caps, num, stp;

#define	KBDATAP		0x60	/* kbd data port */
#define	KBSTATUSPORT	0x61	/* kbd status */
#define	KBSTATP		0x64	/* kbd status port */
#define	KBINRDY		0x01
#define	KBOUTRDY	0x02

#define _x__ 0x00  /* Unknown / unmapped */

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

static int
kbd(noblock)
	int noblock;
{
	unsigned char dt, brk, act;
	int first = 1;	
loop:
	if (noblock) {
		if ((inb(KBSTATP) & KBINRDY) == 0)
			return (-1);
	} else while((inb(KBSTATP) & KBINRDY) == 0) ;

	dt = inb(KBDATAP);

	brk = dt & 0x80;	/* brk == 1 on key release */
	dt = dt & 0x7f;		/* keycode */

	act = action[dt];
	if (/*act&SHF*/ dt == 54)
		shfts = brk ? 0 : 1;
	if (/*act&ALT*/ dt == 48)
		alts = brk ? 0 : 1;
	if (/*act&NUM*/ dt == 69)
		if (act&L) {
			/* NUM lock */
			if(!brk)
				num = !num;
		} else
			num = brk ? 0 : 1;
	if (/*act&CTL*/ dt == 29)
		ctls = brk ? 0 : 1;
	if (/*act&CPS*/ dt == 58)
		if (act&L) {
			/* CAPS lock */
			if(!brk)
				caps = !caps;
		} else
			caps = brk ? 0 : 1;
	if (0/*act&STP*/)
		if (act&L) {
			if(!brk)
				stp = !stp;
		} else
			stp = brk ? 0 : 1;

	if ((act&ASCII) && !brk) {
		unsigned char chr;
		if (shfts)
			chr = shift_map[dt];
		else if (ctls)
			chr = ctrl_map[dt];
		else
			chr = plain_map[dt];
		if (alts)
			chr |= 0x80;

		if (caps && (chr >= 'a' && chr <= 'z'))
			chr -= 'a' - 'A' ;
		if ( chr == 0x01 ) chr = '\n'; /* hack */
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

static
scankbd(void) {
	return (kbd(1) != -1);
}

static 
kbdreset(void)
{
	unsigned char c;
	int i;

	/* Send self-test */
	while (inb(KBSTATP) & KBOUTRDY) ;
	outb(KBSTATP,0xAA);
	while ((inb(KBSTATP) & KBINRDY) == 0) ;	/* wait input ready */
	if ((c = inb(KBDATAP)) != 0x55)
	{
		puts("Keyboard self test failed - result:");
		puthex(c);
		puts("\n");
	}
	/* Enable interrupts and keyboard controller */
	while (inb(KBSTATP) & KBOUTRDY) ;
	outb(KBSTATP,0x60);	
	while (inb(KBSTATP) & KBOUTRDY) ;
	outb(KBDATAP,0x45);
	for (i = 0;  i < 10000;  i++) udelay(1);
	while (inb(KBSTATP) & KBOUTRDY) ;
	outb(KBSTATP,0xAE);
}

static int kbd_reset = 0;

CRT_getc(void)
{
	int c;
	if (!kbd_reset) {kbdreset(); kbd_reset++; }
	while ((c = kbd(0)) == 0) ;
	return(c);
}

CRT_tstc(void)
{
	if (!kbd_reset) {kbdreset(); kbd_reset++; }
	return ((inb(KBSTATP) & KBINRDY) != 0);
}
