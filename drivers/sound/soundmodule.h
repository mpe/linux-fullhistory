#ifndef _SOUNDMODULE_H
#define _SOUNDMODULE_H

#include <linux/notifier.h>

extern struct notifier_block *sound_locker;
extern void sound_notifier_chain_register(struct notifier_block *);

#ifdef MODULE

#define SOUND_LOCK		sound_notifier_chain_register(&sound_notifier); 
#define SOUND_LOCK_END		notifier_chain_unregister(&sound_locker, &sound_notifier)

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
