/*
 * drivers/sbus/char/vfc_i2c.c
 *
 * Driver for the Videopix Frame Grabber.
 * 
 * Functions that support the Phillips i2c(I squared C) bus on the vfc
 *  Documentation for the Phillips I2C bus can be found on the 
 *  phillips home page
 *
 * Copyright (C) 1996 Manish Vachharajani (mvachhar@noc.rutgers.edu)
 *
 */

/* NOTE: It seems to me that the documentation regarding the
pcd8584t/pcf8584 does not show the correct way to address the i2c bus.
Based on the information on the I2C bus itself and the remainder of
the Phillips docs the following algorithims apper to be correct.  I am
fairly certain that the flowcharts in the phillips docs are wrong. */


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sbus.h>

#if 0 
#define VFC_I2C_DEBUG
#endif

#include "vfc.h"
#include "vfc_i2c.h"

#define VFC_I2C_READ (0x1)
#define VFC_I2C_WRITE (0x0)
     
/****** 
  The i2c bus controller chip on the VFC is a pcd8584t, but
  phillips claims it doesn't exist.  As far as I can tell it is
  identical to the PCF8584 so I treat it like it is the pcf8584.
  
  NOTE: The pcf8584 only cares
  about the msb of the word you feed it 
*****/

int vfc_pcf8584_init(struct vfc_dev *dev) 
{
	dev->regs->i2c_s1=RESET;        /* This will also choose
					   register S0_OWN so we can set it*/

	dev->regs->i2c_reg=0x55000000;  /* the pcf8584 shifts this
					   value left one bit and uses
					   it as its i2c bus address */
	dev->regs->i2c_s1=SELECT(S2);
	dev->regs->i2c_reg=0x14000000;  /* this will set the i2c bus at
					   the same speed sun uses,
					   and set another magic bit */
	
	dev->regs->i2c_s1=CLEAR_I2C_BUS;   /* enable the serial port,
					   idle the i2c bus and set
					   the data reg to s0 */
	udelay(100);
	return 0;
}

void vfc_i2c_delay_wakeup(struct vfc_dev *dev) 
{
	/* Used to profile code and eliminate too many delays */
	VFC_I2C_DEBUG_PRINTK(("vfc%d: Delaying\n",dev->instance));
	wake_up(&dev->poll_wait);
}

void vfc_i2c_delay_no_busy(struct vfc_dev *dev,unsigned long usecs) 
{
	dev->poll_timer.next = NULL;
	dev->poll_timer.prev = NULL;
	dev->poll_timer.expires = jiffies + 
		((unsigned long)usecs*(HZ))/1000000;
	dev->poll_timer.data=(unsigned long)dev;
	dev->poll_timer.function=(void *)(unsigned long)vfc_i2c_delay_wakeup;
	add_timer(&dev->poll_timer);
	sleep_on(&dev->poll_wait);
	del_timer(&dev->poll_timer);
}

void inline vfc_i2c_delay(struct vfc_dev *dev) 
{ 
	vfc_i2c_delay_no_busy(dev,100);
}

int vfc_init_i2c_bus(struct vfc_dev *dev)
{
	dev->regs->i2c_s1= ENABLE_SERIAL | SELECT(S0) | ACK;
	vfc_i2c_reset_bus(dev);
	return 0;
}

int vfc_i2c_reset_bus(struct vfc_dev *dev) 
{
	VFC_I2C_DEBUG_PRINTK((KERN_DEBUG "vfc%d: Resetting the i2c bus\n",
			  dev->instance));
	if(!dev) return -EINVAL;
	if(!dev->regs) return -EINVAL;
	dev->regs->i2c_s1=SEND_I2C_STOP;
	dev->regs->i2c_s1=SEND_I2C_STOP | ACK;
	vfc_i2c_delay(dev);
	dev->regs->i2c_s1=CLEAR_I2C_BUS;
	VFC_I2C_DEBUG_PRINTK((KERN_DEBUG "vfc%d: I2C status %x\n",
			  dev->instance, dev->regs->i2c_s1));
	return 0;
}

int vfc_i2c_wait_for_bus(struct vfc_dev *dev) 
{
	int timeout=1000; 

	while(!(dev->regs->i2c_s1 & BB)) {
		if(!(timeout--)) return -ETIMEDOUT;
		vfc_i2c_delay(dev);
	}
	return 0;
}

int vfc_i2c_wait_for_pin(struct vfc_dev *dev, int ack)
{
	int timeout=1000; 
	int s1;

	while((s1=dev->regs->i2c_s1) & PIN) {
		if(!(timeout--)) return -ETIMEDOUT;
		vfc_i2c_delay(dev);
	}
	if(ack==VFC_I2C_ACK_CHECK) {
		if(s1 & LRB) return -EIO; 
	}
	return 0;
}

