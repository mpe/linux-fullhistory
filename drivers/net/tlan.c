/********************************************************************
 *
 *  Linux ThunderLAN Driver
 *
 *  tlan.c
 *  by James Banks
 *
 *  (C) 1997-1998 Caldera, Inc.
 *  (C) 1998 James Banks
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU Public License, incorporated herein by reference.
 *
 ** This file is best viewed/edited with columns>=132.
 *
 ** Useful (if not required) reading:
 *
 *		Texas Instruments, ThunderLAN Programmer's Guide,
 *			TI Literature Number SPWU013A
 *			available in PDF format from www.ti.com
 *		Level One, LXT901 and LXT970 Data Sheets
 *			available in PDF format from www.level1.com
 *		National Semiconductor, DP83840A Data Sheet
 *			available in PDF format from www.national.com
 *		Microchip Technology, 24C01A/02A/04A Data Sheet
 *			available in PDF format from www.microchip.com
 *
 * Change History
 *
 *	Tigran Aivazian <tigran@sco.com>:	TLan_PciProbe() now uses
 *						new PCI BIOS interface.
 *
 ********************************************************************/


#include <linux/module.h>

#include "tlan.h"

#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>



typedef u32 (TLanIntVectorFunc)( struct device *, u16 );


#ifdef MODULE

static	struct device	*TLanDevices = NULL;
static	int		TLanDevicesInstalled = 0;

static	int		aui = 0;
static	int		sa_int = 0;
static	int		duplex = 0;
static	int		speed = 0;

#endif


static  int		debug = 0;
static	int		bbuf = 0;
static	u8		*TLanPadBuffer;
static	char		TLanSignature[] = "TLAN";
static	int		TLanVersionMajor = 1;
static	int		TLanVersionMinor = 0;


static	TLanAdapterEntry TLanAdapterList[] = {
	{ PCI_VENDOR_ID_COMPAQ, 
	  PCI_DEVICE_ID_NETELLIGENT_10,
	  "Compaq Netelligent 10 T PCI UTP",
	  TLAN_ADAPTER_ACTIVITY_LED,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETELLIGENT_10_100,
	  "Compaq Netelligent 10/100 TX PCI UTP",
	  TLAN_ADAPTER_ACTIVITY_LED,
	  0x83
	}, 
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETFLEX_3P_INTEGRATED,
	  "Compaq Integrated NetFlex-3/P",
	  TLAN_ADAPTER_NONE,
	  0x83
	}, 
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETFLEX_3P,
	  "Compaq NetFlex-3/P",
	  TLAN_ADAPTER_UNMANAGED_PHY | TLAN_ADAPTER_BIT_RATE_PHY,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETFLEX_3P_BNC,
	  "Compaq NetFlex-3/P",
	  TLAN_ADAPTER_NONE,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETELLIGENT_10_100_PROLIANT,
	  "Compaq Netelligent Integrated 10/100 TX UTP",
	  TLAN_ADAPTER_NONE,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETELLIGENT_10_100_DUAL,
	  "Compaq Netelligent Dual 10/100 TX PCI UTP",
	  TLAN_ADAPTER_NONE,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_DESKPRO_4000_5233MMX,
	  "Compaq Netelligent 10/100 TX Embedded UTP",
	  TLAN_ADAPTER_NONE,
	  0x83
	},
	{ PCI_VENDOR_ID_OLICOM,
	  PCI_DEVICE_ID_OLICOM_OC2183,
	  "Olicom OC-2183/2185",
	  TLAN_ADAPTER_USE_INTERN_10,
	  0xF8
	},
	{ PCI_VENDOR_ID_OLICOM,
	  PCI_DEVICE_ID_OLICOM_OC2325,
	  "Olicom OC-2325",
	  TLAN_ADAPTER_UNMANAGED_PHY,
	  0xF8
	},
	{ PCI_VENDOR_ID_OLICOM,
	  PCI_DEVICE_ID_OLICOM_OC2326,
	  "Olicom OC-2326",
	  TLAN_ADAPTER_USE_INTERN_10,
	  0xF8
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETELLIGENT_10_100_WS_5100,
	  "Compaq Netelligent 10/100 TX UTP",
	  TLAN_ADAPTER_ACTIVITY_LED,
	  0x83
	},
	{ PCI_VENDOR_ID_COMPAQ,
	  PCI_DEVICE_ID_NETELLIGENT_10_T2,
	  "Compaq Netelligent 10 T/2 PCI UTP/Coax",
	  TLAN_ADAPTER_NONE,
	  0x83
	},
	{ 0,
	  0,
	  NULL,
	  0,
	  0
	} /* End of List */
};


static int	TLan_PciProbe( u8 *, u8 *, u8 *, u32 *, u32 * );
static int	TLan_Init( struct device * );
static int	TLan_Open(struct device *dev);
static int	TLan_StartTx(struct sk_buff *, struct device *);
static void	TLan_HandleInterrupt(int, void *, struct pt_regs *);
static int	TLan_Close(struct device *);
static struct	net_device_stats *TLan_GetStats( struct device * );
static void	TLan_SetMulticastList( struct device * );

static u32	TLan_HandleInvalid( struct device *, u16 );
static u32	TLan_HandleTxEOF( struct device *, u16 );
static u32	TLan_HandleStatOverflow( struct device *, u16 );
static u32	TLan_HandleRxEOF( struct device *, u16 );
static u32	TLan_HandleDummy( struct device *, u16 );
static u32	TLan_HandleTxEOC( struct device *, u16 );
static u32	TLan_HandleStatusCheck( struct device *, u16 );
static u32	TLan_HandleRxEOC( struct device *, u16 );

static void	TLan_Timer( unsigned long );

static void	TLan_ResetLists( struct device * );
static void	TLan_FreeLists( struct device * );
static void	TLan_PrintDio( u16 );
static void	TLan_PrintList( TLanList *, char *, int );
static void	TLan_ReadAndClearStats( struct device *, int );
static void	TLan_ResetAdapter( struct device * );
static void	TLan_FinishReset( struct device * );
static void	TLan_SetMac( struct device *, int areg, char *mac );

static void	TLan_PhyPrint( struct device * );
static void	TLan_PhyDetect( struct device * );
static void	TLan_PhyPowerDown( struct device * );
static void	TLan_PhyPowerUp( struct device * );
static void	TLan_PhyReset( struct device * );
static void	TLan_PhyStartLink( struct device * );
static void	TLan_PhyFinishAutoNeg( struct device * );
/*
static int	TLan_PhyNop( struct device * );
static int	TLan_PhyInternalCheck( struct device * );
static int	TLan_PhyInternalService( struct device * );
static int	TLan_PhyDp83840aCheck( struct device * );
*/

static int	TLan_MiiReadReg( struct device *, u16, u16, u16 * );
static void	TLan_MiiSendData( u16, u32, unsigned );
static void	TLan_MiiSync( u16 );
static void	TLan_MiiWriteReg( struct device *, u16, u16, u16 );

static void	TLan_EeSendStart( u16 );
static int	TLan_EeSendByte( u16, u8, int );
static void	TLan_EeReceiveByte( u16, u8 *, int );
static int	TLan_EeReadByte( struct device *, u8, u8 * );


static TLanIntVectorFunc *TLanIntVector[TLAN_INT_NUMBER_OF_INTS] = {
	TLan_HandleInvalid,
	TLan_HandleTxEOF,
	TLan_HandleStatOverflow,
	TLan_HandleRxEOF,
	TLan_HandleDummy,
	TLan_HandleTxEOC,
	TLan_HandleStatusCheck,
	TLan_HandleRxEOC
};

static inline void
TLan_SetTimer( struct device *dev, u32 ticks, u32 type )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;

	cli();
	if ( priv->timer.function != NULL ) {
		return;
	}
	priv->timer.function = &TLan_Timer;
	sti();

	priv->timer.data = (unsigned long) dev;
	priv->timer.expires = jiffies + ticks;
	priv->timerSetAt = jiffies;
	priv->timerType = type;
	add_timer( &priv->timer );

} /* TLan_SetTimer */


/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Primary Functions

	These functions are more or less common to all Linux network drivers.

******************************************************************************
*****************************************************************************/


#ifdef MODULE

	/***************************************************************
	 *	init_module
	 *
	 *	Returns:
	 *		0 if module installed ok, non-zero if not.
	 *	Parms:
	 *		None
	 *
	 *	This function begins the setup of the driver creating a
	 *	pad buffer, finding all TLAN devices (matching
	 *	TLanAdapterList entries), and creating and initializing a
	 *	device structure for each adapter.
	 *
	 **************************************************************/

extern int init_module(void)
{
	TLanPrivateInfo	*priv;
	struct device	*dev;
	size_t		dev_size;
	u8		dfn;
	u32		index;
	int		failed;
	int		found;
	u32		io_base;
	u8		irq;
	u8		rev;

	printk( "TLAN driver, v%d.%d, (C) 1997-8 Caldera, Inc.\n",
		TLanVersionMajor,
		TLanVersionMinor
	      );
	TLanPadBuffer = (u8 *) kmalloc( TLAN_MIN_FRAME_SIZE,
					( GFP_KERNEL | GFP_DMA )
				      );
	if ( TLanPadBuffer == NULL ) {
		printk( "TLAN:  Could not allocate memory for pad buffer.\n" );
		return -ENOMEM;
	}

	memset( TLanPadBuffer, 0, TLAN_MIN_FRAME_SIZE );

	dev_size = sizeof(struct device) + sizeof(TLanPrivateInfo);

	while ( ( found = TLan_PciProbe( &dfn, &irq, &rev, &io_base, &index ) ) ) {
		dev = (struct device *) kmalloc( dev_size, GFP_KERNEL );
		if ( dev == NULL ) {
			printk( "TLAN:  Could not allocate memory for device.\n" );
			continue;
		}
		memset( dev, 0, dev_size );

		dev->priv = priv = ( (void *) dev ) + sizeof(struct device);
		dev->name = priv->devName;
		strcpy( priv->devName, "    " );
		dev->base_addr = io_base;
		dev->irq = irq;
		dev->init = TLan_Init;

		priv->adapter =    &TLanAdapterList[index];
		priv->adapterRev = rev;
		priv->aui =        aui;
		if ( ( duplex != 1 ) && ( duplex != 2 ) ) {
			duplex = 0;
		}
		priv->duplex =     duplex;
		if ( ( speed != 10 ) && ( speed != 100 ) ) {
			speed = 0;
		}
		priv->speed =      speed;
		priv->sa_int =     sa_int;
		priv->debug =      debug;

		ether_setup( dev );

		failed = register_netdev( dev );

		if ( failed ) {
			printk( "TLAN:  Could not register device.\n" );
			kfree( dev );
		} else {
			priv->nextDevice = TLanDevices;
			TLanDevices = dev;
			TLanDevicesInstalled++;
			printk("TLAN:  %s irq=%2d io=%04x, %s, Rev. %d\n",
				dev->name,
				(int) dev->irq,
				(int) dev->base_addr,
				priv->adapter->deviceLabel,
				priv->adapterRev );
		}
	}
	
	/* printk( "TLAN:  Found %d device(s).\n", TLanDevicesInstalled ); */

    return ( ( TLanDevicesInstalled >= 0 ) ? 0 : -ENODEV );

} /* init_module */




	/***************************************************************
	 *	cleanup_module
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		None
	 *
	 *	Goes through the TLanDevices list and frees the device
	 *	structs and memory associated with each device (lists
	 *	and buffers).  It also ureserves the IO port regions
	 *	associated with this device.
	 *
	 **************************************************************/

