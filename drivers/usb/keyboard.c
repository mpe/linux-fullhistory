/*
	Fixes:
	1999/09/04 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
		Handle states in usb_kbd_irq
	1999/09/06 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
		rmmod usb-keyboard doesn't crash the system anymore: the irq
		handlers are correctly released with usb_release_irq
*/
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/config.h>
#include <linux/module.h>

#include <linux/kbd_ll.h>
#include "usb.h"

#define PCKBD_PRESSED  0x00
#define PCKBD_RELEASED 0x80
#define PCKBD_NEEDS_E0 0x80

#define USBKBD_MODIFIER_BASE  224
#define USBKBD_KEYCODE_OFFSET 2
#define USBKBD_KEYCODE_COUNT  6

#define USBKBD_VALID_KEYCODE(key) ((unsigned char)(key) > 3)
#define USBKBD_FIND_KEYCODE(down, key, count) \
    ((unsigned char*) memscan((down), (key), (count)) < ((down) + (count)))

#define USBKBD_REPEAT_DELAY (HZ / 4)
#define USBKBD_REPEAT_RATE (HZ / 20)

struct usb_keyboard
{
    struct usb_device *dev;
    unsigned long down[2];
    unsigned char repeat_key;
    struct timer_list repeat_timer;
    struct list_head list;
    unsigned int irqpipe;
    void *irq_handler; /* host controller's IRQ transfer handle */
};

extern unsigned char usb_kbd_map[];

static void * usb_kbd_probe(struct usb_device *dev, unsigned int i);
static void usb_kbd_disconnect(struct usb_device *dev, void *ptr);
static void usb_kbd_repeat(unsigned long dummy);

static LIST_HEAD(usb_kbd_list);

static struct usb_driver usb_kbd_driver =
{
    "keyboard",
    usb_kbd_probe,
    usb_kbd_disconnect,
    {NULL, NULL}
};


static void
usb_kbd_handle_key(unsigned char key, int down)
{
    int scancode = (int) usb_kbd_map[key];
    if(scancode)
    {
#ifndef CONFIG_MAC_KEYBOARD
        if(scancode & PCKBD_NEEDS_E0)
        {
            handle_scancode(0xe0, 1);
        }
#endif /* CONFIG_MAC_KEYBOARD */
        handle_scancode((scancode & ~PCKBD_NEEDS_E0), down);
    }
}

static void
usb_kbd_repeat(unsigned long dev_id)
{
    struct usb_keyboard *kbd = (struct usb_keyboard*) dev_id;

    unsigned long flags;
    save_flags(flags);
    cli();

    if(kbd->repeat_key)
    {
        usb_kbd_handle_key(kbd->repeat_key, 1);

        /* reset repeat timer */
        kbd->repeat_timer.function = usb_kbd_repeat;
        kbd->repeat_timer.expires = jiffies + USBKBD_REPEAT_RATE;
        kbd->repeat_timer.data = (unsigned long) kbd;
        kbd->repeat_timer.prev = NULL;
        kbd->repeat_timer.next = NULL;
        add_timer(&kbd->repeat_timer);
    }

    restore_flags(flags);
}

