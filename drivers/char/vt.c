/*
 *  linux/drivers/char/vt.c
 *
 *  Copyright (C) 1992 obz under the linux copyright
 *
 *  Dynamic diacritical handling - aeb@cwi.nl - Dec 1993
 *  Dynamic keymap and string allocation - aeb@cwi.nl - May 1994
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

#include <asm/io.h>
#include <asm/segment.h>

#include "kbd_kern.h"
#include "vt_kern.h"
#include "diacr.h"
#include "selection.h"

extern struct tty_driver console_driver;
extern int sel_cons;

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

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on);

extern int getkeycode(unsigned int scancode);
extern int setkeycode(unsigned int scancode, unsigned int keycode);
extern void compute_shiftstate(void);
extern void change_console(unsigned int new_console);
extern void complete_change_console(unsigned int new_console);
extern int vt_waitactive(void);
extern void do_blank_screen(int nopowersave);
extern void do_unblank_screen(void);

extern unsigned int keymap_count;

/*
 * routines to load custom translation table and EGA/VGA font from console.c
 */
extern int con_set_trans(char * table);
extern int con_get_trans(char * table);
extern void con_clear_unimap(struct unimapinit *ui);
extern int con_set_unimap(ushort ct, struct unipair *list);
extern int con_get_unimap(ushort ct, ushort *uct, struct unipair *list);
extern int con_set_font(char * fontmap);
extern int con_get_font(char * fontmap);

/*
 * these are the valid i/o ports we're allowed to change. they map all the
 * video ports
 */