extern void cleanup_module(void)
{
	struct device	*dev;
	TLanPrivateInfo	*priv;

	while ( TLanDevicesInstalled ) {
		dev = TLanDevices;
		priv = (TLanPrivateInfo *) dev->priv;
		if ( priv->dmaStorage ) {
			kfree( priv->dmaStorage );
		}
		release_region( dev->base_addr, 0x10 );
		unregister_netdev( dev );
		TLanDevices = priv->nextDevice;
		kfree( dev );
		TLanDevicesInstalled--;
	}
	kfree( TLanPadBuffer );

} /* cleanup_module */


#else /* MODULE */




	/***************************************************************
	 *	tlan_probe
	 *
	 *	Returns:
	 *		0 on success, error code on error
	 *	Parms:
	 *		dev	device struct to use if adapter is
	 *			found.
	 *
	 *	The name is lower case to fit in with all the rest of
	 *	the netcard_probe names.  This function looks for a/
	 *	another TLan based adapter, setting it up with the
	 *	provided device struct if one is found.
	 *
	 **************************************************************/
	 
extern int tlan_probe( struct device *dev )
{
	TLanPrivateInfo	*priv;
	static int	pad_allocated = 0;
	int		found;
	u8		dfn, irq, rev;
	u32		io_base, index;

	found = TLan_PciProbe( &dfn, &irq, &rev, &io_base, &index );

	if ( ! found ) {
		return -ENODEV;
	}

	dev->priv = kmalloc( sizeof(TLanPrivateInfo), GFP_KERNEL );
	
	if ( dev->priv == NULL ) {
		printk( "TLAN:  Could not allocate memory for device.\n" );
		return  -ENOMEM;
	}

	memset( dev->priv, 0, sizeof(TLanPrivateInfo) );

	if ( ! pad_allocated ) {
		TLanPadBuffer = (u8 *) kmalloc( TLAN_MIN_FRAME_SIZE, 
//						( GFP_KERNEL | GFP_DMA )
						( GFP_KERNEL )
					      );
		if ( TLanPadBuffer == NULL ) {
			printk( "TLAN:  Could not allocate memory for padding.\n" );
			kfree( dev->priv );
			return -ENOMEM;
		} else {
			pad_allocated = 1;
			memset( TLanPadBuffer, 0, TLAN_MIN_FRAME_SIZE );
		}
	}

	priv = (TLanPrivateInfo *) dev->priv;

	dev->name = priv->devName;
	strcpy( priv->devName, "    " );

	dev = init_etherdev( dev, sizeof(TLanPrivateInfo) );

	dev->base_addr = io_base;
	dev->irq = irq;


	priv->adapter =    &TLanAdapterList[index];
	priv->adapterRev = rev;
	priv->aui =        dev->mem_start & 0x01;
	priv->duplex =     ( ( dev->mem_start & 0x0C ) == 0x0C ) ? 0 : ( dev->mem_start & 0x0C ) >> 2;
	priv->speed =      ( ( dev->mem_start & 0x30 ) == 0x30 ) ? 0 : ( dev->mem_start & 0x30 ) >> 4;
	if ( priv->speed == 0x1 ) {
		priv->speed = TLAN_SPEED_10;
	} else if ( priv->speed == 0x2 ) {
		priv->speed = TLAN_SPEED_100;
	}
	priv->sa_int =     dev->mem_start & 0x02;
	priv->debug =      dev->mem_end;


	printk("TLAN %d.%d:  %s irq=%2d io=%04x, %s, Rev. %d\n",
		TLanVersionMajor,
		TLanVersionMinor,
		dev->name, 
		(int) irq, 
		io_base,
		priv->adapter->deviceLabel,
		priv->adapterRev );

	TLan_Init( dev );
			
   	return 0;

} /* tlan_probe */


#endif /* MODULE */




	/***************************************************************
	 *	TLan_PciProbe
	 *
	 *	Returns:
	 *		1 if another TLAN card was found, 0 if not.
	 *	Parms:
	 *		pci_dfn		The PCI whatever the card was
	 *				found at.
	 *		pci_irq		The IRQ of the found adapter.
	 *		pci_rev		The revision of the adapter.
	 *		pci_io_base	The first IO port used by the
	 *				adapter.
	 *		dl_ix		The index in the device list
	 *				of the adapter.
	 *
	 *	This function searches for an adapter with PCI vendor
	 *	and device IDs matching those in the TLanAdapterList.
	 *	The function 'remembers' the last device it found,
	 *	and so finds a new device (if anymore are to be found)
	 *	each time the function is called.  It then looks up
	 *	pertinent PCI info and returns it to the caller.
	 *
	 **************************************************************/

int TLan_PciProbe(u8 *pci_dfn, u8 *pci_irq, u8 *pci_rev, u32 *pci_io_base, u32 *dl_ix )
{
	static int dl_index = 0;
	static struct pci_dev * pdev = NULL;
	u16	pci_command;
	int	reg;


	if ( ! pci_present() ) {
		printk( "TLAN:   PCI Bios not present.\n" );
		return 0;
	}

	for (; TLanAdapterList[dl_index].vendorId != 0; dl_index++) {

		pdev = pci_find_device(
			TLanAdapterList[dl_index].vendorId,
			TLanAdapterList[dl_index].deviceId, pdev);

		if ( pdev ) {

			TLAN_DBG(
				TLAN_DEBUG_GNRL,
				"TLAN:  found: Vendor Id = 0x%hx, Device Id = 0x%hx\n",
				TLanAdapterList[dl_index].vendorId,
				TLanAdapterList[dl_index].deviceId
			);

			*pci_irq = pdev->irq;
			*pci_io_base = pdev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
			*pci_dfn = pdev->devfn;
			pci_read_config_byte ( pdev, PCI_REVISION_ID, pci_rev);
			pci_read_config_word ( pdev,  PCI_COMMAND, &pci_command);

			for ( reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5; reg +=4 ) {
				pci_read_config_dword( pdev, reg, pci_io_base);
				if ((pci_command & PCI_COMMAND_IO) && (*pci_io_base & 0x3)) {
					*pci_io_base &= PCI_BASE_ADDRESS_IO_MASK;
					TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:    IO mapping is available at %x.\n", *pci_io_base);
					break;
				} else {
					*pci_io_base = 0;
				}
			}

			if ( *pci_io_base == 0 )
				printk("TLAN:    IO mapping not available, ignoring device.\n");

			pci_set_master(pdev);

			if ( *pci_io_base ) {
				*dl_ix = dl_index;
				return 1;
			}

		} else {
			pdev = NULL;
		}
	}

	return 0;

} /* TLan_PciProbe */




	/***************************************************************
	 *	TLan_Init
	 *
	 *	Returns:
	 *		0 on success, error code otherwise.
	 *	Parms:
	 *		dev	The structure of the device to be
	 *			init'ed.
	 *
	 *	This function completes the initialization of the
	 *	device structure and driver.  It reserves the IO
	 *	addresses, allocates memory for the lists and bounce
	 *	buffers, retrieves the MAC address from the eeprom
	 *	and assignes the device's methods.
	 *	
	 **************************************************************/

int TLan_Init( struct device *dev )
{
	int		dma_size;
	int		err;
	int		i;
	TLanPrivateInfo	*priv;

	priv = (TLanPrivateInfo *) dev->priv;

	err = check_region( dev->base_addr, 0x10 );
	if ( err ) {
		printk( "TLAN:  %s: Io port region 0x%lx size 0x%x in use.\n",
			dev->name,
			dev->base_addr,
			0x10 );
		return -EIO;
	}
	request_region( dev->base_addr, 0x10, TLanSignature );

	if ( bbuf ) {
		dma_size = ( TLAN_NUM_RX_LISTS + TLAN_NUM_TX_LISTS )
	           * ( sizeof(TLanList) + TLAN_MAX_FRAME_SIZE );
	} else {
		dma_size = ( TLAN_NUM_RX_LISTS + TLAN_NUM_TX_LISTS )
	           * ( sizeof(TLanList) );
	}

	priv->dmaStorage = kmalloc( dma_size, GFP_KERNEL | GFP_DMA );
	if ( priv->dmaStorage == NULL ) {
		printk( "TLAN:  Could not allocate lists and buffers for %s.\n",
			dev->name );
		return -ENOMEM;
	}
	memset( priv->dmaStorage, 0, dma_size );
	priv->rxList = (TLanList *) 
		       ( ( ( (u32) priv->dmaStorage ) + 7 ) & 0xFFFFFFF8 );
	priv->txList = priv->rxList + TLAN_NUM_RX_LISTS;

	if ( bbuf ) {
		priv->rxBuffer = (u8 *) ( priv->txList + TLAN_NUM_TX_LISTS );
		priv->txBuffer = priv->rxBuffer
				 + ( TLAN_NUM_RX_LISTS * TLAN_MAX_FRAME_SIZE );
	}

	err = 0;
	for ( i = 0;  i < 6 ; i++ )
		err |= TLan_EeReadByte( dev,
					(u8) priv->adapter->addrOfs + i,
					(u8 *) &dev->dev_addr[i] );
	if ( err ) {
		printk( "TLAN:  %s: Error reading MAC from eeprom: %d\n",
			dev->name,
			err );
	}

	dev->addr_len = 6;

	dev->open = &TLan_Open;
	dev->hard_start_xmit = &TLan_StartTx;
	dev->stop = &TLan_Close;
	dev->get_stats = &TLan_GetStats;
	dev->set_multicast_list = &TLan_SetMulticastList;


	return 0;

} /* TLan_Init */




	/***************************************************************
	 *	TLan_Open
	 *
	 *	Returns:
	 *		0 on success, error code otherwise.
	 *	Parms:
	 *		dev	Structure of device to be opened.
	 *
	 *	This routine puts the driver and TLAN adapter in a
	 *	state where it is ready to send and receive packets.
	 *	It allocates the IRQ, resets and brings the adapter
	 *	out of reset, and allows interrupts.  It also delays
	 *	the startup for autonegotiation or sends a Rx GO
	 *	command to the adapter, as appropriate.
	 *
	 **************************************************************/

