/*
 * driver/usb/usb-core.c
 *
 * (C) Copyright David Waite 1999
 * based on code from usb.c, by Linus Torvalds
 *
 * The purpose of this file is to pull any and all generic modular code from
 * usb.c and put it in a separate file. This way usb.c is kept as a generic
 * library, while this file handles starting drivers, etc.
 *
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/config.h>

#include "inits.h"
#include "usb.h"

#ifndef CONFIG_USB_MODULE
#	ifdef CONFIG_USB_UHCI
		int uhci_init(void);
#	endif
#	ifdef CONFIG_USB_OHCI_HCD
		int ohci_hcd_init(void);
#	endif
#endif

int usb_init(void)
{
	usb_major_init();
#ifdef CONFIG_USB_PROC
	proc_usb_init();
#endif
	usb_hub_init();

#ifndef CONFIG_USB_MODULE
#	ifdef CONFIG_USB_UHCI
		uhci_init();
#	endif
#	ifdef CONFIG_USB_OHCI_HCD
		ohci_hcd_init(); 
#	endif
#	ifdef CONFIG_USB_MOUSE
		usb_mouse_init();
#	endif
#       ifdef CONFIG_USB_HP_SCANNER
                usb_hp_scanner_init();
#       endif
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
		usb_printer_init();
#	endif
#	ifdef CONFIG_USB_SERIAL
		usb_serial_init();
#	endif
#	ifdef CONFIG_USB_CPIA
		usb_cpia_init();
#	endif
#	ifdef CONFIG_USB_DC2XX
		usb_dc2xx_init();
#	endif
#	ifdef CONFIG_USB_SCSI
		usb_scsi_init();
#	endif
#	ifdef CONFIG_USB_DABUSB
		dabusb_init();
#	endif
#endif
	return 0;
}

/*
 *  Clean up when unloading the module
 */
void cleanup_drivers(void)
{
	usb_major_cleanup();
#ifdef CONFIG_USB_PROC
	proc_usb_cleanup ();
#endif
	usb_hub_cleanup();	

#ifndef MODULE
#	ifdef CONFIG_USB_MOUSE
        	usb_mouse_cleanup();
#	endif
#       ifdef CONFIG_USB_HP_SCANNER
                usb_hp_scanner_cleanup();
#       endif
#	ifdef CONFIG_USB_DABUSB
		dabusb_cleanup();
#	endif
#	ifdef CONFIG_USB_KBD
		usb_kbd_cleanup();
#	endif
#	ifdef CONFIG_USB_ACM
		usb_acm_cleanup();
#	endif
#	ifdef CONFIG_USB_CPIA
		usb_cpia_cleanup();
#	endif
#	ifdef CONFIG_USB_DC2XX
		usb_dc2xx_cleanup();
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
