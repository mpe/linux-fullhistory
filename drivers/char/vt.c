/*
 *  linux/drivers/char/vt.c
 *
 *  Copyright (C) 1992 obz under the linux copyright
 *
 *  Dynamic diacritical handling - aeb@cwi.nl - Dec 1993
 *  Dynamic keymap and string allocation - aeb@cwi.nl - May 1994
 *  Restrict VT switching via ioctl() - grif@cs.ucr.edu - Dec 1995
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/segment.h>

#include "kbd_kern.h"
#include "vt_kern.h"
#include "diacr.h"
#include "selection.h"

extern char vt_dont_switch;
extern struct tty_driver console_driver;

#define VT_IS_IN_USE(i)	(console_driver.table[i] && console_driver.table[i]->count)
#define VT_BUSY(i)	(VT_IS_IN_USE(i) || i == fg_console || i == sel_cons)

/*
 * Console (vt and kd) routines, as defined by USL SVR4 manual, and by
 * experimentation and study of X386 SYSV handling.
 *
 * One point of difference: SYSV vt's are /dev/vtX, which X >= 0, and
 * /dev/console is a separate ttyp. Under Linux, /dev/tty0 is /dev/console,
 * and the vc start at /dev/ttyX, X >= 1. We maintain that here, so we will
 * always treat our set of vt as numbered 1..MAX_NR_CONSOLES (corresponding to
 * ttys 0..MAX_NR_CONSOLES-1). Explicitly naming VT 0 is illegal, but using
 * /dev/tty0 (fg_console) as a target is legal, since an implicit aliasing
 * to the current console is done by the main ioctl code.
 */

struct vt_struct *vt_cons[MAX_NR_CONSOLES];

#ifndef __alpha__
asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on);
#endif

extern int getkeycode(unsigned int scancode);
extern int setkeycode(unsigned int scancode, unsigned int keycode);
extern void compute_shiftstate(void);
extern void complete_change_console(unsigned int new_console);
extern int vt_waitactive(void);
extern void do_blank_screen(int nopowersave);

extern unsigned int keymap_count;

/*
 * routines to load custom translation table, EGA/VGA font and
 * VGA colour palette from console.c
 */
extern int con_set_trans_old(unsigned char * table);
extern int con_get_trans_old(unsigned char * table);
extern int con_set_trans_new(unsigned short * table);
extern int con_get_trans_new(unsigned short * table);
extern void con_clear_unimap(struct unimapinit *ui);
extern int con_set_unimap(ushort ct, struct unipair *list);
extern int con_get_unimap(ushort ct, ushort *uct, struct unipair *list);
extern void con_set_default_unimap(void);
extern int con_set_font(char * fontmap, int ch512);
extern int con_get_font(char * fontmap);
extern int con_set_cmap(unsigned char *cmap);
extern int con_get_cmap(unsigned char *cmap);
extern void reset_palette(int currcons);
extern int con_adjust_height(unsigned long fontheight);

extern int video_mode_512ch;
extern unsigned long video_font_height;
extern unsigned long default_font_height;
extern unsigned long video_scan_lines;

/*
 * these are the valid i/o ports we're allowed to change. they map all the
 * video ports
 */
#define GPFIRST 0x3b4
#define GPLAST 0x3df
#define GPNUM (GPLAST - GPFIRST + 1)

/*
 * This function is called when the size of the physical screen has been
 * changed.  If either the row or col argument is nonzero, set the appropriate
 * entry in each winsize structure for all the virtual consoles, then
 * send SIGWINCH to all processes with a virtual console as controlling
 * tty.
 */

static int
kd_size_changed(int row, int col)
{
  struct task_struct *p;
  int i;

  if ( !row && !col ) return 0;

  for ( i = 0 ; i < MAX_NR_CONSOLES ; i++ )
    {
      if ( console_driver.table[i] )
	{
	  if ( row ) console_driver.table[i]->winsize.ws_row = row;
	  if ( col ) console_driver.table[i]->winsize.ws_col = col;
	}
    }

  for_each_task(p)
    {
      if ( p->tty && MAJOR(p->tty->device) == TTY_MAJOR &&
	   MINOR(p->tty->device) <= MAX_NR_CONSOLES && MINOR(p->tty->device) )
	{
	  send_sig(SIGWINCH, p, 1);
	}
    }

  return 0;
}