int TLan_Open( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	int		err;

	priv->tlanRev = TLan_DioRead8( dev->base_addr, TLAN_DEF_REVISION );
	if ( priv->sa_int ) {
		TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:   Using SA_INTERRUPT\n" ); 
		err = request_irq( dev->irq, TLan_HandleInterrupt, SA_SHIRQ | SA_INTERRUPT, TLanSignature, dev );
	} else {
		err = request_irq( dev->irq, TLan_HandleInterrupt, SA_SHIRQ, TLanSignature, dev );
	}
	if ( err ) {
		printk( "TLAN:  Cannot open %s because IRQ %d is already in use.\n", dev->name, dev->irq );
		return -EAGAIN;
	}
	
	MOD_INC_USE_COUNT;

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* NOTE: It might not be necessary to read the stats before a
			 reset if you don't care what the values are.
	*/
	TLan_ResetLists( dev );
	TLan_ReadAndClearStats( dev, TLAN_IGNORE );
	TLan_ResetAdapter( dev );

	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Opened.  TLAN Chip Rev: %x\n", dev->name, priv->tlanRev );

	return 0;

} /* TLan_Open */




	/***************************************************************
	 *	TLan_StartTx
	 *  
	 *	Returns:
	 *		0 on success, non-zero on failure.
	 *	Parms:
	 *		skb	A pointer to the sk_buff containing the
	 *			frame to be sent.
	 *		dev	The device to send the data on.
	 *
	 *	This function adds a frame to the Tx list to be sent
	 *	ASAP.  First it	verifies that the adapter is ready and
	 *	there is room in the queue.  Then it sets up the next
	 *	available list, copies the frame to the	corresponding
	 *	buffer.  If the adapter Tx channel is idle, it gives
	 *	the adapter a Tx Go command on the list, otherwise it
	 *	sets the forward address of the previous list to point
	 *	to this one.  Then it frees the sk_buff.
	 *
	 **************************************************************/

int TLan_StartTx( struct sk_buff *skb, struct device *dev )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;
	TLanList	*tail_list;
	u8		*tail_buffer;
	int		pad;

	if ( ! priv->phyOnline ) {
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  %s PHY is not ready\n", dev->name );
		dev_kfree_skb( skb );
		return 0;
	}

	tail_list = priv->txList + priv->txTail;

	if ( tail_list->cStat != TLAN_CSTAT_UNUSED ) {
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  %s is busy (Head=%d Tail=%d)\n", dev->name, priv->txHead, priv->txTail );
		dev->tbusy = 1;
		priv->txBusyCount++;
		return 1;
	}

	tail_list->forward = 0;

	if ( bbuf ) {
		tail_buffer = priv->txBuffer + ( priv->txTail * TLAN_MAX_FRAME_SIZE );
		memcpy( tail_buffer, skb->data, skb->len );
	} else {
		tail_list->buffer[0].address = virt_to_bus( skb->data );
		tail_list->buffer[9].address = (u32) skb;
	}

	pad = TLAN_MIN_FRAME_SIZE - skb->len;

	if ( pad > 0 ) {
		tail_list->frameSize = (u16) skb->len + pad;
		tail_list->buffer[0].count = (u32) skb->len;
		tail_list->buffer[1].count = TLAN_LAST_BUFFER | (u32) pad;
		tail_list->buffer[1].address = virt_to_bus( TLanPadBuffer );
	} else {
		tail_list->frameSize = (u16) skb->len;
		tail_list->buffer[0].count = TLAN_LAST_BUFFER | (u32) skb->len;
		tail_list->buffer[1].count = 0;
		tail_list->buffer[1].address = 0;
	}

	cli();
	tail_list->cStat = TLAN_CSTAT_READY;
	if ( ! priv->txInProgress ) {
		priv->txInProgress = 1;
		outw( 0x4, dev->base_addr + TLAN_HOST_INT );
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  Starting TX on buffer %d\n", priv->txTail );
		outl( virt_to_bus( tail_list ), dev->base_addr + TLAN_CH_PARM );
		outl( TLAN_HC_GO | TLAN_HC_ACK, dev->base_addr + TLAN_HOST_CMD );
	} else {
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  Adding buffer %d to TX channel\n", priv->txTail );
		if ( priv->txTail == 0 ) {
			( priv->txList + ( TLAN_NUM_TX_LISTS - 1 ) )->forward = virt_to_bus( tail_list );
		} else {
			( priv->txList + ( priv->txTail - 1 ) )->forward = virt_to_bus( tail_list );
		}
	}
	sti();

	CIRC_INC( priv->txTail, TLAN_NUM_TX_LISTS );

	if ( bbuf ) {
		dev_kfree_skb( skb );
	}
		
	dev->trans_start = jiffies;
	return 0;

} /* TLan_StartTx */




	/***************************************************************
	 *	TLan_HandleInterrupt
	 *  
	 *	Returns:	
	 *		Nothing
	 *	Parms:
	 *		irq	The line on which the interrupt
	 *			occurred.
	 *		dev_id	A pointer to the device assigned to
	 *			this irq line.
	 *		regs	???
	 *
	 *	This function handles an interrupt generated by its
	 *	assigned TLAN adapter.  The function deactivates
	 *	interrupts on its adapter, records the type of
	 *	interrupt, executes the appropriate subhandler, and
	 *	acknowdges the interrupt to the adapter (thus
	 *	re-enabling adapter interrupts.
	 *
	 **************************************************************/

void TLan_HandleInterrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32		ack;
	struct device	*dev;
	u32		host_cmd;
	u16		host_int;
	int		type;

	dev = (struct device *) dev_id;

	cli();
	if ( dev->interrupt ) {
		printk( "TLAN:   Re-entering interrupt handler for %s: %ld.\n" , dev->name, dev->interrupt );
	}
	dev->interrupt++;

	host_int = inw( dev->base_addr + TLAN_HOST_INT );
	outw( host_int, dev->base_addr + TLAN_HOST_INT );

	type = ( host_int & TLAN_HI_IT_MASK ) >> 2;

	ack = TLanIntVector[type]( dev, host_int );

	if ( ack ) {
		host_cmd = TLAN_HC_ACK | ack | ( type << 18 );
		outl( host_cmd, dev->base_addr + TLAN_HOST_CMD );
	}

	dev->interrupt--;
	sti();

} /* TLan_HandleInterrupts */




	/***************************************************************
	 *	TLan_Close
	 *  
	 * 	Returns:
	 *		An error code.
	 *	Parms:
	 *		dev	The device structure of the device to
	 *			close.
	 *
	 *	This function shuts down the adapter.  It records any
	 *	stats, puts the adapter into reset state, deactivates
	 *	its time as needed, and	frees the irq it is using.
	 *
	 **************************************************************/

int TLan_Close(struct device *dev)
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;

	dev->start = 0;
	dev->tbusy = 1;

	TLan_ReadAndClearStats( dev, TLAN_RECORD );
	outl( TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD );
	if ( priv->timer.function != NULL )
		del_timer( &priv->timer );
	free_irq( dev->irq, dev );
	TLan_FreeLists( dev );
	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  Device %s closed.\n", dev->name );

	MOD_DEC_USE_COUNT;

	return 0;

} /* TLan_Close */




	/***************************************************************
	 *	TLan_GetStats
	 *  
	 *	Returns:
	 *		A pointer to the device's statistics structure.
	 *	Parms:
	 *		dev	The device structure to return the
	 *			stats for.
	 *
	 *	This function updates the devices statistics by reading
	 *	the TLAN chip's onboard registers.  Then it returns the
	 *	address of the statistics structure.
	 *
	 **************************************************************/

struct net_device_stats *TLan_GetStats( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	int i;

	/* Should only read stats if open ? */
	TLan_ReadAndClearStats( dev, TLAN_RECORD );

	TLAN_DBG( TLAN_DEBUG_RX, "TLAN RECEIVE:  %s EOC count = %d\n", dev->name, priv->rxEocCount );
	TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  %s Busy count = %d\n", dev->name, priv->txBusyCount );
	if ( debug & TLAN_DEBUG_GNRL ) {
		TLan_PrintDio( dev->base_addr );
		TLan_PhyPrint( dev );
	}
	if ( debug & TLAN_DEBUG_LIST ) {
		for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ )
			TLan_PrintList( priv->rxList + i, "RX", i );
		for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ )
			TLan_PrintList( priv->txList + i, "TX", i );
	}
	
	return ( &( (TLanPrivateInfo *) dev->priv )->stats );

} /* TLan_GetStats */




	/***************************************************************
	 *	TLan_SetMulticastList
	 *  
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	The device structure to set the
	 *			multicast list for.
	 *
	 *	This function sets the TLAN adaptor to various receive
	 *	modes.  If the IFF_PROMISC flag is set, promiscuous
	 *	mode is acitviated.  Otherwise,	promiscuous mode is
	 *	turned off.  If the IFF_ALLMULTI flag is set, then
	 *	the hash table is set to receive all group addresses.
	 *	Otherwise, the first three multicast addresses are
	 *	stored in AREG_1-3, and the rest are selected via the
	 *	hash table, as necessary.
	 *
	 **************************************************************/

void TLan_SetMulticastList( struct device *dev )
{	
	struct dev_mc_list	*dmi = dev->mc_list;
	u32			hash1 = 0;
	u32			hash2 = 0;
	int			i;
	u32			offset;
	u8			tmp;

	if ( dev->flags & IFF_PROMISC ) {
		tmp = TLan_DioRead8( dev->base_addr, TLAN_NET_CMD );
		TLan_DioWrite8( dev->base_addr, TLAN_NET_CMD, tmp | TLAN_NET_CMD_CAF );
	} else {
		tmp = TLan_DioRead8( dev->base_addr, TLAN_NET_CMD );
		TLan_DioWrite8( dev->base_addr, TLAN_NET_CMD, tmp & ~TLAN_NET_CMD_CAF );
		if ( dev->flags & IFF_ALLMULTI ) {
			for ( i = 0; i < 3; i++ ) 
				TLan_SetMac( dev, i + 1, NULL );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_1, 0xFFFFFFFF );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_2, 0xFFFFFFFF );
		} else {
			for ( i = 0; i < dev->mc_count; i++ ) {
				if ( i < 3 ) {
					TLan_SetMac( dev, i + 1, (char *) &dmi->dmi_addr );
				} else {
					offset = TLan_HashFunc( (u8 *) &dmi->dmi_addr );
					if ( offset < 32 ) 
						hash1 |= ( 1 << offset );
					else
						hash2 |= ( 1 << ( offset - 32 ) );
				}
				dmi = dmi->next;
			}
			for ( ; i < 3; i++ ) 
				TLan_SetMac( dev, i + 1, NULL );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_1, hash1 );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_2, hash2 );
		}
	}

} /* TLan_SetMulticastList */



