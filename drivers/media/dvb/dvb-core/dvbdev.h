/* 
 * dvbdev.h
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
                      for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBDEV_H_
#define _DVBDEV_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/list.h>

#define DVB_MAJOR 250

#define DVB_DEVICE_VIDEO      0
#define DVB_DEVICE_AUDIO      1
#define DVB_DEVICE_SEC        2
#define DVB_DEVICE_FRONTEND   3
#define DVB_DEVICE_DEMUX      4
#define DVB_DEVICE_DVR        5
#define DVB_DEVICE_CA         6
#define DVB_DEVICE_NET        7
#define DVB_DEVICE_OSD        8


typedef struct dvb_adapter_s
{
	int num;
	devfs_handle_t devfs_handle;
	struct list_head list_head;
	struct list_head device_list;
} dvb_adapter_t;


typedef struct dvb_device
{
	struct list_head list_head;
	struct file_operations *fops;
	devfs_handle_t devfs_handle;
	dvb_adapter_t *adapter;
	int type;
	u32 id;

	int users;
	int writers;

	/* don't really need those !? */
	int (*kernel_ioctl)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg);  // FIXME: use generic_usercopy()

	void *priv;
} dvb_device_t;


int dvb_register_device(dvb_adapter_t *adap, dvb_device_t **pdvbdev, 
			dvb_device_t *template, void *priv, int type);
void dvb_unregister_device(struct dvb_device *dvbdev);

int dvb_register_adapter(dvb_adapter_t **padap, char *name);
int dvb_unregister_adapter(dvb_adapter_t *adap);

int dvb_generic_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);
int dvb_generic_open(struct inode *inode, struct file *file);
int dvb_generic_release(struct inode *inode, struct file *file);
int generic_usercopy(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg,
		     int (*func)(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg));

#endif /* #ifndef __DVBDEV_H */
