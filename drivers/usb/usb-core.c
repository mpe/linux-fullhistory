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
#include <linux/usb.h>

/*
 * USB core
 */

int usb_hub_init(void);
void usb_hub_cleanup(void);
int usb_major_init(void);
void usb_major_cleanup(void);

/*
 * USB device drivers
 */

int usb_audio_init(void);
int usb_cpia_init(void);
int usb_ibmcam_init(void);
int usb_ov511_init(void);
int dabusb_init(void);
int plusb_init(void);

/*
 * HCI drivers
 */

int uhci_init(void);
int ohci_hcd_init(void);

#ifdef MODULE

/*
 * Cleanup
 */

void cleanup_module(void)
{
	usb_major_cleanup();
	usbdevfs_cleanup();
	usb_hub_cleanup();
}

/*
 * Init
 */

int init_module(void)
#else
int usb_init(void)
#endif
{
	usb_major_init();
        usbdevfs_init();
	usb_hub_init();

#ifndef CONFIG_USB_MODULE
#ifdef CONFIG_USB_AUDIO
	usb_audio_init();
#endif
#ifdef CONFIG_USB_CPIA
	usb_cpia_init();
#endif
#ifdef CONFIG_USB_IBMCAM
	usb_ibmcam_init();
#endif
#ifdef CONFIG_USB_OV511
	usb_ov511_init();
#endif
#ifdef CONFIG_USB_DABUSB
	dabusb_init();
#endif
#ifdef CONFIG_USB_PLUSB
	plusb_init();
#endif
#ifdef CONFIG_USB_UHCI
	uhci_init();
#endif
#ifdef CONFIG_USB_UHCI_ALT
	uhci_init();
#endif
#ifdef CONFIG_USB_OHCI
	ohci_hcd_init(); 
#endif
#endif
	return 0;
}