/*****************************************************************************
******************************************************************************

        ThunderLAN Driver Interrupt Vectors and Table

	Please see Chap. 4, "Interrupt Handling" of the "ThunderLAN
	Programmer's Guide" for more informations on handling interrupts
	generated by TLAN based adapters.  

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_HandleInvalid
	 *
	 *	Returns:
	 *		0
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles invalid interrupts.  This should
	 *	never happen unless some other adapter is trying to use
	 *	the IRQ line assigned to the device.
	 *
	 **************************************************************/

u32 TLan_HandleInvalid( struct device *dev, u16 host_int )
{
	host_int = 0;
	/* printk( "TLAN:  Invalid interrupt on %s.\n", dev->name ); */
	return 0;

} /* TLan_HandleInvalid */




	/***************************************************************
	 *	TLan_HandleTxEOF
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles Tx EOF interrupts which are raised
	 *	by the adapter when it has completed sending the
	 *	contents of a buffer.  If detemines which list/buffer
	 *	was completed and resets it.  If the buffer was the last
	 *	in the channel (EOC), then the function checks to see if
	 *	another buffer is ready to send, and if so, sends a Tx
	 *	Go command.  Finally, the driver activates/continues the
	 *	activity LED.
	 *
	 **************************************************************/

u32 TLan_HandleTxEOF( struct device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	int		eoc = 0;
	TLanList	*head_list;
	u32		ack = 1;

	TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  Handling TX EOF (Head=%d Tail=%d)\n", priv->txHead, priv->txTail );
	host_int = 0;
	head_list = priv->txList + priv->txHead;

	if ( ! bbuf ) {
		dev_kfree_skb( (struct sk_buff *) head_list->buffer[9].address );
		head_list->buffer[9].address = 0;
	}

	if ( head_list->cStat & TLAN_CSTAT_EOC )
		eoc = 1;
	if ( ! head_list->cStat & TLAN_CSTAT_FRM_CMP ) {
		printk( "TLAN:  Received interrupt for uncompleted TX frame.\n" );
	}

#if LINUX_KERNEL_VERSION > 0x20100
	priv->stats->tx_bytes += head_list->frameSize;
#endif

	head_list->cStat = TLAN_CSTAT_UNUSED;
	dev->tbusy = 0;
	CIRC_INC( priv->txHead, TLAN_NUM_TX_LISTS );
	if ( eoc ) {
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  Handling TX EOC (Head=%d Tail=%d)\n", priv->txHead, priv->txTail );
		head_list = priv->txList + priv->txHead;
		if ( ( head_list->cStat & TLAN_CSTAT_READY ) == TLAN_CSTAT_READY ) {
			outl( virt_to_bus( head_list ), dev->base_addr + TLAN_CH_PARM );
			ack |= TLAN_HC_GO;
		} else {
			priv->txInProgress = 0;
		}
	}

	if ( priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED ) {
		TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT );
		if ( priv->timer.function == NULL ) {
			TLan_SetTimer( dev, TLAN_TIMER_ACT_DELAY, TLAN_TIMER_ACTIVITY );
		} else if ( priv->timerType == TLAN_TIMER_ACTIVITY ) {
			priv->timerSetAt = jiffies;
		}
	}

	return ack;

} /* TLan_HandleTxEOF */




	/***************************************************************
	 *	TLan_HandleStatOverflow
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles the Statistics Overflow interrupt
	 *	which means that one or more of the TLAN statistics
	 *	registers has reached 1/2 capacity and needs to be read.
	 *
	 **************************************************************/

u32 TLan_HandleStatOverflow( struct device *dev, u16 host_int )
{
	host_int = 0;
	TLan_ReadAndClearStats( dev, TLAN_RECORD );

	return 1;

} /* TLan_HandleStatOverflow */




	/***************************************************************
	 *	TLan_HandleRxEOF
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles the Rx EOF interrupt which
	 *	indicates a frame has been received by the adapter from
	 *	the net and the frame has been transferred to memory.
	 *	The function determines the bounce buffer the frame has
	 *	been loaded into, creates a new sk_buff big enough to
	 *	hold the frame, and sends it to protocol stack.  It
	 *	then resets the used buffer and appends it to the end
	 *	of the list.  If the frame was the last in the Rx
	 *	channel (EOC), the function restarts the receive channel
	 *	by sending an Rx Go command to the adapter.  Then it
	 *	activates/continues the the activity LED.
	 *
	 **************************************************************/

u32 TLan_HandleRxEOF( struct device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u32		ack = 1;
	int		eoc = 0;
	u8		*head_buffer;
	TLanList	*head_list;
	struct sk_buff	*skb;
	TLanList	*tail_list;
	void		*t;

	TLAN_DBG( TLAN_DEBUG_RX, "TLAN RECEIVE:  Handling RX EOF (Head=%d Tail=%d)\n", priv->rxHead, priv->rxTail );
	host_int = 0;
	head_list = priv->rxList + priv->rxHead;
	tail_list = priv->rxList + priv->rxTail;

	if ( head_list->cStat & TLAN_CSTAT_EOC ) {
		eoc = 1;
	}

	if ( ! head_list->cStat & TLAN_CSTAT_FRM_CMP ) {
		printk( "TLAN:  Received interrupt for uncompleted RX frame.\n" );
	} else if ( bbuf ) {
		skb = dev_alloc_skb( head_list->frameSize + 7 );
		if ( skb == NULL ) { 
			printk( "TLAN:  Couldn't allocate memory for received data.\n" );
		} else {
			head_buffer = priv->rxBuffer + ( priv->rxHead * TLAN_MAX_FRAME_SIZE );
			skb->dev = dev;
			skb_reserve( skb, 2 );
			t = (void *) skb_put( skb, head_list->frameSize );

#if LINUX_KERNEL_VERSION > 0x20100
			priv->stats->rx_bytes += head_list->frameSize;
#endif

			memcpy( t, head_buffer, head_list->frameSize );
			skb->protocol = eth_type_trans( skb, dev );
			netif_rx( skb );
		}
	} else {
		skb = (struct sk_buff *) head_list->buffer[9].address;
		head_list->buffer[9].address = 0;
		skb_trim( skb, head_list->frameSize );

#if LINUX_KERNEL_VERSION > 0x20100
			priv->stats->rx_bytes += head_list->frameSize;
#endif

		skb->protocol = eth_type_trans( skb, dev );
		netif_rx( skb );

		skb = dev_alloc_skb( TLAN_MAX_FRAME_SIZE + 7 );
		if ( skb == NULL ) {
			printk( "TLAN:  Couldn't allocate memory for received data.\n" );
			/* If this ever happened it would be a problem */
		} else {
			skb->dev = dev;
			skb_reserve( skb, 2 );
			t = (void *) skb_put( skb, TLAN_MAX_FRAME_SIZE );
			head_list->buffer[0].address = virt_to_bus( t );
			head_list->buffer[9].address = (u32) skb;
		}
	}

	head_list->forward = 0;
	head_list->frameSize = TLAN_MAX_FRAME_SIZE;
	head_list->buffer[0].count = TLAN_MAX_FRAME_SIZE | TLAN_LAST_BUFFER;
	tail_list->forward = virt_to_bus( head_list );

	CIRC_INC( priv->rxHead, TLAN_NUM_RX_LISTS );
	CIRC_INC( priv->rxTail, TLAN_NUM_RX_LISTS );

	if ( eoc ) { 
		TLAN_DBG( TLAN_DEBUG_RX, "TLAN RECEIVE:  Handling RX EOC (Head=%d Tail=%d)\n", priv->rxHead, priv->rxTail );
		head_list = priv->rxList + priv->rxHead;
		outl( virt_to_bus( head_list ), dev->base_addr + TLAN_CH_PARM );
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rxEocCount++;
	}

	if ( priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED ) {
		TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT );
		if ( priv->timer.function == NULL )  {
			TLan_SetTimer( dev, TLAN_TIMER_ACT_DELAY, TLAN_TIMER_ACTIVITY );
		} else if ( priv->timerType == TLAN_TIMER_ACTIVITY ) {
			priv->timerSetAt = jiffies;
		}
	}

	dev->last_rx = jiffies;

	return ack;

} /* TLan_HandleRxEOF */




	/***************************************************************
	 *	TLan_HandleDummy
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles the Dummy interrupt, which is
	 *	raised whenever a test interrupt is generated by setting
	 *	the Req_Int bit of HOST_CMD to 1.
	 *
	 **************************************************************/

u32 TLan_HandleDummy( struct device *dev, u16 host_int )
{
	host_int = 0;
	printk( "TLAN:  Test interrupt on %s.\n", dev->name );
	return 1;

} /* TLan_HandleDummy */




	/***************************************************************
	 *	TLan_HandleTxEOC
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This driver is structured to determine EOC occurances by
	 *	reading the CSTAT member of the list structure.  Tx EOC
	 *	interrupts are disabled via the DIO INTDIS register.
	 *	However, TLAN chips before revision 3.0 didn't have this
	 *	functionality, so process EOC events if this is the
	 *	case.
	 *
	 **************************************************************/

u32 TLan_HandleTxEOC( struct device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	TLanList		*head_list;
	u32				ack = 1;

	host_int = 0;
	if ( priv->tlanRev < 0x30 ) {
		TLAN_DBG( TLAN_DEBUG_TX, "TLAN TRANSMIT:  Handling TX EOC (Head=%d Tail=%d) -- IRQ\n", priv->txHead, priv->txTail );
		head_list = priv->txList + priv->txHead;
		if ( ( head_list->cStat & TLAN_CSTAT_READY ) == TLAN_CSTAT_READY ) {
			outl( virt_to_bus( head_list ), dev->base_addr + TLAN_CH_PARM );
			ack |= TLAN_HC_GO;
		} else {
			priv->txInProgress = 0;
		}
	}

	return ack;

} /* TLan_HandleTxEOC */




	/***************************************************************
	 *	TLan_HandleStatusCheck
	 *
	 *	Returns:
	 *		0 if Adapter check, 1 if Network Status check.
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This function handles Adapter Check/Network Status
	 *	interrupts generated by the adapter.  It checks the
	 *	vector in the HOST_INT register to determine if it is
	 *	an Adapter Check interrupt.  If so, it resets the
	 *	adapter.  Otherwise it clears the status registers
	 *	and services the PHY.
	 *
	 **************************************************************/

