/* This file is derived from pegasus.c.
 *  Copyright (c) 1999,2000 Petko Manolov - Petkan (petkan@dce.bg)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
 

/* This file should be included after defining the macro PEGASUS_DEV. */

PEGASUS_DEV( "Billionton USB-100", 0x08dd, 0x0986,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "Billionton USBLP-100", 0x08dd, 0x0987,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )

PEGASUS_DEV( "Billionton USBEL-100", 0x08dd, 0x0988,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "Billionton USBE-100", 0x08dd, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )

PEGASUS_DEV( "Corega FEter USB-TX", 0x7aa, 0x0004,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "MELCO/BUFFALO LUA-TX", 0x0411, 0x0001,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "D-Link DSB-650TX", 0x2001, 0x4001,
		LINKSYS_GPIO_RESET )

PEGASUS_DEV( "D-Link DSB-650TX", 0x2001, 0x4002,
		LINKSYS_GPIO_RESET )

PEGASUS_DEV( "D-Link DSB-650TX(PNA)", 0x2001, 0x4003,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )

PEGASUS_DEV( "D-Link DSB-650", 0x2001, 0xabc1,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "D-Link DU-E10", 0x07b8, 0xabc1,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "D-Link DU-E100", 0x07b8, 0x4002,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "Linksys USB10TX", 0x066b, 0x2202,
		LINKSYS_GPIO_RESET )

PEGASUS_DEV( "Linksys USB100TX", 0x066b, 0x2203,
		LINKSYS_GPIO_RESET )

PEGASUS_DEV( "Linksys USB100TX", 0x066b, 0x2204,
		LINKSYS_GPIO_RESET | HAS_HOME_PNA )

PEGASUS_DEV( "Linksys USB Ethernet Adapter", 0x066b, 0x2206,
		LINKSYS_GPIO_RESET )

PEGASUS_DEV( "SMC 202 USB Ethernet", 0x0707, 0x0200,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "ADMtek AN986 \"Pegasus\" USB Ethernet (eval board)", 0x07a6, 0x0986,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )

PEGASUS_DEV( "Accton USB 10/100 Ethernet Adapter", 0x083a, 0x1046,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "IO DATA USB ET/TX", 0x04bb, 0x0904,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "LANEED USB Ethernet LD-USB/TX", 0x056e, 0x4002,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "SOHOware NUB100 Ethernet", 0x15e8, 0x9100,
		DEFAULT_GPIO_RESET )

PEGASUS_DEV( "ADMtek ADM8511 \"Pegasus II\" USB Ethernet", 0x07a6, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
