/*
    i2c-dev.c - i2c-bus driver, char device interface  

    Copyright (C) 1995-97 Simon G. Vogl
    Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Note that this is a complete rewrite of Simon Vogl's i2c-dev module.
   But I have used so much of his original code and ideas that it seems
   only fair to recognize him as co-author -- Frodo */

/* $Id: i2c-dev.c,v 1.18 1999/12/21 23:45:58 frodo Exp $ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/version.h>

/* If you want debugging uncomment: */
/* #define DEBUG */

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) | ((b) << 8) | (c))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,51)
#include <linux/init.h>
#else
#define __init
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,4))
#define copy_from_user memcpy_fromfs
#define copy_to_user memcpy_tofs
#define get_user_data(to,from) ((to) = get_user(from),0)
#else
#include <asm/uaccess.h>
#define get_user_data(to,from) get_user(to,from)
#endif

/* 2.0.0 kernel compatibility */
#if LINUX_VERSION_CODE < 0x020100
#define MODULE_AUTHOR(noone)
#define MODULE_DESCRIPTION(none)
#define MODULE_PARM(no,param)
#define MODULE_PARM_DESC(no,description)
#define EXPORT_SYMBOL(noexport)
#define EXPORT_NO_SYMBOLS
#endif

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#ifdef MODULE
extern int init_module(void);
extern int cleanup_module(void);
#endif /* def MODULE */

/* struct file_operations changed too often in the 2.1 series for nice code */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
static loff_t i2cdev_lseek (struct file *file, loff_t offset, int origin);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,56))
static long long i2cdev_lseek (struct file *file, long long offset, int origin);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0))
static long long i2cdev_llseek (struct inode *inode, struct file *file, 
                                long long offset, int origin);
#else
static int i2cdev_lseek (struct inode *inode, struct file *file, off_t offset, 
                         int origin);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
static ssize_t i2cdev_read (struct file *file, char *buf, size_t count, 
                            loff_t *offset);
static ssize_t i2cdev_write (struct file *file, const char *buf, size_t count, 
                             loff_t *offset);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0))
static long i2cdev_read (struct inode *inode, struct file *file, char *buf, 
                         unsigned long count);
static long i2cdev_write (struct inode *inode, struct file *file, 
                          const char *buf, unsigned long offset);
#else
static int i2cdev_read(struct inode *inode, struct file *file, char *buf, 
                       int count);
static int i2cdev_write(struct inode *inode, struct file *file, 
                        const char *buf, int count);
#endif

static int i2cdev_ioctl (struct inode *inode, struct file *file, 
                         unsigned int cmd, unsigned long arg);
static int i2cdev_open (struct inode *inode, struct file *file);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,31))
static int i2cdev_release (struct inode *inode, struct file *file);
#else
static void i2cdev_release (struct inode *inode, struct file *file);
#endif


static int i2cdev_attach_adapter(struct i2c_adapter *adap);
static int i2cdev_detach_client(struct i2c_client *client);
static int i2cdev_command(struct i2c_client *client, unsigned int cmd,
                           void *arg);

#ifdef MODULE
static
#else
extern
#endif
       int __init i2c_dev_init(void);
static int i2cdev_cleanup(void);

static struct file_operations i2cdev_fops = {
    i2cdev_lseek,
    i2cdev_read,
    i2cdev_write,
    NULL,                   /* i2cdev_readdir  */
    NULL,                   /* i2cdev_select   */
    i2cdev_ioctl,
    NULL,                   /* i2cdev_mmap     */
    i2cdev_open,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,118)
    NULL,                   /* i2cdev_flush    */
#endif
    i2cdev_release,
};

#define I2CDEV_ADAPS_MAX I2C_ADAP_MAX
static struct i2c_adapter *i2cdev_adaps[I2CDEV_ADAPS_MAX];

static struct i2c_driver i2cdev_driver = {
  /* name */            "i2c-dev dummy driver",
  /* id */              I2C_DRIVERID_I2CDEV,
  /* flags */           I2C_DF_DUMMY,
  /* attach_adapter */  i2cdev_attach_adapter,
  /* detach_client */   i2cdev_detach_client,
  /* command */         i2cdev_command,
  /* inc_use */         NULL,
  /* dec_use */         NULL,
};

static struct i2c_client i2cdev_client_template = {
  /* name */          "I2C /dev entry",
  /* id */            1,
  /* flags */         0,
  /* addr */          -1,
  /* adapter */       NULL,
  /* driver */        &i2cdev_driver,
  /* data */          NULL
};