u32 TLan_HandleStatusCheck( struct device *dev, u16 host_int )
{	
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u32		ack;
	u32		error;
	u8		net_sts;
	u32		phy;
	u16		tlphy_ctl;
	u16		tlphy_sts;
	
	ack = 1;
	if ( host_int & TLAN_HI_IV_MASK ) {
		error = inl( dev->base_addr + TLAN_CH_PARM );
		printk( "TLAN:  %s: Adaptor Error = 0x%x\n", dev->name, error );
		TLan_ReadAndClearStats( dev, TLAN_RECORD );
		outl( TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD );
		TLan_FreeLists( dev );
		TLan_ResetLists( dev );
		TLan_ResetAdapter( dev );
		dev->tbusy = 0;
		ack = 0;
	} else {
		TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Status Check\n", dev->name );
		phy = priv->phy[priv->phyNum];

		net_sts = TLan_DioRead8( dev->base_addr, TLAN_NET_STS );
		if ( net_sts ) {
			TLan_DioWrite8( dev->base_addr, TLAN_NET_STS, net_sts );
			TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s:    Net_Sts = %x\n", dev->name, (unsigned) net_sts );
		}
		if ( ( net_sts & TLAN_NET_STS_MIRQ ) &&  ( priv->phyNum == 0 ) ) {
			TLan_MiiReadReg( dev, phy, TLAN_TLPHY_STS, &tlphy_sts );
			TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl );
        		if ( ! ( tlphy_sts & TLAN_TS_POLOK ) && ! ( tlphy_ctl & TLAN_TC_SWAPOL ) ) {
                		tlphy_ctl |= TLAN_TC_SWAPOL;
                		TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl);
        		} else if ( ( tlphy_sts & TLAN_TS_POLOK ) && ( tlphy_ctl & TLAN_TC_SWAPOL ) ) {
                		tlphy_ctl &= ~TLAN_TC_SWAPOL;
                		TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl);
        		}

			if (debug) {
				TLan_PhyPrint( dev );
			}
		}
	}

	return ack;

} /* TLan_HandleStatusCheck */




	/***************************************************************
	 *	TLan_HandleRxEOC
	 *
	 *	Returns:
	 *		1
	 *	Parms:
	 *		dev		Device assigned the IRQ that was
	 *				raised.
	 *		host_int	The contents of the HOST_INT
	 *				port.
	 *
	 *	This driver is structured to determine EOC occurances by
	 *	reading the CSTAT member of the list structure.  Rx EOC
	 *	interrupts are disabled via the DIO INTDIS register.
	 *	However, TLAN chips before revision 3.0 didn't have this
	 *	CSTAT member or a INTDIS register, so if this chip is
	 *	pre-3.0, process EOC interrupts normally.
	 *
	 **************************************************************/

u32 TLan_HandleRxEOC( struct device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	TLanList	*head_list;
	u32		ack = 1;

	host_int = 0;
	if (  priv->tlanRev < 0x30 ) {
		TLAN_DBG( TLAN_DEBUG_RX, "TLAN RECEIVE:  Handling RX EOC (Head=%d Tail=%d) -- IRQ\n", priv->rxHead, priv->rxTail );
		head_list = priv->rxList + priv->rxHead;
		outl( virt_to_bus( head_list ), dev->base_addr + TLAN_CH_PARM );
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rxEocCount++;
	}

	return ack;

} /* TLan_HandleRxEOC */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Timer Function

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_Timer
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		data	A value given to add timer when
	 *			add_timer was called.
	 *
	 *	This function handles timed functionality for the
	 *	TLAN driver.  The two current timer uses are for
	 *	delaying for autonegotionation and driving the ACT LED.
	 *	-	Autonegotiation requires being allowed about
	 *		2 1/2 seconds before attempting to transmit a
	 *		packet.  It would be a very bad thing to hang
	 *		the kernel this long, so the driver doesn't
	 *		allow transmission 'til after this time, for
	 *		certain PHYs.  It would be much nicer if all
	 *		PHYs were interrupt-capable like the internal
	 *		PHY.
	 *	-	The ACT LED, which shows adapter activity, is
	 *		driven by the driver, and so must be left on
	 *		for a short period to power up the LED so it
	 *		can be seen.  This delay can be changed by
	 *		changing the TLAN_TIMER_ACT_DELAY in tlan.h,
	 *		if desired.  10 jiffies produces a slightly
	 *		sluggish response.
	 *
	 **************************************************************/

void TLan_Timer( unsigned long data )
{
	struct device	*dev = (struct device *) data;
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u32		elapsed;

	priv->timer.function = NULL;

	switch ( priv->timerType ) {
		case TLAN_TIMER_PHY_PDOWN:
			TLan_PhyPowerDown( dev );
			break;
		case TLAN_TIMER_PHY_PUP:
			TLan_PhyPowerUp( dev );
			break;
		case TLAN_TIMER_PHY_RESET:
			TLan_PhyReset( dev );
			break;
		case TLAN_TIMER_PHY_START_LINK:
			TLan_PhyStartLink( dev );
			break;
		case TLAN_TIMER_PHY_FINISH_AN:
			TLan_PhyFinishAutoNeg( dev );
			break;
		case TLAN_TIMER_FINISH_RESET:
			TLan_FinishReset( dev );
			break;
		case TLAN_TIMER_ACTIVITY:
			cli();
			if ( priv->timer.function == NULL ) {
				elapsed = jiffies - priv->timerSetAt;
				if ( elapsed >= TLAN_TIMER_ACT_DELAY ) {
					TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK );
				} else  {
					priv->timer.function = &TLan_Timer;
					priv->timer.expires = priv->timerSetAt + TLAN_TIMER_ACT_DELAY;
					sti();
					add_timer( &priv->timer );
				}
			}
			sti();
			break;
		default:
			break;
	}

} /* TLan_Timer */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Adapter Related Routines

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_ResetLists
	 *  
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	The device structure with the list
	 *			stuctures to be reset.
	 *
	 *	This routine sets the variables associated with managing
	 *	the TLAN lists to their initial values.
	 *
	 **************************************************************/

void TLan_ResetLists( struct device *dev )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;
	int		i;
	TLanList	*list;
	struct sk_buff	*skb;
	void		*t = NULL;

	priv->txHead = 0;
	priv->txTail = 0;
	for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ ) {
		list = priv->txList + i;
		list->cStat = TLAN_CSTAT_UNUSED;
		if ( bbuf ) {
			list->buffer[0].address = virt_to_bus( priv->txBuffer + ( i * TLAN_MAX_FRAME_SIZE ) );
		} else {
			list->buffer[0].address = 0;
		}
		list->buffer[2].count = 0;
		list->buffer[2].address = 0;
	}

	priv->rxHead = 0;
	priv->rxTail = TLAN_NUM_RX_LISTS - 1;
	for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ ) {
		list = priv->rxList + i;
		list->cStat = TLAN_CSTAT_READY;
		list->frameSize = TLAN_MAX_FRAME_SIZE;
		list->buffer[0].count = TLAN_MAX_FRAME_SIZE | TLAN_LAST_BUFFER;
		if ( bbuf ) {
			list->buffer[0].address = virt_to_bus( priv->rxBuffer + ( i * TLAN_MAX_FRAME_SIZE ) );
		} else {
			skb = dev_alloc_skb( TLAN_MAX_FRAME_SIZE + 7 );
			if ( skb == NULL ) {
				printk( "TLAN:  Couldn't allocate memory for received data.\n" );
				/* If this ever happened it would be a problem */
			} else {
				skb->dev = dev;
				skb_reserve( skb, 2 );
				t = (void *) skb_put( skb, TLAN_MAX_FRAME_SIZE );
			}
			list->buffer[0].address = virt_to_bus( t );
			list->buffer[9].address = (u32) skb;
		}
		list->buffer[1].count = 0;
		list->buffer[1].address = 0;
		if ( i < TLAN_NUM_RX_LISTS - 1 )
			list->forward = virt_to_bus( list + 1 );
		else
			list->forward = 0;
	}

} /* TLan_ResetLists */


void TLan_FreeLists( struct device *dev )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;
	int		i;
	TLanList	*list;
	struct sk_buff	*skb;

	if ( ! bbuf ) {
		for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ ) {
			list = priv->txList + i;
			skb = (struct sk_buff *) list->buffer[9].address;
			if ( skb ) {
				dev_kfree_skb( skb );
				list->buffer[9].address = 0;
			}
		}

		for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ ) {
			list = priv->rxList + i;
			skb = (struct sk_buff *) list->buffer[9].address;
			if ( skb ) {
				dev_kfree_skb( skb );
				list->buffer[9].address = 0;
			}
		}
	}

} /* TLan_FreeLists */




	/***************************************************************
	 *	TLan_PrintDio
	 *  
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		io_base		Base IO port of the device of
	 *				which to print DIO registers.
	 *
	 *	This function prints out all the the internal (DIO)
	 *	registers of a TLAN chip.
	 *
	 **************************************************************/

void TLan_PrintDio( u16 io_base )
{
	u32 data0, data1;
	int	i;

	printk( "TLAN:   Contents of internal registers for io base 0x%04hx.\n", io_base );
	printk( "TLAN:      Off.  +0         +4\n" );
	for ( i = 0; i < 0x4C; i+= 8 ) {
		data0 = TLan_DioRead32( io_base, i );
		data1 = TLan_DioRead32( io_base, i + 0x4 );
		printk( "TLAN:      0x%02x  0x%08x 0x%08x\n", i, data0, data1 );
	}

} /* TLan_PrintDio */




	/***************************************************************
	 *	TLan_PrintList
	 *  
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		list	A pointer to the TLanList structure to
	 *			be printed.
	 *		type	A string to designate type of list,
	 *			"Rx" or "Tx".
	 *		num	The index of the list.
	 *
	 *	This function prints out the contents of the list
	 *	pointed to by the list parameter.
	 *
	 **************************************************************/

void TLan_PrintList( TLanList *list, char *type, int num)
{
	int i;

	printk( "TLAN:   %s List %d at 0x%08x\n", type, num, (u32) list );
	printk( "TLAN:      Forward    = 0x%08x\n",  list->forward );
	printk( "TLAN:      CSTAT      = 0x%04hx\n", list->cStat );
	printk( "TLAN:      Frame Size = 0x%04hx\n", list->frameSize );
	/* for ( i = 0; i < 10; i++ ) { */
	for ( i = 0; i < 2; i++ ) {
		printk( "TLAN:      Buffer[%d].count, addr = 0x%08x, 0x%08x\n", i, list->buffer[i].count, list->buffer[i].address );
	}

} /* TLan_PrintList */




	/***************************************************************
	 *	TLan_ReadAndClearStats
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	Pointer to device structure of adapter
	 *			to which to read stats.
	 *		record	Flag indicating whether to add 
	 *
	 *	This functions reads all the internal status registers
	 *	of the TLAN chip, which clears them as a side effect.
	 *	It then either adds the values to the device's status
	 *	struct, or discards them, depending on whether record
	 *	is TLAN_RECORD (!=0)  or TLAN_IGNORE (==0).
	 *
	 **************************************************************/