/*
 * Generates sound of some count for some number of clock ticks
 * [count = 1193180 / frequency]
 *
 * If freq is 0, will turn off sound, else will turn it on for that time.
 * If msec is 0, will return immediately, else will sleep for msec time, then
 * turn sound off.
 *
 * We use the BEEP_TIMER vector since we're using the same method to
 * generate sound, and we'll overwrite any beep in progress. That may
 * be something to fix later, if we like.
 *
 * We also return immediately, which is what was implied within the X
 * comments - KDMKTONE doesn't put the process to sleep.
 */
static void
kd_nosound(unsigned long ignored)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);
	return;
}

void
_kd_mksound(unsigned int hz, unsigned int ticks)
{
	static struct timer_list sound_timer = { NULL, NULL, 0, 0,
						 kd_nosound };

	unsigned int count = 0;

	if (hz > 20 && hz < 32767)
		count = 1193180 / hz;
	
	cli();
	del_timer(&sound_timer);
	if (count) {
		/* enable counter 2 */
		outb_p(inb_p(0x61)|3, 0x61);
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(count & 0xff, 0x42);
		outb((count >> 8) & 0xff, 0x42);

		if (ticks) {
			sound_timer.expires = jiffies+ticks;
			add_timer(&sound_timer);
		}
	} else
		kd_nosound(0);
	sti();
	return;
}

void (*kd_mksound)(unsigned int hz, unsigned int ticks) = _kd_mksound;
	
/*
 * We handle the console-specific ioctl's here.  We allow the
 * capability to modify any console, not just the fg_console. 
 */
int vt_ioctl(struct tty_struct *tty, struct file * file,
	     unsigned int cmd, unsigned long arg)
{
	int i, perm;
	unsigned int console;
	unsigned char ucval;
	struct kbd_struct * kbd;
	struct vt_struct *vt = (struct vt_struct *)tty->driver_data;

	console = vt->vc_num;

	if (!vc_cons_allocated(console)) 	/* impossible? */
		return -ENOIOCTLCMD;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or super-user.
	 */
	perm = 0;
	if (current->tty == tty || suser())
		perm = 1;

	kbd = kbd_table + console;
	switch (cmd) {
	case KIOCSOUND:
		if (!perm)
			return -EPERM;
		if (arg)
			arg = 1193180 / arg;
		kd_mksound(arg, 0);
		return 0;

	case KDMKTONE:
		if (!perm)
			return -EPERM;
	{
		unsigned int ticks, count;
		
		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		ticks = HZ * ((arg >> 16) & 0xffff) / 1000;
		if ((arg & 0xffff) == 0 ) arg |= 1; /* jp: huh? */
		count = ticks ? (1193180 / (arg & 0xffff)) : 0;
		kd_mksound(count, ticks);
		return 0;
	}

	case KDGKBTYPE:
		/*
		 * this is naive.
		 */
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned char));
		if (!i)
			put_user(KB_101, (char *) arg);
		return i;

#ifndef __alpha__
		/*
		 * These cannot be implemented on any machine that implements
		 * ioperm() in user level (such as Alpha PCs).
		 */
	case KDADDIO:
	case KDDELIO:
		/*
		 * KDADDIO and KDDELIO may be able to add ports beyond what
		 * we reject here, but to be safe...
		 */
		if (arg < GPFIRST || arg > GPLAST)
			return -EINVAL;
		return sys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;

	case KDENABIO:
	case KDDISABIO:
		return sys_ioperm(GPFIRST, GPNUM,
				  (cmd == KDENABIO)) ? -ENXIO : 0;
