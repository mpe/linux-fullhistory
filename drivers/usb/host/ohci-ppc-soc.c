/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * (C) Copyright 2003-2005 MontaVista Software Inc.
 * 
 * Bus Glue for PPC On-Chip OHCI driver
 * Tested on Freescale MPC5200 and IBM STB04xxx
 *
 * Modified by Dale Farnsworth <dale@farnsworth.org> from ohci-sa1111.c
 *
 * This file is licenced under the GPL.
 */

#include <asm/usb.h>

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_ppc_soc_probe - initialize On-Chip HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 * Store this function in the HCD's struct pci_driver as probe().
 */
static int usb_hcd_ppc_soc_probe(const struct hc_driver *driver,
			  struct platform_device *pdev)
{
	int retval;
	struct usb_hcd *hcd;
	struct ohci_hcd	*ohci;
	struct resource *res;
	int irq;
	struct usb_hcd_platform_data *pd = pdev->dev.platform_data;

	pr_debug("initializing PPC-SOC USB Controller\n");

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		pr_debug(__FILE__ ": no irq\n");
		return -ENODEV;
	}
	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_debug(__FILE__ ": no reg addr\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, "PPC-SOC USB");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = res->end - res->start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug(__FILE__ ": request_mem_region failed\n");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug(__FILE__ ": ioremap failed\n");
		retval = -ENOMEM;
		goto err2;
	}

	if (pd->start && (retval = pd->start(pdev)))
		goto err3;

	ohci = hcd_to_ohci(hcd);
	ohci->flags |= OHCI_BIG_ENDIAN;
	ohci_hcd_init(ohci);

	retval = usb_add_hcd(hcd, irq, SA_INTERRUPT);
	if (retval == 0)
		return retval;

	pr_debug("Removing PPC-SOC USB Controller\n");
	if (pd && pd->stop)
		pd->stop(pdev);
 err3:
	iounmap(hcd->regs);
 err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
 err1:
 	usb_put_hcd(hcd);
	return retval;
}


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_ppc_soc_remove - shutdown processing for On-Chip HCDs
 * @pdev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_ppc_soc_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
static void usb_hcd_ppc_soc_remove(struct usb_hcd *hcd,
		struct platform_device *pdev)
{
	struct usb_hcd_platform_data *pd = pdev->dev.platform_data;

	usb_remove_hcd(hcd);

	pr_debug("stopping PPC-SOC USB Controller\n");
	if (pd && pd->stop)
		pd->stop(pdev);

	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_hcd_put(hcd);
}

static int __devinit
ohci_ppc_soc_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	int		ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		err("can't start %s", ohci_to_hcd(ohci)->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver ohci_ppc_soc_hc_driver = {
	.description =		hcd_name,
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_ppc_soc_start,
	.stop =			ohci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_USB_SUSPEND
	.hub_suspend =		ohci_hub_suspend,
	.hub_resume =		ohci_hub_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

static int ohci_hcd_ppc_soc_drv_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_ppc_soc_probe(&ohci_ppc_soc_hc_driver, pdev);
	return ret;
}

static int ohci_hcd_ppc_soc_drv_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_hcd_ppc_soc_remove(hcd, pdev);
	return 0;
}

static struct device_driver ohci_hcd_ppc_soc_driver = {
	.name		= "ppc-soc-ohci",
	.bus		= &platform_bus_type,
	.probe		= ohci_hcd_ppc_soc_drv_probe,
	.remove		= ohci_hcd_ppc_soc_drv_remove,
#if	defined(CONFIG_USB_SUSPEND) || defined(CONFIG_PM)
	/*.suspend	= ohci_hcd_ppc_soc_drv_suspend,*/
	/*.resume	= ohci_hcd_ppc_soc_drv_resume,*/
#endif
};

static int __init ohci_hcd_ppc_soc_init(void)
{
	pr_debug(DRIVER_INFO " (PPC SOC)\n");
	pr_debug("block sizes: ed %d td %d\n", sizeof(struct ed),
							sizeof(struct td));

	return driver_register(&ohci_hcd_ppc_soc_driver);
}

static void __exit ohci_hcd_ppc_soc_cleanup(void)
{
	driver_unregister(&ohci_hcd_ppc_soc_driver);
}

module_init(ohci_hcd_ppc_soc_init);
module_exit(ohci_hcd_ppc_soc_cleanup);