void TLan_ReadAndClearStats( struct device *dev, int record )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u32		tx_good, tx_under;
	u32		rx_good, rx_over;
	u32		def_tx, crc, code;
	u32		multi_col, single_col;
	u32		excess_col, late_col, loss;

	outw( TLAN_GOOD_TX_FRMS, dev->base_addr + TLAN_DIO_ADR );
	tx_good  = inb( dev->base_addr + TLAN_DIO_DATA );
	tx_good += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	tx_good += inb( dev->base_addr + TLAN_DIO_DATA + 2 ) << 16;
	tx_under = inb( dev->base_addr + TLAN_DIO_DATA + 3 );

	outw( TLAN_GOOD_RX_FRMS, dev->base_addr + TLAN_DIO_ADR );
	rx_good  = inb( dev->base_addr + TLAN_DIO_DATA );
	rx_good += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	rx_good += inb( dev->base_addr + TLAN_DIO_DATA + 2 ) << 16;
	rx_over  = inb( dev->base_addr + TLAN_DIO_DATA + 3 );
		
	outw( TLAN_DEFERRED_TX, dev->base_addr + TLAN_DIO_ADR );
	def_tx  = inb( dev->base_addr + TLAN_DIO_DATA );
	def_tx += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	crc     = inb( dev->base_addr + TLAN_DIO_DATA + 2 );
	code    = inb( dev->base_addr + TLAN_DIO_DATA + 3 );
	
	outw( TLAN_MULTICOL_FRMS, dev->base_addr + TLAN_DIO_ADR );
	multi_col   = inb( dev->base_addr + TLAN_DIO_DATA );
	multi_col  += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	single_col  = inb( dev->base_addr + TLAN_DIO_DATA + 2 );
	single_col += inb( dev->base_addr + TLAN_DIO_DATA + 3 ) << 8;

	outw( TLAN_EXCESSCOL_FRMS, dev->base_addr + TLAN_DIO_ADR );
	excess_col = inb( dev->base_addr + TLAN_DIO_DATA );
	late_col   = inb( dev->base_addr + TLAN_DIO_DATA + 1 );
	loss       = inb( dev->base_addr + TLAN_DIO_DATA + 2 );

	if ( record ) {
		priv->stats.rx_packets += rx_good;
		priv->stats.rx_errors  += rx_over + crc + code;
		priv->stats.tx_packets += tx_good;
		priv->stats.tx_errors  += tx_under + loss;
		priv->stats.collisions += multi_col + single_col + excess_col + late_col;

		priv->stats.rx_over_errors    += rx_over;
		priv->stats.rx_crc_errors     += crc;
		priv->stats.rx_frame_errors   += code;

		priv->stats.tx_aborted_errors += tx_under;
		priv->stats.tx_carrier_errors += loss;
	}
			
} /* TLan_ReadAndClearStats */




	/***************************************************************
	 *	TLan_Reset
	 *
	 *	Returns:
	 *		0
	 *	Parms:
	 *		dev	Pointer to device structure of adapter
	 *			to be reset.
	 *
	 *	This function resets the adapter and it's physical
	 *	device.  See Chap. 3, pp. 9-10 of the "ThunderLAN
	 *	Programmer's Guide" for details.  The routine tries to
	 *	implement what is detailed there, though adjustments
	 *	have been made.
	 *
	 **************************************************************/

void
TLan_ResetAdapter( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	int		i;
	u32		addr;
	u32		data;
	u8		data8;

	priv->tlanFullDuplex = FALSE;
/*  1.	Assert reset bit. */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_AD_RST;
	outl(data, dev->base_addr + TLAN_HOST_CMD);
	
	udelay(1000);

/*  2.	Turn off interrupts. ( Probably isn't necessary ) */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_INT_OFF;
	outl(data, dev->base_addr + TLAN_HOST_CMD);

/*  3.	Clear AREGs and HASHs. */

 	for ( i = TLAN_AREG_0; i <= TLAN_HASH_2; i += 4 ) {
		TLan_DioWrite32( dev->base_addr, (u16) i, 0 );
	}

/*  4.	Setup NetConfig register. */

	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
	TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, (u16) data );

/*  5.	Load Ld_Tmr and Ld_Thr in HOST_CMD. */

 	outl( TLAN_HC_LD_TMR | 0x0, dev->base_addr + TLAN_HOST_CMD );
 	outl( TLAN_HC_LD_THR | 0x1, dev->base_addr + TLAN_HOST_CMD );

/*  6.	Unreset the MII by setting NMRST (in NetSio) to 1. */

	outw( TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR );
	addr = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;
	TLan_SetBit( TLAN_NET_SIO_NMRST, addr );

/*  7.	Setup the remaining registers. */

	if ( priv->tlanRev >= 0x30 ) {
		data8 = TLAN_ID_TX_EOC | TLAN_ID_RX_EOC;
		TLan_DioWrite8( dev->base_addr, TLAN_INT_DIS, data8 );
	}
	TLan_PhyDetect( dev );
	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN;
	if ( priv->adapter->flags & TLAN_ADAPTER_BIT_RATE_PHY ) {
		data |= TLAN_NET_CFG_BIT;
		if ( priv->aui == 1 ) {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x0a );
		} else if ( priv->duplex == TLAN_DUPLEX_FULL ) {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x00 );
			priv->tlanFullDuplex = TRUE;
		} else {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x08 );
		}
	}
	if ( priv->phyNum == 0 ) {
		data |= TLAN_NET_CFG_PHY_EN;
	}
	TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, (u16) data );

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		TLan_FinishReset( dev );
	} else {
		TLan_PhyPowerDown( dev );
	}

} /* TLan_ResetAdapter */




void
TLan_FinishReset( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u8		data;
	u32		phy;
	u8		sio;
	u16		status;
	u16		tlphy_ctl;

	phy = priv->phy[priv->phyNum];

	data = TLAN_NET_CMD_NRESET | TLAN_NET_CMD_NWRAP;
	if ( priv->tlanFullDuplex ) {
		data |= TLAN_NET_CMD_DUPLEX;
	}
	TLan_DioWrite8( dev->base_addr, TLAN_NET_CMD, data );
	data = TLAN_NET_MASK_MASK4 | TLAN_NET_MASK_MASK5; 
	if ( priv->phyNum == 0 ) {
		data |= TLAN_NET_MASK_MASK7; 
	}
	TLan_DioWrite8( dev->base_addr, TLAN_NET_MASK, data );
	TLan_DioWrite16( dev->base_addr, TLAN_MAX_RX, TLAN_MAX_FRAME_SIZE );

	if ( ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) || ( priv->aui ) ) {
		status = MII_GS_LINK;
		printk( "TLAN:  %s: Link forced.\n", dev->name );
	} else {
		TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
		udelay( 1000 );
		TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
		if ( status & MII_GS_LINK ) {
			printk( "TLAN:  %s: Link active.\n", dev->name );
			TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK );
		}
	}

	if ( priv->phyNum == 0 ) {
        	TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl );
        	tlphy_ctl |= TLAN_TC_INTEN;
        	TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl );
        	sio = TLan_DioRead8( dev->base_addr, TLAN_NET_SIO );
        	sio |= TLAN_NET_SIO_MINTEN;
        	TLan_DioWrite8( dev->base_addr, TLAN_NET_SIO, sio );
	}

	if ( status & MII_GS_LINK ) {
		TLan_SetMac( dev, 0, dev->dev_addr );
		priv->phyOnline = 1;
		outb( ( TLAN_HC_INT_ON >> 8 ), dev->base_addr + TLAN_HOST_CMD + 1 );
		if ( debug >= 1 ) {
			outb( ( TLAN_HC_REQ_INT >> 8 ), dev->base_addr + TLAN_HOST_CMD + 1 );
		}
		outl( virt_to_bus( priv->rxList ), dev->base_addr + TLAN_CH_PARM );
		outl( TLAN_HC_GO | TLAN_HC_RT, dev->base_addr + TLAN_HOST_CMD );
	} else {
		printk( "TLAN:  %s: Link inactive, will retry in 10 secs...\n", dev->name );
		TLan_SetTimer( dev, 1000, TLAN_TIMER_FINISH_RESET );
		return;
	}

} /* TLan_FinishReset */




	/***************************************************************
	 *	TLan_SetMac
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	Pointer to device structure of adapter
	 *			on which to change the AREG.
	 *		areg	The AREG to set the address in (0 - 3).
	 *		mac	A pointer to an array of chars.  Each
	 *			element stores one byte of the address.
	 *			IE, it isn't in ascii.
	 *
	 *	This function transfers a MAC address to one of the
	 *	TLAN AREGs (address registers).  The TLAN chip locks
	 *	the register on writing to offset 0 and unlocks the
	 *	register after writing to offset 5.  If NULL is passed
	 *	in mac, then the AREG is filled with 0's.
	 *
	 **************************************************************/

void TLan_SetMac( struct device *dev, int areg, char *mac )
{
	int i;
			
	areg *= 6;

	if ( mac != NULL ) {
		for ( i = 0; i < 6; i++ )
			TLan_DioWrite8( dev->base_addr, TLAN_AREG_0 + areg + i, mac[i] );
	} else {
		for ( i = 0; i < 6; i++ )
			TLan_DioWrite8( dev->base_addr, TLAN_AREG_0 + areg + i, 0 );
	}

} /* TLan_SetMac */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver PHY Layer Routines

******************************************************************************
*****************************************************************************/



	/*********************************************************************
	 *	TLan_PhyPrint
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	A pointer to the device structure of the
	 *			TLAN device having the PHYs to be detailed.
	 *				
	 *	This function prints the registers a PHY (aka tranceiver).
	 *
	 ********************************************************************/