#define SHIFT(a) ((a) << 24)
int vfc_i2c_xmit_addr(struct vfc_dev *dev, unsigned char addr, char mode) 
{ 
	int ret,raddr;
#if 1
	dev->regs->i2c_s1=SEND_I2C_STOP | ACK;
	dev->regs->i2c_s1=SELECT(S0) | ENABLE_SERIAL;
	vfc_i2c_delay(dev);
#endif

	switch(mode) {
	case VFC_I2C_READ:
		dev->regs->i2c_reg=raddr=SHIFT((unsigned int)addr | 0x1);
		VFC_I2C_DEBUG_PRINTK(("vfc%d: receiving from i2c addr 0x%x\n",
				  dev->instance,addr | 0x1));
		break;
	case VFC_I2C_WRITE:
		dev->regs->i2c_reg=raddr=SHIFT((unsigned int)addr & ~0x1);
		VFC_I2C_DEBUG_PRINTK(("vfc%d: sending to i2c addr 0x%x\n",
				  dev->instance,addr & ~0x1));
		break;
	default:
		return -EINVAL;
	}
	dev->regs->i2c_s1 = SEND_I2C_START;
	vfc_i2c_delay(dev);
	ret=vfc_i2c_wait_for_pin(dev,VFC_I2C_ACK_CHECK); /* We wait
							    for the
							    i2c send
							    to finish
							    here but
							    Sun
							    doesn't,
							    hmm */
	if(ret) {
		printk(KERN_ERR "vfc%d: VFC xmit addr timed out or no ack\n",
		       dev->instance);
		return ret;
	} else if(mode == VFC_I2C_READ) {
		if((ret=dev->regs->i2c_reg & 0xff000000) != raddr) {
			printk(KERN_WARNING 
			       "vfc%d: returned slave address "
			       "mismatch(%x,%x)\n",
			       dev->instance,raddr,ret);
		}
	}	
	return 0;
}

int vfc_i2c_xmit_byte(struct vfc_dev *dev,unsigned char *byte) 
{
	int ret;
	dev->regs->i2c_reg=SHIFT((unsigned int)*byte);

	ret=vfc_i2c_wait_for_pin(dev,VFC_I2C_ACK_CHECK); 
	switch(ret) {
	case -ETIMEDOUT: 
		printk(KERN_ERR "vfc%d: VFC xmit byte timed out or no ack\n",
		       dev->instance);
		break;
	case -EIO:
		ret=XMIT_LAST_BYTE;
		break;
	default:
		break;
	}
	return ret;
}

int vfc_i2c_recv_byte(struct vfc_dev *dev, unsigned char *byte, int last) 
{
	int ret;
	if(last) {
		dev->regs->i2c_reg=NEGATIVE_ACK;
		VFC_I2C_DEBUG_PRINTK(("vfc%d: sending negative ack\n",
				  dev->instance));
	} else {
		dev->regs->i2c_s1=ACK;
	}

	ret=vfc_i2c_wait_for_pin(dev,VFC_I2C_NO_ACK_CHECK);
	if(ret) {
		printk(KERN_ERR "vfc%d: "
		       "VFC recv byte timed out\n",dev->instance);
	}
	*byte=(dev->regs->i2c_reg) >> 24;
	return ret;
}

int vfc_i2c_recvbuf(struct vfc_dev *dev, unsigned char addr,
		    char *buf, int count)
{
	int ret,last;

	if(!(count && buf && dev && dev->regs) ) return -EINVAL;

	if((ret=vfc_i2c_wait_for_bus(dev))) {
		printk(KERN_ERR "vfc%d: VFC I2C bus busy\n",dev->instance);
		return ret;
	}

	if((ret=vfc_i2c_xmit_addr(dev,addr,VFC_I2C_READ))) {
		dev->regs->i2c_s1=SEND_I2C_STOP;
		vfc_i2c_delay(dev);
		return ret;
	}
	
	last=0;
	while(count--) {
		if(!count) last=1;
		if((ret=vfc_i2c_recv_byte(dev,buf,last))) {
			printk(KERN_ERR "vfc%d: "
			       "VFC error while receiving byte\n",
			       dev->instance);
			dev->regs->i2c_s1=SEND_I2C_STOP;
			ret=-EINVAL;
		}
		buf++;
	}
	
	dev->regs->i2c_s1=SEND_I2C_STOP | ACK;
	vfc_i2c_delay(dev);
	return ret;
}

int vfc_i2c_sendbuf(struct vfc_dev *dev, unsigned char addr, 
		    char *buf, int count) 
{
	int ret;
	
	if(!(buf && dev && dev->regs) ) return -EINVAL;
	
	if((ret=vfc_i2c_wait_for_bus(dev))) {
		printk(KERN_ERR "vfc%d: VFC I2C bus busy\n",dev->instance);
		return ret;
	}
	
	if((ret=vfc_i2c_xmit_addr(dev,addr,VFC_I2C_WRITE))) {
		dev->regs->i2c_s1=SEND_I2C_STOP;
		vfc_i2c_delay(dev);
		return ret;
	}
	
	while(count--) {
		ret=vfc_i2c_xmit_byte(dev,buf);
		switch(ret) {
		case XMIT_LAST_BYTE:
			VFC_I2C_DEBUG_PRINTK(("vfc%d: "
					  "Reciever ended transmission with "
					  " %d bytes remaining\n",
					  dev->instance,count));
			ret=0;
			goto done;
			break;
		case 0:
			break;
		default:
			printk(KERN_ERR "vfc%d: "
			       "VFC error while sending byte\n",dev->instance);
			break;
		}
		buf++;
	}
done:
	dev->regs->i2c_s1=SEND_I2C_STOP | ACK;
	
	vfc_i2c_delay(dev);
	return ret;
}









