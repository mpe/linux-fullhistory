/* Functions local to drivers/usb/core/ */

extern void usb_create_driverfs_dev_files (struct usb_device *dev);
extern void usb_create_driverfs_intf_files (struct usb_interface *intf);
extern int usb_probe_interface (struct device *dev);
extern int usb_unbind_interface (struct device *dev);

extern void usb_disable_endpoint (struct usb_device *dev, unsigned int epaddr);
extern void usb_disable_interface (struct usb_device *dev,
		struct usb_interface *intf);
extern void usb_disable_device (struct usb_device *dev, int skip_ep0);

extern void usb_enable_endpoint (struct usb_device *dev,
		struct usb_endpoint_descriptor *epd);
extern void usb_enable_interface (struct usb_device *dev,
		struct usb_interface *intf);