void TLan_PhyPrint( struct device *dev )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;
	u16 i, data0, data1, data2, data3, phy;

	phy = priv->phy[priv->phyNum];

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		printk( "TLAN:   Device %s, Unmanaged PHY.\n", dev->name );
	} else if ( phy <= TLAN_PHY_MAX_ADDR ) {
		printk( "TLAN:   Device %s, PHY 0x%02x.\n", dev->name, phy );
		printk( "TLAN:      Off.  +0     +1     +2     +3 \n" );
                for ( i = 0; i < 0x20; i+= 4 ) {
			printk( "TLAN:      0x%02x", i );
			TLan_MiiReadReg( dev, phy, i, &data0 );
			printk( " 0x%04hx", data0 );
			TLan_MiiReadReg( dev, phy, i + 1, &data1 );
			printk( " 0x%04hx", data1 );
			TLan_MiiReadReg( dev, phy, i + 2, &data2 );
			printk( " 0x%04hx", data2 );
			TLan_MiiReadReg( dev, phy, i + 3, &data3 );
			printk( " 0x%04hx\n", data3 );
		}
	} else {
		printk( "TLAN:   Device %s, Invalid PHY.\n", dev->name );
	}

} /* TLan_PhyPrint */




	/*********************************************************************
	 *	TLan_PhyDetect
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	A pointer to the device structure of the adapter
	 *			for which the PHY needs determined.
	 *
	 *	So far I've found that adapters which have external PHYs
	 *	may also use the internal PHY for part of the functionality.
	 *	(eg, AUI/Thinnet).  This function finds out if this TLAN
	 *	chip has an internal PHY, and then finds the first external
	 *	PHY (starting from address 0) if it exists).
	 *
	 ********************************************************************/

void TLan_PhyDetect( struct device *dev )
{
	TLanPrivateInfo *priv = (TLanPrivateInfo *) dev->priv;
	u16		control;
	u16		hi;
	u16		lo;
	u32		phy;

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		priv->phyNum = 0xFFFF;
		return;
	}

	TLan_MiiReadReg( dev, TLAN_PHY_MAX_ADDR, MII_GEN_ID_HI, &hi );

	if ( hi != 0xFFFF ) {
		priv->phy[0] = TLAN_PHY_MAX_ADDR;
	} else {
		priv->phy[0] = TLAN_PHY_NONE;
	}

	priv->phy[1] = TLAN_PHY_NONE;
	for ( phy = 0; phy <= TLAN_PHY_MAX_ADDR; phy++ ) {
		TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &control );
		TLan_MiiReadReg( dev, phy, MII_GEN_ID_HI, &hi );
		TLan_MiiReadReg( dev, phy, MII_GEN_ID_LO, &lo );
		if ( ( control != 0xFFFF ) || ( hi != 0xFFFF ) || ( lo != 0xFFFF ) ) {
			TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN: PHY found at %02x %04x %04x %04x\n", phy, control, hi, lo );
			if ( ( priv->phy[1] == TLAN_PHY_NONE ) && ( phy != TLAN_PHY_MAX_ADDR ) ) {
				priv->phy[1] = phy;
			}
		}
	}

	if ( priv->phy[1] != TLAN_PHY_NONE ) {
		priv->phyNum = 1;
	} else if ( priv->phy[0] != TLAN_PHY_NONE ) {
		priv->phyNum = 0;
	} else {
		printk( "TLAN:  Cannot initialize device, no PHY was found!\n" );
	}

} /* TLan_PhyDetect */




void TLan_PhyPowerDown( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u16		value;

	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Powering down PHY(s).\n", dev->name );
	value = MII_GC_PDOWN | MII_GC_LOOPBK | MII_GC_ISOLATE;
	TLan_MiiSync( dev->base_addr );
	TLan_MiiWriteReg( dev, priv->phy[priv->phyNum], MII_GEN_CTL, value );
	if ( ( priv->phyNum == 0 ) && ( priv->phy[1] != TLAN_PHY_NONE ) && ( ! ( priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10 ) ) ) {
		TLan_MiiSync( dev->base_addr );
		TLan_MiiWriteReg( dev, priv->phy[1], MII_GEN_CTL, value );
	}

	/* Wait for 5 jiffies (50 ms) and powerup
	 * This is abitrary.  It is intended to make sure the
	 * tranceiver settles.
	 */
	TLan_SetTimer( dev, 5, TLAN_TIMER_PHY_PUP );

} /* TLan_PhyPowerDown */




void TLan_PhyPowerUp( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u16		value;

	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Powering up PHY.\n", dev->name );
	TLan_MiiSync( dev->base_addr );
	value = MII_GC_LOOPBK;
	TLan_MiiWriteReg( dev, priv->phy[priv->phyNum], MII_GEN_CTL, value );

	/* Wait for 50 jiffies (500 ms) and reset the
	 * tranceiver.  The TLAN docs say both 50 ms and
	 * 500 ms, so do the longer, just in case
	 */
	TLan_SetTimer( dev, 50, TLAN_TIMER_PHY_RESET );

} /* TLan_PhyPowerUp */




void TLan_PhyReset( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u16		phy;
	u16		value;

	phy = priv->phy[priv->phyNum];

	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Reseting PHY.\n", dev->name );
	TLan_MiiSync( dev->base_addr );
	value = MII_GC_LOOPBK | MII_GC_RESET;
	TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, value );
	TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &value );
	while ( value & MII_GC_RESET ) {
		TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &value );
	}
	TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0 );

	/* Wait for 50 jiffies (500 ms) and initialize.
	 * I don't remember why I wait this long.
	 */
	TLan_SetTimer( dev, 50, TLAN_TIMER_PHY_START_LINK );

} /* TLan_PhyReset */




void TLan_PhyStartLink( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u16		ability;
	u16		control;
	u16		data;
	u16		phy;
	u16		status;
	u16		tctl;

	phy = priv->phy[priv->phyNum];

	TLAN_DBG( TLAN_DEBUG_GNRL, "TLAN:  %s: Trying to activate link.\n", dev->name );
	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
	if ( ( status & MII_GS_AUTONEG ) && 
	     ( priv->duplex == TLAN_DUPLEX_DEFAULT ) && 
	     ( priv->speed == TLAN_SPEED_DEFAULT ) &&
	     ( ! priv->aui ) ) {
		ability = status >> 11;

		if ( priv->speed == TLAN_SPEED_10 ) {
			ability &= 0x0003;
		} else if ( priv->speed == TLAN_SPEED_100 ) {
			ability &= 0x001C;
		}

		if ( priv->duplex == TLAN_DUPLEX_FULL ) {
			ability &= 0x000A;
		} else if ( priv->duplex == TLAN_DUPLEX_HALF ) {
			ability &= 0x0005;
		}

		TLan_MiiWriteReg( dev, phy, MII_AN_ADV, ( ability << 5 ) | 1 );
       		TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x1000 );
		TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x1200 );

		/* Wait for 400 jiffies (4 sec) for autonegotiation
		 * to complete.  The max spec time is less than this
		 * but the card need additional time to start AN.
		 * .5 sec should be plenty extra.
		 */
		printk( "TLAN:  %s: Starting autonegotiation.\n", dev->name );
		TLan_SetTimer( dev, 400, TLAN_TIMER_PHY_FINISH_AN );
		return;
	}

	if ( ( priv->aui ) && ( priv->phyNum != 0 ) ) {
		priv->phyNum = 0;
		data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
		TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, data );
		TLan_SetTimer( dev, 4, TLAN_TIMER_PHY_PDOWN );
		return;
	} else if ( priv->phyNum == 0 ) {
        	TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tctl );
		if ( priv->aui ) {
                	tctl |= TLAN_TC_AUISEL;
		} else {
                	tctl &= ~TLAN_TC_AUISEL;
			control = 0;
			if ( priv->duplex == TLAN_DUPLEX_FULL ) {
				control |= MII_GC_DUPLEX;
				priv->tlanFullDuplex = TRUE;
			}
			if ( priv->speed == TLAN_SPEED_100 ) {
				control |= MII_GC_SPEEDSEL;
			}
       			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, control );
		}
        	TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tctl );
	}

	/* Wait for 100 jiffies (1 sec) to give the tranceiver time
	 * to establish link.
	 */
	TLan_SetTimer( dev, 100, TLAN_TIMER_FINISH_RESET );

} /* TLan_PhyStartLink */




void TLan_PhyFinishAutoNeg( struct device *dev )
{
	TLanPrivateInfo	*priv = (TLanPrivateInfo *) dev->priv;
	u16		an_adv;
	u16		an_lpa;
	u16		data;
	u16		mode;
	u16		phy;
	u16		status;
	
	phy = priv->phy[priv->phyNum];

	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
	if ( ! ( status & MII_GS_AUTOCMPLT ) ) {
		/* Wait for 800 jiffies (8 sec) to give the process
		 * more time.  Perhaps we should fail after a while.
		 */
		printk( "TLAN:  Giving autonegotiation more time.\n" );
		TLan_SetTimer( dev, 800, TLAN_TIMER_PHY_FINISH_AN );
		return;
	}

	printk( "TLAN:  %s: Autonegotiation complete.\n", dev->name );
	TLan_MiiReadReg( dev, phy, MII_AN_ADV, &an_adv );
	TLan_MiiReadReg( dev, phy, MII_AN_LPA, &an_lpa );
	mode = an_adv & an_lpa & 0x03E0;
	if ( mode & 0x0100 ) {
		priv->tlanFullDuplex = TRUE;
	} else if ( ! ( mode & 0x0080 ) && ( mode & 0x0040 ) ) {
		priv->tlanFullDuplex = TRUE;
	}

	if ( ( ! ( mode & 0x0180 ) ) && ( priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10 ) && ( priv->phyNum != 0 ) ) {
		priv->phyNum = 0;
		data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
		TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, data );
		TLan_SetTimer( dev, 40, TLAN_TIMER_PHY_PDOWN );
		return;
	}

	if ( priv->phyNum == 0 ) {
		if ( ( priv->duplex == TLAN_DUPLEX_FULL ) || ( an_adv & an_lpa & 0x0040 ) ) {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, MII_GC_AUTOENB | MII_GC_DUPLEX );
			printk( "TLAN:  Starting internal PHY with DUPLEX\n" );
		} else {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, MII_GC_AUTOENB );
			printk( "TLAN:  Starting internal PHY with HALF-DUPLEX\n" );
		}
	}

	/* Wait for 10 jiffies (100 ms).  No reason in partiticular.
	 */
	TLan_SetTimer( dev, 10, TLAN_TIMER_FINISH_RESET );
		
} /* TLan_PhyFinishAutoNeg */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver MII Routines

	These routines are based on the information in Chap. 2 of the
	"ThunderLAN Programmer's Guide", pp. 15-24.

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_MiiReadReg
	 *
	 *	Returns:
	 *		0	if ack received ok
	 *		1	otherwise.
	 *
	 *	Parms:
	 *		dev		The device structure containing
	 *				The io address and interrupt count
	 *				for this device.
	 *		phy		The address of the PHY to be queried.
	 *		reg		The register whose contents are to be
	 *				retreived.
	 *		val		A pointer to a variable to store the
	 *				retrieved value.
	 *
	 *	This function uses the TLAN's MII bus to retreive the contents
	 *	of a given register on a PHY.  It sends the appropriate info
	 *	and then reads the 16-bit register value from the MII bus via
	 *	the TLAN SIO register.
	 *
	 **************************************************************/

