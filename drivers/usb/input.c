/*
 *  input.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  The input layer module itself
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/random.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

#ifndef MODULE
EXPORT_SYMBOL(input_register_device);
EXPORT_SYMBOL(input_unregister_device);
EXPORT_SYMBOL(input_register_handler);
EXPORT_SYMBOL(input_unregister_handler);
EXPORT_SYMBOL(input_open_device);
EXPORT_SYMBOL(input_close_device);
EXPORT_SYMBOL(input_event);
#endif

static struct input_dev *input_dev = NULL;
static struct input_handler *input_handler = NULL;

static int input_number = 0;

void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct input_handle *handle = dev->handle;

/*
 * Filter non-events, and bad input values out.
 */

	if (type > EV_MAX || !test_bit(type, dev->evbit))
		return;

	switch (type) {

		case EV_KEY:

			if (code > KEY_MAX || !test_bit(code, dev->keybit) || !!test_bit(code, dev->key) == value)
				return;

			if (value == 2) break;

			change_bit(code, dev->key);

			if (test_bit(EV_REP, dev->evbit) && dev->timer.function) {
				if (value) {
					mod_timer(&dev->timer, jiffies + dev->rep[REP_DELAY]);
					dev->repeat_key = code;
					break;
				}
				if (dev->repeat_key == code)
					del_timer(&dev->timer);
			}

			break;
		
		case EV_ABS:

			if (code > ABS_MAX || !test_bit(code, dev->absbit) || (value == dev->abs[code]))
				return;

			dev->abs[code] = value;

			break;

		case EV_REL:

			if (code > REL_MAX || !test_bit(code, dev->relbit) || (value == 0))
				return;

			break;

		case EV_LED:
	
			if (code > LED_MAX || !test_bit(code, dev->ledbit) || !!test_bit(code, dev->led) == value)
				return;

			change_bit(code, dev->led);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_SND:
	
			if (code > SND_MAX || !test_bit(code, dev->sndbit) || !!test_bit(code, dev->snd) == value)
				return;

			change_bit(code, dev->snd);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_REP:

			if (code > REP_MAX || dev->rep[code] == value) return;

			dev->rep[code] = value;
			if (dev->event) dev->event(dev, type, code, value);

			break;
	}
/*
 * Add randomness.
 */

#if 0 /* BUG */
	add_input_randomness(((unsigned long) dev) ^ (type << 24) ^ (code << 16) ^ value);
#endif

/*
 * Distribute the event to handler modules.
 */

	while (handle) {
		handle->handler->event(handle, type, code, value);
		handle = handle->dnext;
	}
}

static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;
	input_event(dev, EV_KEY, dev->repeat_key, 2);
	mod_timer(&dev->timer, jiffies + dev->rep[REP_PERIOD]);
}

void input_register_device(struct input_dev *dev)
{
	struct input_handler *handler = input_handler;

/*
 * Initialize repeat timer to default values.
 */

	init_timer(&dev->timer);
	dev->timer.data = (long) dev;
	dev->timer.function = input_repeat_key;
	dev->rep[REP_DELAY] = HZ/4;
	dev->rep[REP_PERIOD] = HZ/33;

/*
 * Add the device.
 */

	MOD_INC_USE_COUNT;
	dev->number = input_number++;
	dev->next = input_dev;	
	input_dev = dev;

/*
 * Notify handlers.
 */

	while (handler) {
		handler->connect(handler, dev);
		handler = handler->next;
	}
}

void input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle = dev->handle;
	struct input_dev **devptr = &input_dev;

/*
 * Kill any pending repeat timers.
 */

	del_timer(&dev->timer);

/*
 * Notify handlers.
 */

	while (handle) {
		handle->handler->disconnect(handle);
		handle = handle->dnext;
	}

/*
 * Remove the device.
 */

	while (*devptr && (*devptr != dev))
		devptr = &((*devptr)->next);
	*devptr = (*devptr)->next;

	input_number--;
	MOD_DEC_USE_COUNT;
}

void input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev = input_dev;

/*
 * Add the handler.
 */

	handler->next = input_handler;	
	input_handler = handler;
	
/*
 * Notify it about all existing devices.
 */

	while (dev) {
		handler->connect(handler, dev);
		dev = dev->next;
	}
}

void input_unregister_handler(struct input_handler *handler)
{
	struct input_handler **handlerptr = &input_handler;
	struct input_handle *handle = handler->handle;

/*
 * Tell the handler to disconnect from all devices it keeps open.
 */

	while (handle) {
		handler->disconnect(handle);
		handle = handle->hnext;
	}

/*
 * Remove it.
 */

	while (*handlerptr && (*handlerptr != handler))
		handlerptr = &((*handlerptr)->next);

	*handlerptr = (*handlerptr)->next;

}

void input_open_device(struct input_handle *handle)
{
	handle->dnext = handle->dev->handle;
	handle->hnext = handle->handler->handle;
	handle->dev->handle = handle;
	handle->handler->handle = handle;

	if (handle->dev->open)
		handle->dev->open(handle->dev);
}

void input_close_device(struct input_handle *handle)
{
	struct input_handle **handleptr;

	if (handle->dev->close)
		handle->dev->close(handle->dev);
/*
 * Remove from device list of handles.
 */

	handleptr = &handle->dev->handle;

	while (*handleptr && (*handleptr != handle))
		handleptr = &((*handleptr)->dnext);
	*handleptr = (*handleptr)->dnext;

/*
 * Remove from handler list of handles.
 */

	handleptr = &handle->handler->handle;

	while (*handleptr && (*handleptr != handle))
		handleptr = &((*handleptr)->hnext);
	*handleptr = (*handleptr)->hnext;
}


#ifdef MODULE
int init_module(void)
#else
int __init input_init(void)
#endif
{
#ifndef MODULE
#ifdef CONFIG_INPUT_KEYBDEV
	keybdev_init();
#endif
#ifdef CONFIG_INPUT_MOUSEDEV
	mousedev_init();
#endif
#ifdef CONFIG_INPUT_JOYDEV
	joydev_init();
#endif
#ifdef CONFIG_INPUT_EVDEV
	evdev_init();
#endif
#endif
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
}
#endif
