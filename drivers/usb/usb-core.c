/*
 * driver/usb/usb-core.c
 *
 * (C) Copyright David Waite 1999
 * based on code from usb.c, by Linus Torvolds
 *
 * The purpose of this file is to pull any and all generic modular code from
 * usb.c and put it in a separate file. This way usb.c is kept as a generic
 * library, while this file handles starting drivers, etc.
 *
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>

#include "inits.h"
#include "usb.h"

#ifndef CONFIG_USB_MODULE
#	ifdef CONFIG_USB_UHCI
		int uhci_init(void);
#	endif
#	ifdef CONFIG_USB_OHCI
		int ohci_init(void);
#	endif
#	ifdef CONFIG_USB_OHCI_HCD
		int ohci_hcd_init(void);
#	endif
#endif

int usb_init(void)
{
#ifndef CONFIG_USB_MODULE
#	ifdef CONFIG_USB_UHCI
		uhci_init();
#	endif
#	ifdef CONFIG_USB_OHCI
		ohci_init();
#	endif
#	ifdef CONFIG_USB_OHCI_HCD
		ohci_hcd_init(); 
#	endif
#	ifdef CONFIG_USB_MOUSE
		usb_mouse_init();
#	endif
#	ifdef CONFIG_USB_KBD
		usb_kbd_init();
#	endif
#	ifdef CONFIG_USB_AUDIO
		usb_audio_init();
#	endif
#	ifdef CONFIG_USB_ACM
		usb_acm_init();
#	endif
#	ifdef CONFIG_USB_PRINTER
		usb_print_init();
#	endif
#	ifdef CONFIG_USB_CPIA
		usb_cpia_init();
#	endif
#	ifdef CONFIG_USB_HUB
		usb_hub_init();
#	endif
#endif
	return 0;
}
/*
 *  Clean up when unloading the module
 */
void cleanup_drivers(void)
{
#ifndef MODULE
#	ifdef CONFIG_USB_HUB
		usb_hub_cleanup();
#	endif
#	ifdef CONFIG_USB_MOUSE
        	usb_mouse_cleanup();
#	endif
#endif
}

#ifdef MODULE
int init_module(void)
{
	return usb_init();
}
void cleanup_module(void)
{
	cleanup_drivers();
}
#endif