int TLan_MiiReadReg( struct device *dev, u16 phy, u16 reg, u16 *val )
{
	u8	nack;
	u16	sio, tmp;
	u32	i;
	int	err;
	int	minten;

	err = FALSE;
	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if ( dev->interrupt == 0 )
		cli();
	dev->interrupt++;

	TLan_MiiSync(dev->base_addr);

	minten = TLan_GetBit( TLAN_NET_SIO_MINTEN, sio );
	if ( minten )
		TLan_ClearBit(TLAN_NET_SIO_MINTEN, sio);

	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Start ( 01b ) */
	TLan_MiiSendData( dev->base_addr, 0x2, 2 );	/* Read  ( 10b ) */
	TLan_MiiSendData( dev->base_addr, phy, 5 );	/* Device #      */
	TLan_MiiSendData( dev->base_addr, reg, 5 );	/* Register #    */


	TLan_ClearBit(TLAN_NET_SIO_MTXEN, sio);		/* Change direction */

	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Clock Idle bit */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Wait 300ns */

	nack = TLan_GetBit(TLAN_NET_SIO_MDATA, sio);	/* Check for ACK */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);		/* Finish ACK */
	if (nack) {					/* No ACK, so fake it */
		for (i = 0; i < 16; i++) {
			TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);
			TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
		}
		tmp = 0xffff;
		err = TRUE;
	} else {					/* ACK, so read data */
		for (tmp = 0, i = 0x8000; i; i >>= 1) {
			TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);
			if (TLan_GetBit(TLAN_NET_SIO_MDATA, sio))
				tmp |= i;
			TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
		}
	}


	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Idle cycle */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);

	if ( minten )
		TLan_SetBit(TLAN_NET_SIO_MINTEN, sio);

	*val = tmp;

	dev->interrupt--;
	if ( dev->interrupt == 0 )
		sti();

	return err;

} /* TLan_MiiReadReg */




	/***************************************************************
	 *	TLan_MiiSendData
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		base_port	The base IO port of the adapter	in
	 *				question.
	 *		dev		The address of the PHY to be queried.
	 *		data		The value to be placed on the MII bus.
	 *		num_bits	The number of bits in data that are to
	 *				be placed on the MII bus.
	 *
	 *	This function sends on sequence of bits on the MII
	 *	configuration bus.
	 *
	 **************************************************************/

void TLan_MiiSendData( u16 base_port, u32 data, unsigned num_bits )
{
	u16 sio;
	u32 i;

	if ( num_bits == 0 )
		return;

	outw( TLAN_NET_SIO, base_port + TLAN_DIO_ADR );
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;
	TLan_SetBit( TLAN_NET_SIO_MTXEN, sio );

	for ( i = ( 0x1 << ( num_bits - 1 ) ); i; i >>= 1 ) {
		TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );
		TLan_GetBit( TLAN_NET_SIO_MCLK, sio );
		if ( data & i )
			TLan_SetBit( TLAN_NET_SIO_MDATA, sio );
		else
			TLan_ClearBit( TLAN_NET_SIO_MDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_MCLK, sio );
		TLan_GetBit( TLAN_NET_SIO_MCLK, sio );
	}

} /* TLan_MiiSendData */




	/***************************************************************
	 *	TLan_MiiSync
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		base_port	The base IO port of the adapter in
	 *				question.
	 *
	 *	This functions syncs all PHYs in terms of the MII configuration
	 *	bus.
	 *
	 **************************************************************/

void TLan_MiiSync( u16 base_port )
{
	int i;
	u16 sio;

	outw( TLAN_NET_SIO, base_port + TLAN_DIO_ADR );
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;

	TLan_ClearBit( TLAN_NET_SIO_MTXEN, sio );
	for ( i = 0; i < 32; i++ ) {
		TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );
		TLan_SetBit( TLAN_NET_SIO_MCLK, sio );
	}

} /* TLan_MiiSync */




	/***************************************************************
	 *	TLan_MiiWriteReg
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev		The device structure for the device
	 *				to write to.
	 *		phy		The address of the PHY to be written to.
	 *		reg		The register whose contents are to be
	 *				written.
	 *		val		The value to be written to the register.
	 *
	 *	This function uses the TLAN's MII bus to write the contents of a
	 *	given register on a PHY.  It sends the appropriate info and then
	 *	writes the 16-bit register value from the MII configuration bus
	 *	via the TLAN SIO register.
	 *
	 **************************************************************/

void TLan_MiiWriteReg( struct device *dev, u16 phy, u16 reg, u16 val )
{
	u16	sio;
	int	minten;

	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if ( dev->interrupt == 0 )
		cli();
	dev->interrupt++;

	TLan_MiiSync( dev->base_addr );

	minten = TLan_GetBit( TLAN_NET_SIO_MINTEN, sio );
	if ( minten )
		TLan_ClearBit( TLAN_NET_SIO_MINTEN, sio );

	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Start ( 01b ) */
	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Write ( 01b ) */
	TLan_MiiSendData( dev->base_addr, phy, 5 );	/* Device #      */
	TLan_MiiSendData( dev->base_addr, reg, 5 );	/* Register #    */

	TLan_MiiSendData( dev->base_addr, 0x2, 2 );	/* Send ACK */
	TLan_MiiSendData( dev->base_addr, val, 16 );	/* Send Data */

	TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );	/* Idle cycle */
	TLan_SetBit( TLAN_NET_SIO_MCLK, sio );

	if ( minten )
		TLan_SetBit( TLAN_NET_SIO_MINTEN, sio );

	dev->interrupt--;
	if ( dev->interrupt == 0 )
		sti();

} /* TLan_MiiWriteReg */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Eeprom routines

	The Compaq Netelligent 10 and 10/100 cards use a Microchip 24C02A
	EEPROM.  These functions are based on information in Microchip's
	data sheet.  I don't know how well this functions will work with
	other EEPROMs.

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_EeSendStart
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:	
	 *		io_base		The IO port base address for the
	 *				TLAN device with the EEPROM to
	 *				use.
	 *
	 *	This function sends a start cycle to an EEPROM attached
	 *	to a TLAN chip.
	 *
	 **************************************************************/

void TLan_EeSendStart( u16 io_base )
{
	u16	sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
	TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );
	TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
	TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );

} /* TLan_EeSendStart */




	/***************************************************************
	 *	TLan_EeSendByte
	 *
	 *	Returns:
	 *		If the correct ack was received, 0, otherwise 1
	 *	Parms:	io_base		The IO port base address for the
	 *				TLAN device with the EEPROM to
	 *				use.
	 *		data		The 8 bits of information to
	 *				send to the EEPROM.
	 *		stop		If TLAN_EEPROM_STOP is passed, a
	 *				stop cycle is sent after the
	 *				byte is sent after the ack is
	 *				read.
	 *
	 *	This function sends a byte on the serial EEPROM line,
	 *	driving the clock to send each bit. The function then
	 *	reverses transmission direction and reads an acknowledge
	 *	bit.
	 *
	 **************************************************************/

int TLan_EeSendByte( u16 io_base, u8 data, int stop )
{
	int	err;
	u8	place;
	u16	sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	/* Assume clock is low, tx is enabled; */
	for ( place = 0x80; place != 0; place >>= 1 ) {
		if ( place & data )
			TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
		else
			TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	}
	TLan_ClearBit( TLAN_NET_SIO_ETXEN, sio );
	TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
	err = TLan_GetBit( TLAN_NET_SIO_EDATA, sio );
	TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );

	if ( ( ! err ) && stop ) {
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );	/* STOP, raise data while clock is high */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	}

	return ( err );

} /* TLan_EeSendByte */




	/***************************************************************
	 *	TLan_EeReceiveByte
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		io_base		The IO port base address for the
	 *				TLAN device with the EEPROM to
	 *				use.
	 *		data		An address to a char to hold the
	 *				data sent from the EEPROM.
	 *		stop		If TLAN_EEPROM_STOP is passed, a
	 *				stop cycle is sent after the
	 *				byte is received, and no ack is
	 *				sent.
	 *
	 *	This function receives 8 bits of data from the EEPROM
	 *	over the serial link.  It then sends and ack bit, or no
	 *	ack and a stop bit.  This function is used to retrieve
	 *	data after the address of a byte in the EEPROM has been
	 *	sent.
	 *
	 **************************************************************/

void TLan_EeReceiveByte( u16 io_base, u8 *data, int stop )
{
	u8  place;
	u16 sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;
	*data = 0;

	/* Assume clock is low, tx is enabled; */
	TLan_ClearBit( TLAN_NET_SIO_ETXEN, sio );
	for ( place = 0x80; place; place >>= 1 ) {
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		if ( TLan_GetBit( TLAN_NET_SIO_EDATA, sio ) )
			*data |= place;
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	}

	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );
	if ( ! stop ) {
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );	/* Ack = 0 */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	} else {
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );		/* No ack = 1 (?) */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );	/* STOP, raise data while clock is high */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	}

} /* TLan_EeReceiveByte */




	/***************************************************************
	 *	TLan_EeReadByte
	 *
	 *	Returns:
	 *		No error = 0, else, the stage at which the error
	 *		occurred.
	 *	Parms:
	 *		io_base		The IO port base address for the
	 *				TLAN device with the EEPROM to
	 *				use.
	 *		ee_addr		The address of the byte in the
	 *				EEPROM whose contents are to be
	 *				retrieved.
	 *		data		An address to a char to hold the
	 *				data obtained from the EEPROM.
	 *
	 *	This function reads a byte of information from an byte
	 *	cell in the EEPROM.
	 *
	 **************************************************************/

int TLan_EeReadByte( struct device *dev, u8 ee_addr, u8 *data )
{
	int err;

	if ( dev->interrupt == 0 )
		cli();
	dev->interrupt++;

	TLan_EeSendStart( dev->base_addr );
	err = TLan_EeSendByte( dev->base_addr, 0xA0, TLAN_EEPROM_ACK );
	if (err)
		return 1;
	err = TLan_EeSendByte( dev->base_addr, ee_addr, TLAN_EEPROM_ACK );
	if (err)
		return 2;
	TLan_EeSendStart( dev->base_addr );
	err = TLan_EeSendByte( dev->base_addr, 0xA1, TLAN_EEPROM_ACK );
	if (err)
		return 3;
	TLan_EeReceiveByte( dev->base_addr, data, TLAN_EEPROM_STOP );

	dev->interrupt--;
	if ( dev->interrupt == 0 )
		sti();

	return 0;

} /* TLan_EeReadByte */