#define GPFIRST 0x3b4
#define GPLAST 0x3df
#define GPNUM (GPLAST - GPFIRST + 1)

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
kd_mksound(unsigned int count, unsigned int ticks)
{
	static struct timer_list sound_timer = { NULL, NULL, 0, 0, kd_nosound };

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
			sound_timer.expires = ticks;
			add_timer(&sound_timer);
		}
	} else
		kd_nosound(0);
	sti();
	return;
}

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
		kd_mksound((unsigned int)arg, 0);
		return 0;

	case KDMKTONE:
		if (!perm)
			return -EPERM;
	{
		unsigned int ticks = HZ * ((arg >> 16) & 0xffff) / 1000;

		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		kd_mksound(arg & 0xffff, ticks);
		if (ticks == 0)
			kd_nosound(0);
		return 0;
	}

	case KDGKBTYPE:
		/*
		 * this is naive.
		 */
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned char));
		if (!i)
			put_fs_byte(KB_101, (char *) arg);
		return i;

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
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned long));
		if (!i)
			put_fs_long(vt_cons[console]->vc_mode, (unsigned long *) arg);
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
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned long));
		if (!i) {
			ucval = ((kbd->kbdmode == VC_RAW) ? K_RAW :
				 (kbd->kbdmode == VC_MEDIUMRAW) ? K_MEDIUMRAW :
				 (kbd->kbdmode == VC_UNICODE) ? K_UNICODE :
				 K_XLATE);
			put_fs_long(ucval, (unsigned long *) arg);
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
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned long));
		if (!i) {
			ucval = (vc_kbd_mode(kbd, VC_META) ? K_ESCPREFIX :
				 K_METABIT);
			put_fs_long(ucval, (unsigned long *) arg);
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
		sc = get_fs_long((int *) &a->scancode);
		kc = getkeycode(sc);
		if (kc < 0)
			return kc;
		put_fs_long(kc, (int *) &a->keycode);
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
		sc = get_fs_long((int *) &a->scancode);
		kc = get_fs_long((int *) &a->keycode);
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
		if ((i = get_fs_byte((char *) &a->kb_index)) >= NR_KEYS)
			return -EINVAL;
		if ((s = get_fs_byte((char *) &a->kb_table)) >= MAX_NR_KEYMAPS)
			return -EINVAL;
		key_map = key_maps[s];
		if (key_map) {
		    val = U(key_map[i]);
		    if (kbd->kbdmode != VC_UNICODE && KTYP(val) >= NR_TYPES)
			val = K_HOLE;
		} else
		    val = (i ? K_HOLE : K_NOSUCHMAP);
		put_fs_word(val, (short *) &a->kb_value);
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
		i = verify_area(VERIFY_READ, (void *)a, sizeof(struct kbentry));
		if (i)
			return i;
		if ((i = get_fs_byte((char *) &a->kb_index)) >= NR_KEYS)
			return -EINVAL;
		if ((s = get_fs_byte((char *) &a->kb_table)) >= MAX_NR_KEYMAPS)
			return -EINVAL;
		v = get_fs_word(&a->kb_value);
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
		if ((i = get_fs_byte(&a->kb_func)) >= MAX_NR_FUNC || i < 0)
			return -EINVAL;
		sz = sizeof(a->kb_string) - 1; /* sz should have been
						  a struct member */
		q = a->kb_string;
		p = func_table[i];
		if(p)
			for ( ; *p && sz; p++, sz--)
				put_fs_byte(*p, q++);
		put_fs_byte(0, q);
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
		if ((i = get_fs_byte(&a->kb_func)) >= MAX_NR_FUNC)
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
		for (p = a->kb_string; get_fs_byte(p) && sz; p++,sz--)
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
			if (!(*q = get_fs_byte(p)))
				break;
		return 0;
	}

	case KDGKBDIACR:
	{
		struct kbdiacrs *a = (struct kbdiacrs *)arg;

		i = verify_area(VERIFY_WRITE, (void *) a, sizeof(struct kbdiacrs));
		if (i)
			return i;
		put_fs_long(accent_table_size, &a->kb_cnt);
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
		ct = get_fs_long(&a->kb_cnt);
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
		put_fs_byte(kbd->ledflagstate |
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
		put_fs_byte(getledstate(), (char *) arg);
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
		i = verify_area(VERIFY_WRITE, (void *)vtmode, sizeof(struct vt_mode));
		if (i)
			return i;
		mode = get_fs_byte(&vtmode->mode);
		if (mode != VT_AUTO && mode != VT_PROCESS)
			return -EINVAL;
		vt_cons[console]->vt_mode.mode = mode;
		vt_cons[console]->vt_mode.waitv = get_fs_byte(&vtmode->waitv);
		vt_cons[console]->vt_mode.relsig = get_fs_word(&vtmode->relsig);
		vt_cons[console]->vt_mode.acqsig = get_fs_word(&vtmode->acqsig);
		/* the frsig is ignored, so we set it to 0 */
		vt_cons[console]->vt_mode.frsig = 0;
		vt_cons[console]->vt_pid = current->pid;
		vt_cons[console]->vt_newvt = 0;
		return 0;
	}

	case VT_GETMODE:
	{
		struct vt_mode *vtmode = (struct vt_mode *)arg;

		i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct vt_mode));
		if (i)
			return i;
		put_fs_byte(vt_cons[console]->vt_mode.mode, &vtmode->mode);
		put_fs_byte(vt_cons[console]->vt_mode.waitv, &vtmode->waitv);
		put_fs_word(vt_cons[console]->vt_mode.relsig, &vtmode->relsig);
		put_fs_word(vt_cons[console]->vt_mode.acqsig, &vtmode->acqsig);
		put_fs_word(vt_cons[console]->vt_mode.frsig, &vtmode->frsig);
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
		put_fs_word(fg_console + 1, &vtstat->v_active);
		state = 1;	/* /dev/tty0 is always open */
		for (i = 0, mask = 2; i < MAX_NR_CONSOLES && mask; ++i, mask <<= 1)
			if (VT_IS_IN_USE(i))
				state |= mask;
		put_fs_word(state, &vtstat->v_state);
		return 0;
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
		i = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long));
		if (i)
			return i;
		for (i = 0; i < MAX_NR_CONSOLES; ++i)
			if (! VT_IS_IN_USE(i))
				break;
		put_fs_long(i < MAX_NR_CONSOLES ? (i+1) : -1,
			    (unsigned long *)arg);
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
		change_console(arg);
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
				complete_change_console(newvt);
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
		ll = get_fs_word(&vtsizes->v_rows);
		cc = get_fs_word(&vtsizes->v_cols);
		return vc_resize(ll, cc);
	}

	case PIO_FONT:
		if (!perm)
			return -EPERM;
		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;
		return con_set_font((char *)arg);
		/* con_set_font() defined in console.c */

	case GIO_FONT:
		if (vt_cons[fg_console]->vc_mode != KD_TEXT)
			return -EINVAL;
		return con_get_font((char *)arg);
		/* con_get_font() defined in console.c */

	case PIO_SCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans((char *)arg);

	case GIO_SCRNMAP:
		return con_get_trans((char *)arg);

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
		    ct = get_fs_word(&ud->entry_ct);
		    list = (struct unipair *) get_fs_long(&ud->entries);
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
		    ct = get_fs_word(&ud->entry_ct);
		    list = (struct unipair *) get_fs_long(&ud->entries);
		    if (ct)
		      i = verify_area(VERIFY_WRITE, (void *) list,
				      ct*sizeof(struct unipair));
		}
		if (i)
		  return i;
		return con_get_unimap(ct, &(ud->entry_ct), list);
	      }
 
	default:
		return -ENOIOCTLCMD;
	}
}