static int i2cdev_initialized;

/* Note that the lseek function is called llseek in 2.1 kernels. But things
   are complicated enough as is. */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
loff_t i2cdev_lseek (struct file *file, loff_t offset, int origin)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,56))
long long i2cdev_lseek (struct file *file, long long offset, int origin)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0))
long long i2cdev_llseek (struct inode *inode, struct file *file, 
                         long long offset, int origin)
#else
int i2cdev_lseek (struct inode *inode, struct file *file, off_t offset, 
                  int origin)
#endif
{
#ifdef DEBUG
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,56))
   struct inode *inode = file->f_dentry->d_inode;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70)) */
  printk("i2c-dev,o: i2c-%d lseek to %ld bytes relative to %d.\n",
         MINOR(inode->i_rdev),(long) offset,origin);
#endif /* DEBUG */
  return -ESPIPE;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
static ssize_t i2cdev_read (struct file *file, char *buf, size_t count,
                            loff_t *offset)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0))
static long i2cdev_read (struct inode *inode, struct file *file, char *buf,
                         unsigned long count)
#else
static int i2cdev_read(struct inode *inode, struct file *file, char *buf,
                       int count)
#endif
{
  char *tmp;
  int ret;

#ifdef DEBUG
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
   struct inode *inode = file->f_dentry->d_inode;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70)) */
#endif /* DEBUG */

  struct i2c_client *client = (struct i2c_client *)file->private_data;

  /* copy user space data to kernel space. */
  tmp = kmalloc(count,GFP_KERNEL);
  if (tmp==NULL)
     return -ENOMEM;

#ifdef DEBUG
  printk("i2c-dev,o: i2c-%d reading %d bytes.\n",MINOR(inode->i_rdev),count);
#endif

  ret = i2c_master_recv(client,tmp,count);
  copy_to_user(buf,tmp,count);
  kfree(tmp);
  return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
static ssize_t i2cdev_write (struct file *file, const char *buf, size_t count,
                             loff_t *offset)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0))
static long i2cdev_write (struct inode *inode, struct file *file,
                          const char *buf, unsigned long offset)
#else
static int i2cdev_write(struct inode *inode, struct file *file,
                        const char *buf, int count)
#endif
{
  int ret;
  char *tmp;
  struct i2c_client *client = (struct i2c_client *)file->private_data;

#ifdef DEBUG
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70))
   struct inode *inode = file->f_dentry->d_inode;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,70)) */
#endif /* DEBUG */

  /* copy user space data to kernel space. */
  tmp = kmalloc(count,GFP_KERNEL);
  if (tmp==NULL)
    return -ENOMEM;
  copy_from_user(tmp,buf,count);

#ifdef DEBUG
  printk("i2c-dev,o: i2c-%d writing %d bytes.\n",MINOR(inode->i_rdev),count);
#endif
  ret = i2c_master_send(client,tmp,count);
  kfree(tmp);
  return ret;
}