static int
usb_kbd_irq(int state, void *buffer, int len, void *dev_id)
{
    struct usb_keyboard *kbd = (struct usb_keyboard*) dev_id;
    unsigned long *down = (unsigned long*) buffer;

    /*
     * USB_ST_NOERROR is the normal case.
     * USB_ST_REMOVED occurs if keyboard disconnected
     * On other states, ignore
     */

    switch (state) {
	case USB_ST_REMOVED:
	case USB_ST_INTERNALERROR:
        	printk(KERN_DEBUG "%s(%d): Suspending\n", __FILE__, __LINE__);
		return 0; /* disable */
	case USB_ST_NOERROR: break;
		default: return 1; /* ignore */
    }

    if(kbd->down[0] != down[0] || kbd->down[1] != down[1]) {
        unsigned char *olddown, *newdown;
        unsigned char modsdelta, key;
        int i;

        /* handle modifier change */
        modsdelta = (*(unsigned char*) down ^ *(unsigned char*) kbd->down);
        if(modsdelta)
        {
            for(i = 0; i < 8; i++)
            {
                if(modsdelta & 0x01)
                {
                    int pressed = (*(unsigned char*) down >> i) & 0x01;
                    usb_kbd_handle_key(
                        i + USBKBD_MODIFIER_BASE,
                        pressed);
                }
                modsdelta >>= 1;
            }
        }

        olddown = (unsigned char*) kbd->down + USBKBD_KEYCODE_OFFSET;
        newdown = (unsigned char*) down + USBKBD_KEYCODE_OFFSET;

        /* handle released keys */
        for(i = 0; i < USBKBD_KEYCODE_COUNT; i++)
        {
            key = olddown[i];
            if(USBKBD_VALID_KEYCODE(key)
               && !USBKBD_FIND_KEYCODE(newdown, key, USBKBD_KEYCODE_COUNT))
            {
                usb_kbd_handle_key(key, 0);
            }
        }

        /* handle pressed keys */
        kbd->repeat_key = 0;
        for(i = 0; i < USBKBD_KEYCODE_COUNT; i++)
        {
            key = newdown[i];
            if(USBKBD_VALID_KEYCODE(key)
               && !USBKBD_FIND_KEYCODE(olddown, key, USBKBD_KEYCODE_COUNT))
            {
                usb_kbd_handle_key(key, 1);
                kbd->repeat_key = key;
            }
        }

        /* set repeat timer if any keys were pressed */
        if(kbd->repeat_key)
        {
            del_timer(&kbd->repeat_timer);
            kbd->repeat_timer.function = usb_kbd_repeat;
            kbd->repeat_timer.expires = jiffies + USBKBD_REPEAT_DELAY;
            kbd->repeat_timer.data = (unsigned long) kbd;
            kbd->repeat_timer.prev = NULL;
            kbd->repeat_timer.next = NULL;
            add_timer(&kbd->repeat_timer);
        }

        kbd->down[0] = down[0];
        kbd->down[1] = down[1];
    }

    return 1;
}

static void *
usb_kbd_probe(struct usb_device *dev, unsigned int i)
{
    struct usb_interface_descriptor *interface;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_keyboard *kbd;
    int ret;
    
    interface = &dev->actconfig->interface[i].altsetting[0];
    endpoint = &interface->endpoint[0];

    if(interface->bInterfaceClass != 3
       || interface->bInterfaceSubClass != 1
       || interface->bInterfaceProtocol != 1)
    {
        return NULL;
    }

    printk(KERN_INFO "USB HID boot protocol keyboard detected.\n");

    kbd = kmalloc(sizeof(struct usb_keyboard), GFP_KERNEL);
    if(kbd)
    {
        memset(kbd, 0, sizeof(*kbd));
        kbd->dev = dev;

        usb_set_protocol(dev, 0);
        usb_set_idle(dev, 0, 0);
        
	kbd->irqpipe = usb_rcvctrlpipe(dev, endpoint->bEndpointAddress);
	ret = usb_request_irq(dev, kbd->irqpipe,
				usb_kbd_irq, endpoint->bInterval,
				kbd, &kbd->irq_handler);
	if (ret) {
		printk(KERN_INFO "usb-keyboard failed usb_request_irq (0x%x)\n", ret);
		goto probe_err;
	}

        list_add(&kbd->list, &usb_kbd_list);
	
	return kbd;
    }

probe_err:
    if (kbd)
    	kfree (kbd);
    return NULL;
}

static void
usb_kbd_disconnect(struct usb_device *dev, void *ptr)
{
    struct usb_keyboard *kbd = (struct usb_keyboard*) ptr;
    if (kbd)
    {
	usb_release_irq(dev, kbd->irq_handler, kbd->irqpipe);
	list_del(&kbd->list);
	del_timer(&kbd->repeat_timer);
	kfree(kbd);
    }

    printk(KERN_INFO "USB HID boot protocol keyboard removed.\n");
}

int usb_kbd_init(void)
{
	return usb_register(&usb_kbd_driver);
}

void usb_kbd_cleanup(void)
{
	struct list_head *cur, *head = &usb_kbd_list;

	cur = head->next;

	while (cur != head) {
		struct usb_keyboard *kbd = list_entry(cur, struct usb_keyboard, list);

		cur = cur->next;

                list_del(&kbd->list);
                INIT_LIST_HEAD(&kbd->list);

		if (kbd->irq_handler) {
			usb_release_irq(kbd->dev, kbd->irq_handler, kbd->irqpipe);
			/* never keep a reference to a released IRQ! */
			kbd->irq_handler = NULL;
		}
	}

	usb_deregister(&usb_kbd_driver);
}

#ifdef MODULE
int init_module(void)
{
	return usb_kbd_init();
}

void cleanup_module(void)
{
	usb_kbd_cleanup();
}
#endif

