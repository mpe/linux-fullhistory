/*
 * debug.c - USB debug helper routines.
 *
 * I just want these out of the way where they aren't in your
 * face, but so that you can still use them..
 */
#include <linux/kernel.h>

#include "usb.h"

static void usb_show_endpoint(struct usb_endpoint_descriptor *endpoint)
{
	usb_show_endpoint_descriptor(endpoint);
}

static void usb_show_interface(struct usb_interface_descriptor *interface)
{
	int i;

	usb_show_interface_descriptor(interface);
	for (i = 0 ; i < interface->bNumEndpoints; i++)
		usb_show_endpoint(interface->endpoint + i);
}

static void usb_show_config(struct usb_config_descriptor *config)
{
	int i;

	usb_show_config_descriptor(config);
	for (i = 0 ; i < config->bNumInterfaces; i++)
		usb_show_interface(config->interface + i);
}

void usb_show_device(struct usb_device *dev)
{
	int i;

	usb_show_device_descriptor(&dev->descriptor);
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
		usb_show_config(dev->config + i);
}


/*
 * Parse and show the different USB descriptors.
 */
void usb_show_device_descriptor(struct usb_device_descriptor *desc)
{
	printk("  USB version %x.%02x\n", desc->bcdUSB >> 8, desc->bcdUSB & 0xff);
	printk("  Vendor:  %04x\n", desc->idVendor);
	printk("  Product: %04x\n", desc->idProduct);
	printk("  Configurations: %d\n", desc->bNumConfigurations);

	printk("  Device Class: %d\n", desc->bDeviceClass);
	switch (desc->bDeviceClass) {
	case 0:
		printk("    Per-interface classes\n");
		break;
	case 9:
		printk("    Hub device class\n");
		break;
	case 0xff:
		printk("    Vendor class\n");
		break;
	default:
		printk("    Unknown class\n");
	}
}

void usb_show_config_descriptor(struct usb_config_descriptor * desc)
{
	printk("Configuration:\n");
	printk("  bLength             = %4d%s\n", desc->bLength,
		desc->bLength == 9 ? "" : " (!!!)");
	printk("  bDescriptorType     =   %02x\n", desc->bDescriptorType);
	printk("  wTotalLength        = %04x\n", desc->wTotalLength);
	printk("  bNumInterfaces      =   %02x\n", desc->bNumInterfaces);
	printk("  bConfigurationValue =   %02x\n", desc->bConfigurationValue);
	printk("  iConfiguration      =   %02x\n", desc->iConfiguration);
	printk("  bmAttributes        =   %02x\n", desc->bmAttributes);
	printk("  MaxPower            = %4dmA\n", desc->MaxPower * 2);
}

void usb_show_interface_descriptor(struct usb_interface_descriptor * desc)
{
	printk("  Interface:\n");
	printk("    bLength             = %4d%s\n", desc->bLength,
		desc->bLength == 9 ? "" : " (!!!)");
	printk("    bDescriptorType     =   %02x\n", desc->bDescriptorType);
	printk("    bInterfaceNumber    =   %02x\n", desc->bInterfaceNumber);
	printk("    bAlternateSetting   =   %02x\n", desc->bAlternateSetting);
	printk("    bNumEndpoints       =   %02x\n", desc->bNumEndpoints);
	printk("    bInterfaceClass     =   %02x\n", desc->bInterfaceClass);
	printk("    bInterfaceSubClass  =   %02x\n", desc->bInterfaceSubClass);
	printk("    bInterfaceProtocol  =   %02x\n", desc->bInterfaceProtocol);
	printk("    iInterface          =   %02x\n", desc->iInterface);
}

void usb_show_endpoint_descriptor(struct usb_endpoint_descriptor * desc)
{
	char *EndpointType[4] = { "Control", "Isochronous", "Bulk", "Interrupt" };
	printk("    Endpoint:\n");
	printk("      bLength             = %4d%s\n", desc->bLength,
		desc->bLength == 7 ? "" : " (!!!)");
	printk("      bDescriptorType     =   %02x\n", desc->bDescriptorType);
	printk("      bEndpointAddress    =   %02x (%s)\n", desc->bEndpointAddress,
		(desc->bEndpointAddress & 0x80) ? "in" : "out");
	printk("      bmAttributes        =   %02x (%s)\n", desc->bmAttributes,
		EndpointType[3 & desc->bmAttributes]);
	printk("      wMaxPacketSize      = %04x\n", desc->wMaxPacketSize);
	printk("      bInterval           =   %02x\n", desc->bInterval);
}

void usb_show_hub_descriptor(struct usb_hub_descriptor * desc)
{
	int len = 7;
	unsigned char *ptr = (unsigned char *) desc;

	printk("Interface:");
	while (len) {
		printk(" %02x", *ptr);
		ptr++; len--;
	}
	printk("\n");
}

void usb_show_string(struct usb_device* dev, char *id, int index)
{
	if (index <= dev->maxstring && dev->stringindex && dev->stringindex[index])
		printk("%s: %s\n", id, dev->stringindex[index]);
}