int i2cdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, 
                  unsigned long arg)
{
  struct i2c_client *client = (struct i2c_client *)file->private_data;
  struct i2c_smbus_ioctl_data data_arg;
  union i2c_smbus_data temp;
  int ver,datasize,res;
  unsigned long funcs;

#ifdef DEBUG
  printk("i2c-dev.o: i2c-%d ioctl, cmd: 0x%x, arg: %lx.\n", 
         MINOR(inode->i_rdev),cmd, arg);
#endif /* DEBUG */

  switch ( cmd ) {
    case I2C_SLAVE:
    case I2C_SLAVE_FORCE:
      if ((arg > 0x3ff) || (((client->flags & I2C_M_TEN) == 0) && arg > 0x7f))
        return -EINVAL;
      if ((cmd == I2C_SLAVE) && i2c_check_addr(client->adapter,arg))
        return -EBUSY;
      client->addr = arg;
      return 0;
    case I2C_TENBIT:
      if (arg)
        client->flags |= I2C_M_TEN;
      else
        client->flags &= ~I2C_M_TEN;
      return 0;
    case I2C_FUNCS:
      if (! arg) {
#ifdef DEBUG
        printk("i2c-dev.o: NULL argument pointer in ioctl I2C_SMBUS.\n");
#endif
        return -EINVAL;
      }
      if (verify_area(VERIFY_WRITE,(unsigned long *) arg,
          sizeof(unsigned long))) {
#ifdef DEBUG
        printk("i2c-dev.o: invalid argument pointer (%ld) "
               "in IOCTL I2C_SMBUS.\n", arg);
#endif
        return -EINVAL;
      }
      
      funcs = i2c_get_functionality(client->adapter);
      copy_to_user((unsigned long *)arg,&funcs,sizeof(unsigned long));
      return 0;
    case I2C_SMBUS:
      if (! arg) {
#ifdef DEBUG
        printk("i2c-dev.o: NULL argument pointer in ioctl I2C_SMBUS.\n");
#endif
        return -EINVAL;
      }
      if (verify_area(VERIFY_READ,(struct i2c_smbus_ioctl_data *) arg,
          sizeof(struct i2c_smbus_ioctl_data))) {
#ifdef DEBUG
        printk("i2c-dev.o: invalid argument pointer (%ld) "
               "in IOCTL I2C_SMBUS.\n", arg);
#endif
        return -EINVAL;
      }
      copy_from_user(&data_arg,(struct i2c_smbus_ioctl_data *) arg,
                     sizeof(struct i2c_smbus_ioctl_data));
      if ((data_arg.size != I2C_SMBUS_BYTE) && 
          (data_arg.size != I2C_SMBUS_QUICK) &&
          (data_arg.size != I2C_SMBUS_BYTE_DATA) && 
          (data_arg.size != I2C_SMBUS_WORD_DATA) &&
          (data_arg.size != I2C_SMBUS_PROC_CALL) &&
          (data_arg.size != I2C_SMBUS_BLOCK_DATA)) {
#ifdef DEBUG
        printk("i2c-dev.o: size out of range (%x) in ioctl I2C_SMBUS.\n",
               data_arg.size);
#endif
        return -EINVAL;
      }
      /* Note that I2C_SMBUS_READ and I2C_SMBUS_WRITE are 0 and 1, 
         so the check is valid if size==I2C_SMBUS_QUICK too. */
      if ((data_arg.read_write != I2C_SMBUS_READ) && 
          (data_arg.read_write != I2C_SMBUS_WRITE)) {
#ifdef DEBUG
        printk("i2c-dev.o: read_write out of range (%x) in ioctl I2C_SMBUS.\n",
               data_arg.read_write);
#endif
        return -EINVAL;
      }

      /* Note that command values are always valid! */

      if ((data_arg.size == I2C_SMBUS_QUICK) ||
          ((data_arg.size == I2C_SMBUS_BYTE) && 
           (data_arg.read_write == I2C_SMBUS_WRITE)))
        /* These are special: we do not use data */
        return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
                              data_arg.read_write, data_arg.command,
                              data_arg.size, NULL);

      if (data_arg.data == NULL) {
#ifdef DEBUG
        printk("i2c-dev.o: data is NULL pointer in ioctl I2C_SMBUS.\n");
#endif
        return -EINVAL;
      }

      /* This seems unlogical but it is not: if the user wants to read a
         value, we must write that value to user memory! */
      ver = ((data_arg.read_write == I2C_SMBUS_WRITE) && 
             (data_arg.size != I2C_SMBUS_PROC_CALL))?VERIFY_READ:VERIFY_WRITE;

      if ((data_arg.size == I2C_SMBUS_BYTE_DATA) || (data_arg.size == I2C_SMBUS_BYTE))
        datasize = sizeof(data_arg.data->byte);
      else if ((data_arg.size == I2C_SMBUS_WORD_DATA) || 
               (data_arg.size == I2C_SMBUS_PROC_CALL))
        datasize = sizeof(data_arg.data->word);
      else /* size == I2C_SMBUS_BLOCK_DATA */
        datasize = sizeof(data_arg.data->block);

      if (verify_area(ver,data_arg.data,datasize)) {
#ifdef DEBUG
        printk("i2c-dev.o: invalid pointer data (%p) in ioctl I2C_SMBUS.\n",
               data_arg.data);
#endif
        return -EINVAL;
      }

      if ((data_arg.size == I2C_SMBUS_PROC_CALL) || 
          (data_arg.read_write == I2C_SMBUS_WRITE))
        copy_from_user(&temp,data_arg.data,datasize);
      res = i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           data_arg.read_write,
                           data_arg.command,data_arg.size,&temp);
      if (! res && ((data_arg.size == I2C_SMBUS_PROC_CALL) || 
                    (data_arg.read_write == I2C_SMBUS_READ)))
        copy_to_user(data_arg.data,&temp,datasize);
      return res;

    default:
      return i2c_control(client,cmd,arg);
   }
  return 0;
}

