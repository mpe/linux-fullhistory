/* $Id: fsm.c,v 1.2 1996/04/29 22:49:57 fritz Exp $
 *
 * $Log: fsm.c,v $
 * Revision 1.2  1996/04/29 22:49:57  fritz
 * Removed compatibility-macros.
 *
 * Revision 1.1  1996/04/13 10:23:41  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "teles.h"

void
FsmNew(struct Fsm *fsm,
       struct FsmNode *fnlist, int fncount)
{
	int             i;

	fsm->jumpmatrix = (int *) Smalloc(4L * fsm->state_count * fsm->event_count,
					  GFP_KERNEL, "Fsm jumpmatrix");
	memset(fsm->jumpmatrix, 0, 4L * fsm->state_count * fsm->event_count);

	for (i = 0; i < fncount; i++)
		fsm->jumpmatrix[fsm->state_count * fnlist[i].event +
			      fnlist[i].state] = (int) fnlist[i].routine;
}

void
FsmFree(struct Fsm *fsm)
{
	Sfree((void *) fsm->jumpmatrix);
}

int
FsmEvent(struct FsmInst *fi, int event, void *arg)
{
	void            (*r) (struct FsmInst *, int, void *);
	char            str[80];

	r = (void (*)) fi->fsm->jumpmatrix[fi->fsm->state_count * event + fi->state];
	if (r) {
		if (fi->debug) {
			sprintf(str, "State %s Event %s",
				fi->fsm->strState[fi->state],
				fi->fsm->strEvent[event]);
			fi->printdebug(fi, str);
		}
		r(fi, event, arg);
		return (0);
	} else {
		if (fi->debug) {
			sprintf(str, "State %s Event %s no routine",
				fi->fsm->strState[fi->state],
				fi->fsm->strEvent[event]);
			fi->printdebug(fi, str);
		}
		return (!0);
	}
}

void
FsmChangeState(struct FsmInst *fi, int newstate)
{
	char            str[80];

	fi->state = newstate;
	if (fi->debug) {
		sprintf(str, "ChangeState %s",
			fi->fsm->strState[newstate]);
		fi->printdebug(fi, str);
	}
}

static void
FsmExpireTimer(struct FsmTimer *ft)
{
	FsmEvent(ft->fi, ft->event, ft->arg);
}

void
FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft)
{
	ft->fi = fi;
	ft->tl.function = (void *) FsmExpireTimer;
	ft->tl.data = (long) ft;
	init_timer(&ft->tl);
}

void
FsmDelTimer(struct FsmTimer *ft, int where)
{
	long            flags;

#if 0
	if (ft->fi->debug) {
		sprintf(str, "FsmDelTimer %lx %d", ft, where);
		ft->fi->printdebug(ft->fi, str);
	}
#endif

	save_flags(flags);
	cli();
	if (ft->tl.next)
		del_timer(&ft->tl);
	restore_flags(flags);
}

int
FsmAddTimer(struct FsmTimer *ft,
	    int millisec, int event, void *arg, int where)
{

#if 0
	if (ft->fi->debug) {
		sprintf(str, "FsmAddTimer %lx %d %d", ft, millisec, where);
		ft->fi->printdebug(ft->fi, str);
	}
#endif

	if (ft->tl.next) {
		printk(KERN_WARNING "FsmAddTimer: timer already active!\n");
		return -1;
	}
	init_timer(&ft->tl);
	ft->event = event;
	ft->arg = arg;
	ft->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&ft->tl);
	return 0;
}

int
FsmTimerRunning(struct FsmTimer *ft)
{
	return (ft->tl.next != NULL);
}

void
jiftime(char *s, long mark)
{
	s += 8;

	*s-- = '\0';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = '.';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 6 + '0';
	mark /= 6;
	*s-- = ':';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
}
