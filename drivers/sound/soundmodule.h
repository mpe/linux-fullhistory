#ifndef _SOUNDMODULE_H
#define _SOUNDMODULE_H

#ifdef MODULE

#include <linux/notifier.h>

#ifdef SOUND_CORE

struct notifier_block *sound_locker=(struct notifier_block *)0;

#define SOUND_INC_USE_COUNT	notifier_call_chain(&sound_locker, 1, 0)
#define SOUND_DEC_USE_COUNT	notifier_call_chain(&sound_locker, 0, 0)

#else

#define SOUND_LOCK		notifier_chain_register(&sound_locker, &sound_notifier)
#define SOUND_LOCK_END		notifier_chain_unregister(&sound_locker, &sound_notifier)

extern struct notifier_block *sound_locker;


static int my_notifier_call(struct notifier_block *b, unsigned long foo, void *bar)
{
	if(foo)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
	return NOTIFY_DONE;
}

static struct notifier_block sound_notifier=
{
	my_notifier_call,
	(void *)0,
	0
};

#endif
#endif
#endif
