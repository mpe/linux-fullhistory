#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/ioctl.h>

// #define DEBUG

#include "usb.h"

/* WARNING: These DATA_DUMP's can produce a lot of data. Caveat Emptor. */
// #define RD_DATA_DUMP /* Enable to dump data - limited to 24 bytes */
// #define WR_DATA_DUMP /* DEBUG does not have to be defined. */

#define IS_EP_BULK(ep)  ((ep).bmAttributes == USB_ENDPOINT_XFER_BULK ? 1 : 0)
#define IS_EP_BULK_IN(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
#define IS_EP_BULK_OUT(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
#define IS_EP_INTR(ep) ((ep).bmAttributes == USB_ENDPOINT_XFER_INT ? 1 : 0)

#define USB_SCN_MINOR(X) MINOR((X)->i_rdev) - SCN_BASE_MNR

#ifdef DEBUG
#define SCN_DEBUG(X) X
#else
#define SCN_DEBUG(X)
#endif

#define IBUF_SIZE 32768
#define OBUF_SIZE 4096


/* FIXME: These are NOT registered ioctls()'s */

#define PV8630_IOCTL_INREQUEST 69
#define PV8630_IOCTL_OUTREQUEST 70

#define SCN_MAX_MNR 16		/* We're allocated 16 minors */
#define SCN_BASE_MNR 48		/* USB Scanners start at minor 48 */

struct scn_usb_data {
	struct usb_device *scn_dev;
	struct urb scn_irq;
	unsigned int ifnum;	/* Interface number of the USB device */
	kdev_t scn_minor;	/* Scanner minor - used in disconnect() */
	unsigned char button;	/* Front panel buffer */
        char isopen;		/* Not zero if the device is open */
	char present;		/* Not zero if device is present */
	char *obuf, *ibuf;	/* transfer buffers */
	char bulk_in_ep, bulk_out_ep, intr_ep; /* Endpoint assignments */
};

static struct scn_usb_data *p_scn_table[SCN_MAX_MNR] = { NULL, /* ... */};

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION("USB Scanner Driver");

static __s32 vendor=-1, product=-1;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");

/* Forward declarations */
static struct usb_driver scanner_driver;
