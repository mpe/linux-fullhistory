#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/ioctl.h>

// #define DEBUG

#include "usb.h"

// #define RD_DATA_DUMP /* Enable to dump data - limited to 24 bytes */
// #define WR_DATA_DUMP /* DEBUG does not have to be defined. */

#define IS_EP_BULK(ep)  ((ep).bmAttributes == USB_ENDPOINT_XFER_BULK ? 1 : 0)
#define IS_EP_BULK_IN(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
#define IS_EP_BULK_OUT(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
#define IS_EP_INTR(ep) ((ep).bmAttributes == USB_ENDPOINT_XFER_INT ? 1 : 0)

#ifdef DEBUG
#define SCN_DEBUG(X) X
#else
#define SCN_DEBUG(X)
#endif

#define IBUF_SIZE 32768
#define OBUF_SIZE 4096


/* FIXME: These are NOT registered ioctls()'s */

#define PV8630_RECEIVE 69
#define PV8630_SEND    70

struct hpscan_usb_data {
	struct usb_device *hpscan_dev;
        int isopen;		/* Not zero if the device is open */
	int present;		/* Device is present on the bus */
	char *obuf, *ibuf;	/* transfer buffers */
	char bulk_in_ep, bulk_out_ep, intr_ep; /* Endpoint assignments */
	char *button;		/* Front panel button buffer */
};

static struct hpscan_usb_data hpscan;

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION("USB Scanner Driver");

static __s32 vendor=-1, product=-1;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");