#endif

	case KDSETMODE:
		/*
		 * currently, setting the mode from KD_TEXT to KD_GRAPHICS
		 * doesn't do a whole lot. i'm not sure if it should do any
		 * restoration of modes or what...
		 */
		if (!perm)
			return -EPERM;
		switch (arg) {
		case KD_GRAPHICS:
			break;
		case KD_TEXT0:
		case KD_TEXT1:
			arg = KD_TEXT;
		case KD_TEXT:
			break;
		default:
			return -EINVAL;
		}
		if (vt_cons[console]->vc_mode == (unsigned char) arg)
			return 0;
		vt_cons[console]->vc_mode = (unsigned char) arg;
		if (console != fg_console)
			return 0;
		/*
		 * explicitly blank/unblank the screen if switching modes
		 */
		if (arg == KD_TEXT)
			do_unblank_screen();
		else
			do_blank_screen(1);
		return 0;

	case KDGETMODE:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (!i)
			put_user(vt_cons[console]->vc_mode, (int *) arg);
		return i;

	case KDMAPDISP:
	case KDUNMAPDISP:
		/*
		 * these work like a combination of mmap and KDENABIO.
		 * this could be easily finished.
		 */
		return -EINVAL;

	case KDSKBMODE:
		if (!perm)
			return -EPERM;
		switch(arg) {
		  case K_RAW:
			kbd->kbdmode = VC_RAW;
			break;
		  case K_MEDIUMRAW:
			kbd->kbdmode = VC_MEDIUMRAW;
			break;
		  case K_XLATE:
			kbd->kbdmode = VC_XLATE;
			compute_shiftstate();
			break;
		  case K_UNICODE:
			kbd->kbdmode = VC_UNICODE;
			compute_shiftstate();
			break;
		  default:
			return -EINVAL;
		}
		if (tty->ldisc.flush_buffer)
			tty->ldisc.flush_buffer(tty);
		return 0;

	case KDGKBMODE:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (!i) {
			ucval = ((kbd->kbdmode == VC_RAW) ? K_RAW :
				 (kbd->kbdmode == VC_MEDIUMRAW) ? K_MEDIUMRAW :
				 (kbd->kbdmode == VC_UNICODE) ? K_UNICODE :
				 K_XLATE);
			put_user(ucval, (int *) arg);
		}
		return i;

	/* this could be folded into KDSKBMODE, but for compatibility
	   reasons it is not so easy to fold KDGKBMETA into KDGKBMODE */
	case KDSKBMETA:
		switch(arg) {
		  case K_METABIT:
			clr_vc_kbd_mode(kbd, VC_META);
			break;
		  case K_ESCPREFIX:
			set_vc_kbd_mode(kbd, VC_META);
			break;
		  default:
			return -EINVAL;
		}
		return 0;

	case KDGKBMETA:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (!i) {
			ucval = (vc_kbd_mode(kbd, VC_META) ? K_ESCPREFIX :
				 K_METABIT);
			put_user(ucval, (int *) arg);
		}
		return i;

	case KDGETKEYCODE:
	{
		struct kbkeycode * const a = (struct kbkeycode *)arg;
		unsigned int sc;
		int kc;

		i = verify_area(VERIFY_WRITE, (void *)a, sizeof(struct kbkeycode));
		if (i)
			return i;
		sc = get_user(&a->scancode);
		kc = getkeycode(sc);
		if (kc < 0)
			return kc;
		put_user(kc, &a->keycode);
		return 0;
	}

	case KDSETKEYCODE:
	{
		struct kbkeycode * const a = (struct kbkeycode *)arg;
		unsigned int sc, kc;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)a, sizeof(struct kbkeycode));
		if (i)
			return i;
		sc = get_user(&a->scancode);
		kc = get_user(&a->keycode);
		return setkeycode(sc, kc);
	}

	case KDGKBENT:
	{
		struct kbentry * const a = (struct kbentry *)arg;
		ushort *key_map, val;
		u_char s;

		i = verify_area(VERIFY_WRITE, (void *)a, sizeof(struct kbentry));
		if (i)
			return i;
		if ((i = get_user(&a->kb_index)) >= NR_KEYS)
			return -EINVAL;
		if ((s = get_user(&a->kb_table)) >= MAX_NR_KEYMAPS)
			return -EINVAL;
		key_map = key_maps[s];
		if (key_map) {
		    val = U(key_map[i]);
		    if (kbd->kbdmode != VC_UNICODE && KTYP(val) >= NR_TYPES)
			val = K_HOLE;
		} else
		    val = (i ? K_HOLE : K_NOSUCHMAP);
		put_user(val, &a->kb_value);
		return 0;
	}

	case KDSKBENT:
	{
		const struct kbentry * a = (struct kbentry *)arg;
		ushort *key_map;
		u_char s;
		u_short v, ov;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (const void *)a, sizeof(struct kbentry));
		if (i)
			return i;
		if ((i = get_user(&a->kb_index)) >= NR_KEYS)
			return -EINVAL;
		if ((s = get_user(&a->kb_table)) >= MAX_NR_KEYMAPS)
			return -EINVAL;
		v = get_user(&a->kb_value);
		if (!i && v == K_NOSUCHMAP) {
			/* disallocate map */
			key_map = key_maps[s];
			if (s && key_map) {
			    key_maps[s] = 0;
			    if (key_map[0] == U(K_ALLOCATED)) {
				kfree_s(key_map, sizeof(plain_map));
				keymap_count--;
			    }
			}
			return 0;
		}

		if (KTYP(v) < NR_TYPES) {
		    if (KVAL(v) > max_vals[KTYP(v)])
			return -EINVAL;
		} else
		    if (kbd->kbdmode != VC_UNICODE)
			return -EINVAL;

		/* assignment to entry 0 only tests validity of args */
		if (!i)
			return 0;

		if (!(key_map = key_maps[s])) {
			int j;

			if (keymap_count >= MAX_NR_OF_USER_KEYMAPS && !suser())
				return -EPERM;

			key_map = (ushort *) kmalloc(sizeof(plain_map),
						     GFP_KERNEL);
			if (!key_map)
				return -ENOMEM;
			key_maps[s] = key_map;
			key_map[0] = U(K_ALLOCATED);
			for (j = 1; j < NR_KEYS; j++)
				key_map[j] = U(K_HOLE);
			keymap_count++;
		}
		ov = U(key_map[i]);
		if (v == ov)
			return 0;	/* nothing to do */
		/*
		 * Only the Superuser can set or unset the Secure
		 * Attention Key.
		 */
		if (((ov == K_SAK) || (v == K_SAK)) && !suser())
			return -EPERM;
		key_map[i] = U(v);
		if (!s && (KTYP(ov) == KT_SHIFT || KTYP(v) == KT_SHIFT))
			compute_shiftstate();
		return 0;
	}

	case KDGKBSENT:
	{
		struct kbsentry *a = (struct kbsentry *)arg;
		char *p;
		u_char *q;
		int sz;

		i = verify_area(VERIFY_WRITE, (void *)a, sizeof(struct kbsentry));
		if (i)
			return i;
		if ((i = get_user(&a->kb_func)) >= MAX_NR_FUNC || i < 0)
			return -EINVAL;
		sz = sizeof(a->kb_string) - 1; /* sz should have been
						  a struct member */
		q = a->kb_string;
		p = func_table[i];
		if(p)
			for ( ; *p && sz; p++, sz--)
				put_user(*p, q++);
		put_user('\0', q);
		return ((p && *p) ? -EOVERFLOW : 0);
	}

	case KDSKBSENT:
	{
		struct kbsentry * const a = (struct kbsentry *)arg;
		int delta;
		char *first_free, *fj, *fnw;
		int j, k, sz;
		u_char *p;
		char *q;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)a, sizeof(struct kbsentry));
		if (i)
			return i;
		if ((i = get_user(&a->kb_func)) >= MAX_NR_FUNC)
			return -EINVAL;
		q = func_table[i];

		first_free = funcbufptr + (funcbufsize - funcbufleft);
		for (j = i+1; j < MAX_NR_FUNC && !func_table[j]; j++) ;
		if (j < MAX_NR_FUNC)
			fj = func_table[j];
		else
			fj = first_free;

		delta = (q ? -strlen(q) : 1);
		sz = sizeof(a->kb_string); 	/* sz should have been
						   a struct member */
		for (p = a->kb_string; get_user(p) && sz; p++,sz--)
			delta++;
		if (!sz)
			return -EOVERFLOW;
		if (delta <= funcbufleft) { 	/* it fits in current buf */
		    if (j < MAX_NR_FUNC) {
			memmove(fj + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			    if (func_table[k])
				func_table[k] += delta;
		    }
		    if (!q)
		      func_table[i] = fj;
		    funcbufleft -= delta;
		} else {			/* allocate a larger buffer */
		    sz = 256;
		    while (sz < funcbufsize - funcbufleft + delta)
		      sz <<= 1;
		    fnw = (char *) kmalloc(sz, GFP_KERNEL);
		    if(!fnw)
		      return -ENOMEM;

		    if (!q)
		      func_table[i] = fj;
		    if (fj > funcbufptr)
			memmove(fnw, funcbufptr, fj - funcbufptr);
		    for (k = 0; k < j; k++)
		      if (func_table[k])
			func_table[k] = fnw + (func_table[k] - funcbufptr);

		    if (first_free > fj) {
			memmove(fnw + (fj - funcbufptr) + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			  if (func_table[k])
			    func_table[k] = fnw + (func_table[k] - funcbufptr) + delta;
		    }
		    if (funcbufptr != func_buf)
		      kfree_s(funcbufptr, funcbufsize);
		    funcbufptr = fnw;
		    funcbufleft = funcbufleft - delta + sz - funcbufsize;
		    funcbufsize = sz;
		}
		for (p = a->kb_string, q = func_table[i]; ; p++, q++)
			if (!(*q = get_user(p)))
				break;
		return 0;
	}

	case KDGKBDIACR:
	{
		struct kbdiacrs *a = (struct kbdiacrs *)arg;

		i = verify_area(VERIFY_WRITE, (void *) a, sizeof(struct kbdiacrs));
		if (i)
			return i;
		put_user(accent_table_size, &a->kb_cnt);
		memcpy_tofs(a->kbdiacr, accent_table,
			    accent_table_size*sizeof(struct kbdiacr));
		return 0;
	}

	case KDSKBDIACR:
	{
		struct kbdiacrs *a = (struct kbdiacrs *)arg;
		unsigned int ct;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *) a, sizeof(struct kbdiacrs));
		if (i)
			return i;
		ct = get_user(&a->kb_cnt);
		if (ct >= MAX_DIACR)
			return -EINVAL;
		accent_table_size = ct;
		memcpy_fromfs(accent_table, a->kbdiacr, ct*sizeof(struct kbdiacr));
		return 0;
	}

	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned char));
		if (i)
			return i;
		put_user(kbd->ledflagstate |
			 (kbd->default_ledflagstate << 4), (char *) arg);
		return 0;

	case KDSKBLED:
		if (!perm)
			return -EPERM;
		if (arg & ~0x77)
			return -EINVAL;
		kbd->ledflagstate = (arg & 7);
		kbd->default_ledflagstate = ((arg >> 4) & 7);
		set_leds();
		return 0;

	/* the ioctls below only set the lights, not the functions */
	/* for those, see KDGKBLED and KDSKBLED above */
	case KDGETLED:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned char));
		if (i)
			return i;
		put_user(getledstate(), (char *) arg);
		return 0;

	case KDSETLED:
		if (!perm)
		  return -EPERM;
		setledstate(kbd, arg);
		return 0;

	/*
	 * A process can indicate its willingness to accept signals
	 * generated by pressing an appropriate key combination.
	 * Thus, one can have a daemon that e.g. spawns a new console
	 * upon a keypress and then changes to it.
	 * Probably init should be changed to do this (and have a
	 * field ks (`keyboard signal') in inittab describing the
	 * desired action), so that the number of background daemons
	 * does not increase.
	 */
	case KDSIGACCEPT:
	{
		extern int spawnpid, spawnsig;
		if (!perm)
		  return -EPERM;
		if (arg < 1 || arg > NSIG || arg == SIGKILL)
		  return -EINVAL;
		spawnpid = current->pid;
		spawnsig = arg;
		return 0;
	}

	case VT_SETMODE:
	{
		struct vt_mode *vtmode = (struct vt_mode *)arg;
		char mode;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)vtmode, sizeof(struct vt_mode));
		if (i)
			return i;
		mode = get_user(&vtmode->mode);
		if (mode != VT_AUTO && mode != VT_PROCESS)
			return -EINVAL;
		vt_cons[console]->vt_mode.mode = mode;
		vt_cons[console]->vt_mode.waitv = get_user(&vtmode->waitv);
		vt_cons[console]->vt_mode.relsig = get_user(&vtmode->relsig);
		vt_cons[console]->vt_mode.acqsig = get_user(&vtmode->acqsig);
		/* the frsig is ignored, so we set it to 0 */
		vt_cons[console]->vt_mode.frsig = 0;
		vt_cons[console]->vt_pid = current->pid;
		/* no switch is required -- saw@shade.msu.ru */
		vt_cons[console]->vt_newvt = -1; 
		return 0;
	}

	case VT_GETMODE:
	{
		struct vt_mode *vtmode = (struct vt_mode *)arg;

		i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct vt_mode));
		if (i)
			return i;
		put_user(vt_cons[console]->vt_mode.mode, &vtmode->mode);
		put_user(vt_cons[console]->vt_mode.waitv, &vtmode->waitv);
		put_user(vt_cons[console]->vt_mode.relsig, &vtmode->relsig);
		put_user(vt_cons[console]->vt_mode.acqsig, &vtmode->acqsig);
		put_user(vt_cons[console]->vt_mode.frsig, &vtmode->frsig);
		return 0;
	}

	/*
	 * Returns global vt state. Note that VT 0 is always open, since
	 * it's an alias for the current VT, and people can't use it here.
	 * We cannot return state for more than 16 VTs, since v_state is short.
	 */
	case VT_GETSTATE:
	{
		struct vt_stat *vtstat = (struct vt_stat *)arg;
		unsigned short state, mask;

		i = verify_area(VERIFY_WRITE,(void *)vtstat, sizeof(struct vt_stat));
		if (i)
			return i;
		put_user(fg_console + 1, &vtstat->v_active);
		state = 1;	/* /dev/tty0 is always open */
		for (i = 0, mask = 2; i < MAX_NR_CONSOLES && mask; ++i, mask <<= 1)
			if (VT_IS_IN_USE(i))
				state |= mask;
		put_user(state, &vtstat->v_state);
		return 0;
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (i)
			return i;
		for (i = 0; i < MAX_NR_CONSOLES; ++i)
			if (! VT_IS_IN_USE(i))
				break;
		put_user(i < MAX_NR_CONSOLES ? (i+1) : -1, (int *) arg);
		return 0;

	/*
	 * ioctl(fd, VT_ACTIVATE, num) will cause us to switch to vt # num,
	 * with num >= 1 (switches to vt 0, our console, are not allowed, just
	 * to preserve sanity).
	 */
	case VT_ACTIVATE:
		if (!perm)
			return -EPERM;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			return -ENXIO;
		arg--;
		i = vc_allocate(arg);
		if (i)
			return i;
		set_console(arg);
		return 0;

	/*
	 * wait until the specified VT has been activated
	 */
	case VT_WAITACTIVE:
		if (!perm)
			return -EPERM;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			return -ENXIO;
		arg--;
		while (fg_console != arg)
		{
			if (vt_waitactive() < 0)
				return -EINTR;
		}
		return 0;

	/*
	 * If a vt is under process control, the kernel will not switch to it
	 * immediately, but postpone the operation until the process calls this
	 * ioctl, allowing the switch to complete.
	 *
	 * According to the X sources this is the behavior:
	 *	0:	pending switch-from not OK
	 *	1:	pending switch-from OK
	 *	2:	completed switch-to OK
	 */
	case VT_RELDISP:
		if (!perm)
			return -EPERM;
		if (vt_cons[console]->vt_mode.mode != VT_PROCESS)
			return -EINVAL;

		/*
		 * Switching-from response
		 */
		if (vt_cons[console]->vt_newvt >= 0)
		{
			if (arg == 0)
				/*
				 * Switch disallowed, so forget we were trying
				 * to do it.
				 */
				vt_cons[console]->vt_newvt = -1;

			else
			{
				/*
				 * The current vt has been released, so
				 * complete the switch.
				 */
				int newvt = vt_cons[console]->vt_newvt;
				vt_cons[console]->vt_newvt = -1;
				i = vc_allocate(newvt);
				if (i)
					return i;
				/*
				 * When we actually do the console switch,
				 * make sure we are atomic with respect to
				 * other console switches..
				 */
				start_bh_atomic();
				complete_change_console(newvt);
				end_bh_atomic();
			}
		}

		/*
		 * Switched-to response
		 */
		else
		{
			/*
			 * If it's just an ACK, ignore it
			 */
			if (arg != VT_ACKACQ)
				return -EINVAL;
		}

		return 0;

	 /*
	  * Disallocate memory associated to VT (but leave VT1)
	  */
	 case VT_DISALLOCATE:
		if (arg > MAX_NR_CONSOLES)
			return -ENXIO;
		if (arg == 0) {
		    /* disallocate all unused consoles, but leave 0 */
		    for (i=1; i<MAX_NR_CONSOLES; i++)
		      if (! VT_BUSY(i))
			vc_disallocate(i);
		} else {
		    /* disallocate a single console, if possible */
		    arg--;
		    if (VT_BUSY(arg))
		      return -EBUSY;
		    if (arg)			      /* leave 0 */
		      vc_disallocate(arg);
		}
		return 0;

	case VT_RESIZE:
	{
		struct vt_sizes *vtsizes = (struct vt_sizes *) arg;
		ushort ll,cc;
		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)vtsizes, sizeof(struct vt_sizes));
		if (i)
			return i;
		ll = get_user(&vtsizes->v_rows);
		cc = get_user(&vtsizes->v_cols);
		i = vc_resize(ll, cc);
		return i ? i : 	kd_size_changed(ll, cc);
	}

	case VT_RESIZEX:
	{
		struct vt_consize *vtconsize = (struct vt_consize *) arg;
		ushort ll,cc,vlin,clin,vcol,ccol;
		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)vtconsize, sizeof(struct vt_consize));
		if (i)
			return i;
		ll = get_user(&vtconsize->v_rows);
		cc = get_user(&vtconsize->v_cols);
		vlin = get_user(&vtconsize->v_vlin);
		clin = get_user(&vtconsize->v_clin);
		vcol = get_user(&vtconsize->v_vcol);
		ccol = get_user(&vtconsize->v_ccol);
		vlin = vlin ? vlin : video_scan_lines;
		if ( clin )
		  {
		    if ( ll )
		      {
			if ( ll != vlin/clin )
			  return EINVAL; /* Parameters don't add up */
		      }
		    else 
		      ll = vlin/clin;
		  }
		if ( vcol && ccol )
		  {
		    if ( cc )
		      {
			if ( cc != vcol/ccol )
			  return EINVAL;
		      }
		    else
		      cc = vcol/ccol;
		  }

		if ( clin > 32 )
		  return EINVAL;
		    
		if ( vlin )
		  video_scan_lines = vlin;
		if ( clin )
		  video_font_height = clin;
		
		i = vc_resize(ll, cc);
		if (i)
			return i;

		kd_size_changed(ll, cc);
		return 0;
  	}

	case PIO_FONT:
		if (!perm)
			return -EPERM;
		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;
		return con_set_font((char *)arg, 0);
		/* con_set_font() defined in console.c */

	case GIO_FONT:
		if (vt_cons[fg_console]->vc_mode != KD_TEXT ||
		    video_mode_512ch)
			return -EINVAL;
		return con_get_font((char *)arg);
		/* con_get_font() defined in console.c */

	case PIO_CMAP:
                if (!perm)
			return -EPERM;
                return con_set_cmap((char *)arg);
                /* con_set_cmap() defined in console.c */

	case GIO_CMAP:
                return con_get_cmap((char *)arg);
                /* con_get_cmap() defined in console.c */

	case PIO_FONTX:
	{
	        struct consolefontdesc cfdarg;

		if (!perm)
			return -EPERM;
		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;
		i = verify_area(VERIFY_READ, (void *)arg,
				sizeof(struct consolefontdesc));
		if (i) return i;
		memcpy_fromfs(&cfdarg, (void *)arg,
			      sizeof(struct consolefontdesc)); 
		
		if ( cfdarg.charcount == 256 ||
		     cfdarg.charcount == 512 ) {
			i = con_set_font(cfdarg.chardata,
				cfdarg.charcount == 512);
			if (i)
				return i;
			i = con_adjust_height(cfdarg.charheight);
			return (i <= 0) ? i : kd_size_changed(i, 0);
		} else
			return -EINVAL;
	}

	case PIO_FONTRESET:
	{
		if (!perm)
			return -EPERM;
		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;

#ifdef BROKEN_GRAPHICS_PROGRAMS
		/* With BROKEN_GRAPHICS_PROGRAMS defined, the default
		   font is not saved. */
		return -ENOSYS;
#else

		i = con_set_font(NULL, 0);	/* Set font to default */
		if (i) return i;

		i = con_adjust_height(default_font_height);
		if ( i > 0 ) kd_size_changed(i, 0);
		con_set_default_unimap();

		return 0;
#endif
	}

	case GIO_FONTX:
	{
	        struct consolefontdesc cfdarg;
		int nchar;

		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;
		i = verify_area(VERIFY_WRITE, (void *)arg,
			sizeof(struct consolefontdesc));
		if (i) return i;	
		memcpy_fromfs(&cfdarg, (void *) arg,
			      sizeof(struct consolefontdesc)); 
		i = cfdarg.charcount;
		cfdarg.charcount = nchar = video_mode_512ch ? 512 : 256;
		cfdarg.charheight = video_font_height;
		memcpy_tofs((void *) arg, &cfdarg,
			    sizeof(struct consolefontdesc)); 
		if ( cfdarg.chardata )
		{
			if ( i < nchar )
				return -ENOMEM;
			return con_get_font(cfdarg.chardata);
		} else
			return 0;
	}

	case PIO_SCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_old((unsigned char *)arg);

	case GIO_SCRNMAP:
		return con_get_trans_old((unsigned char *)arg);

	case PIO_UNISCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_new((unsigned short *)arg);

	case GIO_UNISCRNMAP:
		return con_get_trans_new((unsigned short *)arg);

	case PIO_UNIMAPCLR:
	      { struct unimapinit ui;
		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)arg, sizeof(struct unimapinit));
		if (i)
		  return i;
		memcpy_fromfs(&ui, (void *)arg, sizeof(struct unimapinit));
		con_clear_unimap(&ui);
		return 0;
	      }

	case PIO_UNIMAP:
	      { struct unimapdesc *ud;
		u_short ct;
		struct unipair *list;

		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)arg, sizeof(struct unimapdesc));
		if (i == 0) {
		    ud = (struct unimapdesc *) arg;
		    ct = get_user(&ud->entry_ct);
		    list = get_user(&ud->entries);
		    i = verify_area(VERIFY_READ, (void *) list,
				    ct*sizeof(struct unipair));
		}
		if (i)
		  return i;
		return con_set_unimap(ct, list);
	      }

	case GIO_UNIMAP:
	      { struct unimapdesc *ud;
		u_short ct;
		struct unipair *list;

		i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct unimapdesc));
		if (i == 0) {
		    ud = (struct unimapdesc *) arg;
		    ct = get_user(&ud->entry_ct);
		    list = get_user(&ud->entries);
		    if (ct)
		      i = verify_area(VERIFY_WRITE, (void *) list,
				      ct*sizeof(struct unipair));
		}
		if (i)
		  return i;
		return con_get_unimap(ct, &(ud->entry_ct), list);
	      }
	case VT_LOCKSWITCH:
		if (!suser())
		   return -EPERM;
		vt_dont_switch = 1;
		return 0;
	case VT_UNLOCKSWITCH:
		if (!suser())
		   return -EPERM;
		vt_dont_switch = 0;
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