int i2cdev_open (struct inode *inode, struct file *file)
{
  unsigned int minor = MINOR(inode->i_rdev);
  struct i2c_client *client;

  if ((minor >= I2CDEV_ADAPS_MAX) || ! (i2cdev_adaps[minor])) {
#ifdef DEBUG
    printk("i2c-dev.o: Trying to open unattached adapter i2c-%d\n",minor);
#endif
    return -ENODEV;
  }

  /* Note that we here allocate a client for later use, but we will *not*
     register this client! Yes, this is safe. No, it is not very clean. */
  if(! (client = kmalloc(sizeof(struct i2c_client),GFP_KERNEL)))
    return -ENOMEM;
  memcpy(client,&i2cdev_client_template,sizeof(struct i2c_client));
  client->adapter = i2cdev_adaps[minor];
  file->private_data = client;

  i2cdev_adaps[minor]->inc_use(i2cdev_adaps[minor]);
  MOD_INC_USE_COUNT;

#ifdef DEBUG
  printk("i2c-dev.o: opened i2c-%d\n",minor);
#endif
  return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,31))
static int i2cdev_release (struct inode *inode, struct file *file)
#else
static void i2cdev_release (struct inode *inode, struct file *file)
#endif
{
  unsigned int minor = MINOR(inode->i_rdev);
   kfree(file->private_data);
   file->private_data=NULL;
#ifdef DEBUG
   printk("i2c-dev.o: Closed: i2c-%d\n", minor);
#endif
  MOD_DEC_USE_COUNT;
  i2cdev_adaps[minor]->dec_use(i2cdev_adaps[minor]);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,31))
   return 0;
#endif
}

int i2cdev_attach_adapter(struct i2c_adapter *adap)
{
  int i;

  if ((i = i2c_adapter_id(adap)) < 0) {
    printk("i2c-dev.o: Unknown adapter ?!?\n");
    return -ENODEV;
  }
  if (i >= I2CDEV_ADAPS_MAX) {
    printk("i2c-dev.o: Adapter number too large?!? (%d)\n",i);
    return -ENODEV;
  }
  
  if (! i2cdev_adaps[i]) {
    i2cdev_adaps[i] = adap;
    printk("i2c-dev.o: Registered '%s' as minor %d\n",adap->name,i);
  } else {
    i2cdev_adaps[i] = NULL;
#ifdef DEBUG
    printk("i2c-dev.o: Adapter unregistered: %s\n",adap->name);
#endif
  }

  return 0;
}

int i2cdev_detach_client(struct i2c_client *client)
{
  return 0;
}

static int i2cdev_command(struct i2c_client *client, unsigned int cmd,
                           void *arg)
{
  return -1;
}

int __init i2c_dev_init(void)
{
  int res;

  printk("i2c-dev.o: i2c /dev entries driver module\n");

  i2cdev_initialized = 0;
  if (register_chrdev(I2C_MAJOR,"i2c",&i2cdev_fops)) {
    printk("i2c-dev.o: unable to get major %d for i2c bus\n",I2C_MAJOR);
    return -EIO;
  }
  i2cdev_initialized ++;

  if ((res = i2c_add_driver(&i2cdev_driver))) {
    printk("i2c-dev.o: Driver registration failed, module not inserted.\n");
    i2cdev_cleanup();
    return res;
  }
  i2cdev_initialized ++;
  return 0;
}

int i2cdev_cleanup(void)
{
  int res;

  if (i2cdev_initialized >= 2) {
    if ((res = i2c_del_driver(&i2cdev_driver))) {
      printk("i2c-dev.o: Driver deregistration failed, "
             "module not removed.\n");
      return res;
    }
    i2cdev_initialized ++;
  }

  if (i2cdev_initialized >= 1) {
    if ((res = unregister_chrdev(I2C_MAJOR,"i2c"))) {
      printk("i2c-dev.o: unable to release major %d for i2c bus\n",I2C_MAJOR);
      return res;
    }
    i2cdev_initialized --;
  }
  return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C /dev entries driver");

int init_module(void)
{
  return i2c_dev_init();
}

int cleanup_module(void)
{
  return i2cdev_cleanup();
}

#endif /* def MODULE */

