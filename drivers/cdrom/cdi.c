/* -- cdi.c
 *
 *    Initialisation of software configurable cdrom interface 
 *    cards goes here.
 *
 *    Copyright (c) 1996 Eric van der Maarel <H.T.M.v.d.Maarel@marin.nl>
 *
 *    Version 0.1
 *
 *    History:
 *    0.1 First release. Only support for ISP16/MAD16/Mozart.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/blk.h>  /* where the proto type of cdi_init() is */
#ifdef CONFIG_ISP16_CDI
#include <linux/isp16.h>
#endif CONFIG_ISP16_CDI

/*
 *  Cdrom interface configuration.
 */
int
cdi_init(void)
{
  int ret_val = -1;

#ifdef CONFIG_ISP16_CDI
  ret_val &= isp16_init();
#endif CONFIG_ISP16_CDI

  return(ret_val);
}

