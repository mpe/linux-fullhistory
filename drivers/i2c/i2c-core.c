/* i2c-core.c - a device driver for the iic-bus interface		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-99 Simon G. Vogl

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Ky�sti M�lkki <kmalkki@cc.hut.fi>.
   All SMBus-related things are written by Frodo Looijaard <frodol@dds.nl>
   SMBus 2.0 support by Mark Studebaker <mdsxyz123@yahoo.com>                */

/* $Id: i2c-core.c,v 1.95 2003/01/22 05:25:08 kmalkki Exp $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/uaccess.h>


#define DEB(x) if (i2c_debug>=1) x;
#define DEB2(x) if (i2c_debug>=2) x;

static struct i2c_adapter *adapters[I2C_ADAP_MAX];
static struct i2c_driver *drivers[I2C_DRIVER_MAX];
static DECLARE_MUTEX(core_lists);

/**** debug level */
static int i2c_debug;

#ifdef CONFIG_PROC_FS
static int i2cproc_register(struct i2c_adapter *adap, int bus);
static void i2cproc_remove(int bus);
#else
# define i2cproc_register(adap, bus)	0
# define i2cproc_remove(bus)		do { } while (0)
#endif /* CONFIG_PROC_FS */


/* ---------------------------------------------------
 * registering functions 
 * --------------------------------------------------- 
 */

/* -----
 * i2c_add_adapter is called from within the algorithm layer,
 * when a new hw adapter registers. A new device is register to be
 * available for clients.
 */
int i2c_add_adapter(struct i2c_adapter *adap)
{
	int res = 0, i, j;

	down(&core_lists);
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (NULL == adapters[i])
			break;
	if (I2C_ADAP_MAX == i) {
		printk(KERN_WARNING 
		       " i2c-core.o: register_adapter(%s) - enlarge I2C_ADAP_MAX.\n",
			adap->name);
		res = -ENOMEM;
		goto out_unlock;
	}

	res = i2cproc_register(adap, i);
	if (res)
		goto out_unlock;

	adapters[i] = adap;

	init_MUTEX(&adap->bus);
	init_MUTEX(&adap->list);

	/* inform drivers of new adapters */
	for (j=0;j<I2C_DRIVER_MAX;j++)
		if (drivers[j]!=NULL && 
		    (drivers[j]->flags&(I2C_DF_NOTIFY|I2C_DF_DUMMY)))
			/* We ignore the return code; if it fails, too bad */
			drivers[j]->attach_adapter(adap);
	up(&core_lists);
	
	DEB(printk(KERN_DEBUG "i2c-core.o: adapter %s registered as adapter %d.\n",
	           adap->name,i));

 out_unlock:
	up(&core_lists);
	return res;;
}


int i2c_del_adapter(struct i2c_adapter *adap)
{
	int res = 0, i, j;

	down(&core_lists);
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adap == adapters[i])
			break;
	if (I2C_ADAP_MAX == i) {
		printk( KERN_WARNING "i2c-core.o: unregister_adapter adap [%s] not found.\n",
			adap->name);
		res = -ENODEV;
		goto out_unlock;
	}

	/* DUMMY drivers do not register their clients, so we have to
	 * use a trick here: we call driver->attach_adapter to
	 * *detach* it! Of course, each dummy driver should know about
	 * this or hell will break loose...
	 */
	for (j = 0; j < I2C_DRIVER_MAX; j++) 
		if (drivers[j] && (drivers[j]->flags & I2C_DF_DUMMY))
			if ((res = drivers[j]->attach_adapter(adap))) {
				printk(KERN_WARNING "i2c-core.o: can't detach adapter %s "
				       "while detaching driver %s: driver not "
				       "detached!",adap->name,drivers[j]->name);
				goto out_unlock;
			}

	/* detach any active clients. This must be done first, because
	 * it can fail; in which case we give upp. */
	for (j=0;j<I2C_CLIENT_MAX;j++) {
		struct i2c_client *client = adap->clients[j];
		if (client!=NULL) {
		    /* detaching devices is unconditional of the set notify
		     * flag, as _all_ clients that reside on the adapter
		     * must be deleted, as this would cause invalid states.
		     */
			if ((res=client->driver->detach_client(client))) {
				printk(KERN_ERR "i2c-core.o: adapter %s not "
					"unregistered, because client at "
					"address %02x can't be detached. ",
					adap->name, client->addr);
				goto out_unlock;
			}
		}
	}

	i2cproc_remove(i);

	adapters[i] = NULL;

	DEB(printk(KERN_DEBUG "i2c-core.o: adapter unregistered: %s\n",adap->name));

 out_unlock:
	up(&core_lists);
	return res;
}


/* -----
 * What follows is the "upwards" interface: commands for talking to clients,
 * which implement the functions to access the physical information of the
 * chips.
 */

int i2c_add_driver(struct i2c_driver *driver)
{
	int res = 0, i;

	down(&core_lists);
	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (NULL == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i) {
		printk(KERN_WARNING 
		       " i2c-core.o: register_driver(%s) "
		       "- enlarge I2C_DRIVER_MAX.\n",
			driver->name);
		res = -ENOMEM;
		goto out_unlock;
	}

	drivers[i] = driver;
	
	DEB(printk(KERN_DEBUG "i2c-core.o: driver %s registered.\n",driver->name));
	
	/* now look for instances of driver on our adapters
	 */
	if (driver->flags& (I2C_DF_NOTIFY|I2C_DF_DUMMY)) {
		for (i=0;i<I2C_ADAP_MAX;i++) {
			if (adapters[i]!=NULL)
				/* Ignore errors */
				driver->attach_adapter(adapters[i]);
		}
	}

 out_unlock:
	up(&core_lists);
	return res;
}

int i2c_del_driver(struct i2c_driver *driver)
{
	int res = 0, i, j, k;

	down(&core_lists);
	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (driver == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i) {
		printk(KERN_WARNING " i2c-core.o: unregister_driver: "
				    "[%s] not found\n",
			driver->name);
		res = -ENODEV;
		goto out_unlock;
	}

	/* Have a look at each adapter, if clients of this driver are still
	 * attached. If so, detach them to be able to kill the driver 
	 * afterwards.
	 */
	DEB2(printk(KERN_DEBUG "i2c-core.o: unregister_driver - looking for clients.\n"));

	/* removing clients does not depend on the notify flag, else 
	 * invalid operation might (will!) result, when using stale client
	 * pointers.
	 */
	for (k=0;k<I2C_ADAP_MAX;k++) {
		struct i2c_adapter *adap = adapters[k];
		if (adap == NULL) /* skip empty entries. */
			continue;
		DEB2(printk(KERN_DEBUG "i2c-core.o: examining adapter %s:\n",
			    adap->name));
		if (driver->flags & I2C_DF_DUMMY) {
		/* DUMMY drivers do not register their clients, so we have to
		 * use a trick here: we call driver->attach_adapter to
		 * *detach* it! Of course, each dummy driver should know about
		 * this or hell will break loose...  
		 */
			if ((res = driver->attach_adapter(adap))) {
				printk(KERN_WARNING "i2c-core.o: while unregistering "
				       "dummy driver %s, adapter %s could "
				       "not be detached properly; driver "
				       "not unloaded!",driver->name,
				       adap->name);
				goto out_unlock;
			}
		} else {
			for (j=0;j<I2C_CLIENT_MAX;j++) { 
				struct i2c_client *client = adap->clients[j];
				if (client != NULL && 
				    client->driver == driver) {
					DEB2(printk(KERN_DEBUG "i2c-core.o: "
						    "detaching client %s:\n",
					            client->name));
					if ((res = driver->
							detach_client(client)))
					{
						printk(KERN_ERR "i2c-core.o: while "
						       "unregistering driver "
						       "`%s', the client at "
						       "address %02x of "
						       "adapter `%s' could not "
						       "be detached; driver "
						       "not unloaded!",
						       driver->name,
						       client->addr,
						       adap->name);
						goto out_unlock;
					}
				}
			}
		}
	}
	drivers[i] = NULL;
	
	DEB(printk(KERN_DEBUG "i2c-core.o: driver unregistered: %s\n",driver->name));

 out_unlock:
	up(&core_lists);
	return 0;
}

static int __i2c_check_addr(struct i2c_adapter *adapter, int addr)
{
	int i;

	for (i = 0; i < I2C_CLIENT_MAX ; i++)
		if (adapter->clients[i] && (adapter->clients[i]->addr == addr))
			return -EBUSY;

	return 0;
}

int i2c_check_addr(struct i2c_adapter *adapter, int addr)
{
	int rval;

	down(&adapter->list);
	rval = __i2c_check_addr(adapter, addr);
	up(&adapter->list);

	return rval;
}

int i2c_attach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int i;

	down(&adapter->list);
	if (__i2c_check_addr(client->adapter, client->addr))
		goto out_unlock_list;

	for (i = 0; i < I2C_CLIENT_MAX; i++) {
		if (!adapter->clients[i])
			goto free_slot;
	}

	printk(KERN_WARNING 
	       " i2c-core.o: attach_client(%s) - enlarge I2C_CLIENT_MAX.\n",
	       client->name);

 out_unlock_list:
	up(&adapter->list);
	return -EBUSY;

 free_slot:
	adapter->clients[i] = client;
	up(&adapter->list);
	
	if (adapter->client_register)  {
		if (adapter->client_register(client))  {
			printk(KERN_DEBUG
			       "i2c-core.o: warning: client_register seems "
			       "to have failed for client %02x at adapter %s\n",
			       client->addr, adapter->name);
		}
	}

	DEB(printk(KERN_DEBUG
		   "i2c-core.o: client [%s] registered to adapter [%s] "
		   "(pos. %d).\n", client->name, adapter->name, i));

	if (client->flags & I2C_CLIENT_ALLOW_USE)
		client->usage_count = 0;
	return 0;
}


int i2c_detach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int res = 0, i;
	
	if ((client->flags & I2C_CLIENT_ALLOW_USE) && (client->usage_count > 0))
		return -EBUSY;

	if (adapter->client_unregister)  {
		res = adapter->client_unregister(client);
		if (res) {
			printk(KERN_ERR
			       "i2c-core.o: client_unregister [%s] failed, "
			       "client not detached", client->name);
			goto out;
		}
	}

	down(&adapter->list);
	for (i = 0; i < I2C_CLIENT_MAX; i++) {
		if (client == adapter->clients[i]) {
			adapter->clients[i] = NULL;
			goto out_unlock;
		}
	}

	printk(KERN_WARNING
	       " i2c-core.o: unregister_client [%s] not found\n",
	       client->name);
	res = -ENODEV;

 out_unlock:
	up(&adapter->list);
 out:
	return res;
}

static int i2c_inc_use_client(struct i2c_client *client)
{

	if (!try_module_get(client->driver->owner))
		return -ENODEV;
	if (!try_module_get(client->adapter->owner)) {
		module_put(client->driver->owner);
		return -ENODEV;
	}

	return 0;
}

static void i2c_dec_use_client(struct i2c_client *client)
{
	module_put(client->driver->owner);
	module_put(client->adapter->owner);
}

int i2c_use_client(struct i2c_client *client)
{
	if (!i2c_inc_use_client(client))
		return -ENODEV;

	if (client->flags & I2C_CLIENT_ALLOW_USE) {
		if (client->flags & I2C_CLIENT_ALLOW_MULTIPLE_USE)
			client->usage_count++;
		else if (client->usage_count > 0) 
			goto busy;
		else 
			client->usage_count++;
	}

	return 0;
 busy:
	i2c_dec_use_client(client);
	return -EBUSY;
}

int i2c_release_client(struct i2c_client *client)
{
	if(client->flags & I2C_CLIENT_ALLOW_USE) {
		if(client->usage_count>0)
			client->usage_count--;
		else
		{
			printk(KERN_WARNING " i2c-core.o: dec_use_client used one too many times\n");
			return -EPERM;
		}
	}
	
	i2c_dec_use_client(client);
	
	return 0;
}

/* ----------------------------------------------------
 * The /proc functions
 * ----------------------------------------------------
 */

#ifdef CONFIG_PROC_FS
/* This function generates the output for /proc/bus/i2c */
static int read_bus_i2c(char *buf, char **start, off_t offset,
			int len, int *eof, void *private)
{
	int i;
	int nr = 0;

	/* Note that it is safe to write a `little' beyond len. Yes, really. */
	/* Fuck you.  Will convert this to seq_file later.  --hch */

	down(&core_lists);
	for (i = 0; (i < I2C_ADAP_MAX) && (nr < len); i++) {
		if (adapters[i]) {
			nr += sprintf(buf+nr, "i2c-%d\t", i);
			if (adapters[i]->algo->smbus_xfer) {
				if (adapters[i]->algo->master_xfer)
					nr += sprintf(buf+nr,"smbus/i2c");
				else
					nr += sprintf(buf+nr,"smbus    ");
			} else if (adapters[i]->algo->master_xfer)
				nr += sprintf(buf+nr,"i2c       ");
			else
				nr += sprintf(buf+nr,"dummy     ");
			nr += sprintf(buf+nr,"\t%-32s\t%-32s\n",
			              adapters[i]->name,
			              adapters[i]->algo->name);
		}
	}
	up(&core_lists);

	return nr;
}

/* This function generates the output for /proc/bus/i2c-? */
static ssize_t i2cproc_bus_read(struct file *file, char *buf,
				size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	char *kbuf;
	struct i2c_client *client;
	int i,j,k,order_nr,len=0;
	size_t len_total;
	int order[I2C_CLIENT_MAX];
#define OUTPUT_LENGTH_PER_LINE 70

	len_total = file->f_pos + count;
	if (len_total > (I2C_CLIENT_MAX * OUTPUT_LENGTH_PER_LINE) )
		/* adjust to maximum file size */
		len_total = (I2C_CLIENT_MAX * OUTPUT_LENGTH_PER_LINE);
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adapters[i]->inode == inode->i_ino) {
		/* We need a bit of slack in the kernel buffer; this makes the
		   sprintf safe. */
			if (! (kbuf = kmalloc(len_total +
			                      OUTPUT_LENGTH_PER_LINE,
			                      GFP_KERNEL)))
				return -ENOMEM;
			/* Order will hold the indexes of the clients
			   sorted by address */
			order_nr=0;
			for (j = 0; j < I2C_CLIENT_MAX; j++) {
				if ((client = adapters[i]->clients[j]) && 
				    (client->driver->id != I2C_DRIVERID_I2CDEV))  {
					for(k = order_nr; 
					    (k > 0) && 
					    adapters[i]->clients[order[k-1]]->
					             addr > client->addr; 
					    k--)
						order[k] = order[k-1];
					order[k] = j;
					order_nr++;
				}
			}


			for (j = 0; (j < order_nr) && (len < len_total); j++) {
				client = adapters[i]->clients[order[j]];
				len += sprintf(kbuf+len,"%02x\t%-32s\t%-32s\n",
				              client->addr,
				              client->name,
				              client->driver->name);
			}
			len = len - file->f_pos;
			if (len > count)
				len = count;
			if (len < 0) 
				len = 0;
			if (copy_to_user (buf,kbuf+file->f_pos, len)) {
				kfree(kbuf);
				return -EFAULT;
			}
			file->f_pos += len;
			kfree(kbuf);
			return len;
		}
	return -ENOENT;
}

static struct file_operations i2cproc_operations = {
	.read		= i2cproc_bus_read,
};

static int i2cproc_register(struct i2c_adapter *adap, int bus)
{
	struct proc_dir_entry *proc_entry;
	char name[8];

	sprintf(name, "i2c-%d", bus);

	proc_entry = create_proc_entry(name, 0, proc_bus);
	if (!proc_entry)
		goto fail;

	proc_entry->proc_fops = &i2cproc_operations;
	proc_entry->owner = adap->owner;
	adap->inode = proc_entry->low_ino;
	return 0;
 fail:
	printk(KERN_ERR "i2c-core.o: Could not create /proc/bus/%s\n", name);
	return -ENOENT;
}

static void i2cproc_remove(int bus)
{
	char name[8];

	sprintf(name,"i2c-%d", bus);
	remove_proc_entry(name, proc_bus);
}

static int __init i2cproc_init(void)
{
	struct proc_dir_entry *proc_bus_i2c;

	proc_bus_i2c = create_proc_entry("i2c",0,proc_bus);
	if (!proc_bus_i2c) {
		printk(KERN_ERR "i2c-core.o: Could not create /proc/bus/i2c");
		return -ENOENT;
 	}

	proc_bus_i2c->read_proc = &read_bus_i2c;
	proc_bus_i2c->owner = THIS_MODULE;
	return 0;
}

static void __exit i2cproc_cleanup(void)
{
	remove_proc_entry("i2c",proc_bus);
}

module_init(i2cproc_init);
module_exit(i2cproc_cleanup);
#endif /* def CONFIG_PROC_FS */

/* ----------------------------------------------------
 * the functional interface to the i2c busses.
 * ----------------------------------------------------
 */

int i2c_transfer(struct i2c_adapter * adap, struct i2c_msg msgs[],int num)
{
	int ret;

	if (adap->algo->master_xfer) {
 	 	DEB2(printk(KERN_DEBUG "i2c-core.o: master_xfer: %s with %d msgs.\n",
		            adap->name,num));

		down(&adap->bus);
		ret = adap->algo->master_xfer(adap,msgs,num);
		up(&adap->bus);

		return ret;
	} else {
		printk(KERN_ERR "i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
		       adap->id);
		return -ENOSYS;
	}
}

int i2c_master_send(struct i2c_client *client,const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;

	if (client->adapter->algo->master_xfer) {
		msg.addr   = client->addr;
		msg.flags = client->flags & I2C_M_TEN;
		msg.len = count;
		(const char *)msg.buf = buf;
	
		DEB2(printk(KERN_DEBUG "i2c-core.o: master_send: writing %d bytes on %s.\n",
			count,client->adapter->name));
	
		down(&adap->bus);
		ret = adap->algo->master_xfer(adap,&msg,1);
		up(&adap->bus);

		/* if everything went ok (i.e. 1 msg transmitted), return #bytes
		 * transmitted, else error code.
		 */
		return (ret == 1 )? count : ret;
	} else {
		printk(KERN_ERR "i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
		       client->adapter->id);
		return -ENOSYS;
	}
}

int i2c_master_recv(struct i2c_client *client, char *buf ,int count)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	if (client->adapter->algo->master_xfer) {
		msg.addr   = client->addr;
		msg.flags = client->flags & I2C_M_TEN;
		msg.flags |= I2C_M_RD;
		msg.len = count;
		msg.buf = buf;

		DEB2(printk(KERN_DEBUG "i2c-core.o: master_recv: reading %d bytes on %s.\n",
			count,client->adapter->name));
	
		down(&adap->bus);
		ret = adap->algo->master_xfer(adap,&msg,1);
		up(&adap->bus);
	
		DEB2(printk(KERN_DEBUG "i2c-core.o: master_recv: return:%d (count:%d, addr:0x%02x)\n",
			ret, count, client->addr));
	
		/* if everything went ok (i.e. 1 msg transmitted), return #bytes
	 	* transmitted, else error code.
	 	*/
		return (ret == 1 )? count : ret;
	} else {
		printk(KERN_DEBUG "i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
		       client->adapter->id);
		return -ENOSYS;
	}
}


int i2c_control(struct i2c_client *client,
	unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct i2c_adapter *adap = client->adapter;

	DEB2(printk(KERN_DEBUG "i2c-core.o: i2c ioctl, cmd: 0x%x, arg: %#lx\n", cmd, arg));
	switch ( cmd ) {
		case I2C_RETRIES:
			adap->retries = arg;
			break;
		case I2C_TIMEOUT:
			adap->timeout = arg;
			break;
		default:
			if (adap->algo->algo_control!=NULL)
				ret = adap->algo->algo_control(adap,cmd,arg);
	}
	return ret;
}

/* ----------------------------------------------------
 * the i2c address scanning function
 * Will not work for 10-bit addresses!
 * ----------------------------------------------------
 */
int i2c_probe(struct i2c_adapter *adapter,
                   struct i2c_client_address_data *address_data,
                   i2c_client_found_addr_proc *found_proc)
{
	int addr,i,found,err;
	int adap_id = i2c_adapter_id(adapter);

	/* Forget it if we can't probe using SMBUS_QUICK */
	if (! i2c_check_functionality(adapter,I2C_FUNC_SMBUS_QUICK))
		return -1;

	for (addr = 0x00; addr <= 0x7f; addr++) {

		/* Skip if already in use */
		if (i2c_check_addr(adapter,addr))
			continue;

		/* If it is in one of the force entries, we don't do any detection
		   at all */
		found = 0;

		for (i = 0; !found && (address_data->force[i] != I2C_CLIENT_END); i += 3) {
			if (((adap_id == address_data->force[i]) || 
			     (address_data->force[i] == ANY_I2C_BUS)) &&
			     (addr == address_data->force[i+1])) {
				DEB2(printk(KERN_DEBUG "i2c-core.o: found force parameter for adapter %d, addr %04x\n",
				            adap_id,addr));
				if ((err = found_proc(adapter,addr,0,0)))
					return err;
				found = 1;
			}
		}
		if (found) 
			continue;

		/* If this address is in one of the ignores, we can forget about
		   it right now */
		for (i = 0;
		     !found && (address_data->ignore[i] != I2C_CLIENT_END);
		     i += 2) {
			if (((adap_id == address_data->ignore[i]) || 
			    ((address_data->ignore[i] == ANY_I2C_BUS))) &&
			    (addr == address_data->ignore[i+1])) {
				DEB2(printk(KERN_DEBUG "i2c-core.o: found ignore parameter for adapter %d, "
				     "addr %04x\n", adap_id ,addr));
				found = 1;
			}
		}
		for (i = 0;
		     !found && (address_data->ignore_range[i] != I2C_CLIENT_END);
		     i += 3) {
			if (((adap_id == address_data->ignore_range[i]) ||
			    ((address_data->ignore_range[i]==ANY_I2C_BUS))) &&
			    (addr >= address_data->ignore_range[i+1]) &&
			    (addr <= address_data->ignore_range[i+2])) {
				DEB2(printk(KERN_DEBUG "i2c-core.o: found ignore_range parameter for adapter %d, "
				            "addr %04x\n", adap_id,addr));
				found = 1;
			}
		}
		if (found) 
			continue;

		/* Now, we will do a detection, but only if it is in the normal or 
		   probe entries */  
		for (i = 0;
		     !found && (address_data->normal_i2c[i] != I2C_CLIENT_END);
		     i += 1) {
			if (addr == address_data->normal_i2c[i]) {
				found = 1;
				DEB2(printk(KERN_DEBUG "i2c-core.o: found normal i2c entry for adapter %d, "
				            "addr %02x", adap_id,addr));
			}
		}

		for (i = 0;
		     !found && (address_data->normal_i2c_range[i] != I2C_CLIENT_END);
		     i += 2) {
			if ((addr >= address_data->normal_i2c_range[i]) &&
			    (addr <= address_data->normal_i2c_range[i+1])) {
				found = 1;
				DEB2(printk(KERN_DEBUG "i2c-core.o: found normal i2c_range entry for adapter %d, "
				            "addr %04x\n", adap_id,addr));
			}
		}

		for (i = 0;
		     !found && (address_data->probe[i] != I2C_CLIENT_END);
		     i += 2) {
			if (((adap_id == address_data->probe[i]) ||
			    ((address_data->probe[i] == ANY_I2C_BUS))) &&
			    (addr == address_data->probe[i+1])) {
				found = 1;
				DEB2(printk(KERN_DEBUG "i2c-core.o: found probe parameter for adapter %d, "
				            "addr %04x\n", adap_id,addr));
			}
		}
		for (i = 0;
		     !found && (address_data->probe_range[i] != I2C_CLIENT_END);
		     i += 3) {
			if (((adap_id == address_data->probe_range[i]) ||
			   (address_data->probe_range[i] == ANY_I2C_BUS)) &&
			   (addr >= address_data->probe_range[i+1]) &&
			   (addr <= address_data->probe_range[i+2])) {
				found = 1;
				DEB2(printk(KERN_DEBUG "i2c-core.o: found probe_range parameter for adapter %d, "
				            "addr %04x\n", adap_id,addr));
			}
		}
		if (!found) 
			continue;

		/* OK, so we really should examine this address. First check
		   whether there is some client here at all! */
		if (i2c_smbus_xfer(adapter,addr,0,0,0,I2C_SMBUS_QUICK,NULL) >= 0)
			if ((err = found_proc(adapter,addr,0,-1)))
				return err;
	}
	return 0;
}

/*
 * return id number for a specific adapter
 */
int i2c_adapter_id(struct i2c_adapter *adap)
{
	int i;
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adap == adapters[i])
			return i;
	return -1;
}

/* The SMBus parts */

#define POLY    (0x1070U << 3) 
static u8
crc8(u16 data)
{
	int i;
  
	for(i = 0; i < 8; i++) {
		if (data & 0x8000) 
			data = data ^ POLY;
		data = data << 1;
	}
	return (u8)(data >> 8);
}

/* CRC over count bytes in the first array plus the bytes in the rest
   array if it is non-null. rest[0] is the (length of rest) - 1
   and is included. */
u8 i2c_smbus_partial_pec(u8 crc, int count, u8 *first, u8 *rest)
{
	int i;

	for(i = 0; i < count; i++)
		crc = crc8((crc ^ first[i]) << 8);
	if(rest != NULL)
		for(i = 0; i <= rest[0]; i++)
			crc = crc8((crc ^ rest[i]) << 8);
	return crc;
}

u8 i2c_smbus_pec(int count, u8 *first, u8 *rest)
{
	return i2c_smbus_partial_pec(0, count, first, rest);
}

/* Returns new "size" (transaction type)
   Note that we convert byte to byte_data and byte_data to word_data
   rather than invent new xxx_PEC transactions. */
int i2c_smbus_add_pec(u16 addr, u8 command, int size,
                      union i2c_smbus_data *data)
{
	u8 buf[3];

	buf[0] = addr << 1;
	buf[1] = command;
	switch(size) {
		case I2C_SMBUS_BYTE:
			data->byte = i2c_smbus_pec(2, buf, NULL);
			size = I2C_SMBUS_BYTE_DATA;
			break;
		case I2C_SMBUS_BYTE_DATA:
			buf[2] = data->byte;
			data->word = buf[2] ||
			            (i2c_smbus_pec(3, buf, NULL) << 8);
			size = I2C_SMBUS_WORD_DATA;
			break;
		case I2C_SMBUS_WORD_DATA:
			/* unsupported */
			break;
		case I2C_SMBUS_BLOCK_DATA:
			data->block[data->block[0] + 1] =
			             i2c_smbus_pec(2, buf, data->block);
			size = I2C_SMBUS_BLOCK_DATA_PEC;
			break;
	}
	return size;	
}

int i2c_smbus_check_pec(u16 addr, u8 command, int size, u8 partial,
                        union i2c_smbus_data *data)
{
	u8 buf[3], rpec, cpec;

	buf[1] = command;
	switch(size) {
		case I2C_SMBUS_BYTE_DATA:
			buf[0] = (addr << 1) | 1;
			cpec = i2c_smbus_pec(2, buf, NULL);
			rpec = data->byte;
			break;
		case I2C_SMBUS_WORD_DATA:
			buf[0] = (addr << 1) | 1;
			buf[2] = data->word & 0xff;
			cpec = i2c_smbus_pec(3, buf, NULL);
			rpec = data->word >> 8;
			break;
		case I2C_SMBUS_WORD_DATA_PEC:
			/* unsupported */
			cpec = rpec = 0;
			break;
		case I2C_SMBUS_PROC_CALL_PEC:
			/* unsupported */
			cpec = rpec = 0;
			break;
		case I2C_SMBUS_BLOCK_DATA_PEC:
			buf[0] = (addr << 1);
			buf[2] = (addr << 1) | 1;
			cpec = i2c_smbus_pec(3, buf, data->block);
			rpec = data->block[data->block[0] + 1];
			break;
		case I2C_SMBUS_BLOCK_PROC_CALL_PEC:
			buf[0] = (addr << 1) | 1;
			rpec = i2c_smbus_partial_pec(partial, 1,
			                             buf, data->block);
			cpec = data->block[data->block[0] + 1];
			break;
		default:
			cpec = rpec = 0;
			break;
	}
	if(rpec != cpec) {
		DEB(printk(KERN_DEBUG "i2c-core.o: Bad PEC 0x%02x vs. 0x%02x\n",
		           rpec, cpec));
		return -1;
	}
	return 0;	
}

extern s32 i2c_smbus_write_quick(struct i2c_client * client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
 	                      value,0,I2C_SMBUS_QUICK,NULL);
}

extern s32 i2c_smbus_read_byte(struct i2c_client * client)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,0,I2C_SMBUS_BYTE, &data))
		return -1;
	else
		return 0x0FF & data.byte;
}

extern s32 i2c_smbus_write_byte(struct i2c_client * client, u8 value)
{
	union i2c_smbus_data data;	/* only for PEC */
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,value, I2C_SMBUS_BYTE,&data);
}

extern s32 i2c_smbus_read_byte_data(struct i2c_client * client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_BYTE_DATA,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

extern s32 i2c_smbus_write_byte_data(struct i2c_client * client, u8 command,
                                     u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BYTE_DATA,&data);
}

extern s32 i2c_smbus_read_word_data(struct i2c_client * client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_WORD_DATA, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

extern s32 i2c_smbus_write_word_data(struct i2c_client * client,
                                     u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_WORD_DATA,&data);
}

extern s32 i2c_smbus_process_call(struct i2c_client * client,
                                  u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_WRITE,command,
	                   I2C_SMBUS_PROC_CALL, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

/* Returns the number of read bytes */
extern s32 i2c_smbus_read_block_data(struct i2c_client * client,
                                     u8 command, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command,
	                   I2C_SMBUS_BLOCK_DATA,&data))
		return -1;
	else {
		for (i = 1; i <= data.block[0]; i++)
			values[i-1] = data.block[i];
		return data.block[0];
	}
}

extern s32 i2c_smbus_write_block_data(struct i2c_client * client,
                                      u8 command, u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BLOCK_DATA,&data);
}

/* Returns the number of read bytes */
extern s32 i2c_smbus_block_process_call(struct i2c_client * client,
                                        u8 command, u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_BLOCK_MAX - 1)
		return -1;
	data.block[0] = length;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	if(i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                  I2C_SMBUS_WRITE, command,
	                  I2C_SMBUS_BLOCK_PROC_CALL, &data))
		return -1;
	for (i = 1; i <= data.block[0]; i++)
		values[i-1] = data.block[i];
	return data.block[0];
}

/* Returns the number of read bytes */
extern s32 i2c_smbus_read_i2c_block_data(struct i2c_client * client,
                                         u8 command, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_READ,command,
	                      I2C_SMBUS_I2C_BLOCK_DATA,&data))
		return -1;
	else {
		for (i = 1; i <= data.block[0]; i++)
			values[i-1] = data.block[i];
		return data.block[0];
	}
}

extern s32 i2c_smbus_write_i2c_block_data(struct i2c_client * client,
                                          u8 command, u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_I2C_BLOCK_MAX)
		length = I2C_SMBUS_I2C_BLOCK_MAX;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_I2C_BLOCK_DATA,&data);
}

/* Simulate a SMBus command using the i2c protocol 
   No checking of parameters is done!  */
static s32 i2c_smbus_xfer_emulated(struct i2c_adapter * adapter, u16 addr, 
                                   unsigned short flags,
                                   char read_write, u8 command, int size, 
                                   union i2c_smbus_data * data)
{
	/* So we need to generate a series of msgs. In the case of writing, we
	  need to use only one message; when reading, we need two. We initialize
	  most things with sane defaults, to keep the code below somewhat
	  simpler. */
	unsigned char msgbuf0[34];
	unsigned char msgbuf1[34];
	int num = read_write == I2C_SMBUS_READ?2:1;
	struct i2c_msg msg[2] = { { addr, flags, 1, msgbuf0 }, 
	                          { addr, flags | I2C_M_RD, 0, msgbuf1 }
	                        };
	int i;

	msgbuf0[0] = command;
	switch(size) {
	case I2C_SMBUS_QUICK:
		msg[0].len = 0;
		/* Special case: The read/write field is used as data */
		msg[0].flags = flags | (read_write==I2C_SMBUS_READ)?I2C_M_RD:0;
		num = 1;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			/* Special case: only a read! */
			msg[0].flags = I2C_M_RD | flags;
			num = 1;
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 1;
		else {
			msg[0].len = 2;
			msgbuf0[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 2;
		else {
			msg[0].len=3;
			msgbuf0[1] = data->word & 0xff;
			msgbuf0[2] = (data->word >> 8) & 0xff;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		read_write = I2C_SMBUS_READ;
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = (data->word >> 8) & 0xff;
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_BLOCK_DATA_PEC:
		if (read_write == I2C_SMBUS_READ) {
			printk(KERN_ERR "i2c-core.o: Block read not supported "
			       "under I2C emulation!\n");
			return -1;
		} else {
			msg[0].len = data->block[0] + 2;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 2) {
				printk(KERN_ERR "i2c-core.o: smbus_access called with "
				       "invalid block write size (%d)\n",
				       data->block[0]);
				return -1;
			}
			if(size == I2C_SMBUS_BLOCK_DATA_PEC)
				(msg[0].len)++;
			for (i = 1; i <= msg[0].len; i++)
				msgbuf0[i] = data->block[i-1];
		}
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
	case I2C_SMBUS_BLOCK_PROC_CALL_PEC:
		printk(KERN_ERR "i2c-core.o: Block process call not supported "
		       "under I2C emulation!\n");
		return -1;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].len = I2C_SMBUS_I2C_BLOCK_MAX;
		} else {
			msg[0].len = data->block[0] + 1;
			if (msg[0].len > I2C_SMBUS_I2C_BLOCK_MAX + 1) {
				printk("i2c-core.o: i2c_smbus_xfer_emulated called with "
				       "invalid block write size (%d)\n",
				       data->block[0]);
				return -1;
			}
			for (i = 1; i <= data->block[0]; i++)
				msgbuf0[i] = data->block[i];
		}
		break;
	default:
		printk(KERN_ERR "i2c-core.o: smbus_access called with invalid size (%d)\n",
		       size);
		return -1;
	}

	if (i2c_transfer(adapter, msg, num) < 0)
		return -1;

	if (read_write == I2C_SMBUS_READ)
		switch(size) {
			case I2C_SMBUS_BYTE:
				data->byte = msgbuf0[0];
				break;
			case I2C_SMBUS_BYTE_DATA:
				data->byte = msgbuf1[0];
				break;
			case I2C_SMBUS_WORD_DATA: 
			case I2C_SMBUS_PROC_CALL:
				data->word = msgbuf1[0] | (msgbuf1[1] << 8);
				break;
			case I2C_SMBUS_I2C_BLOCK_DATA:
				/* fixed at 32 for now */
				data->block[0] = I2C_SMBUS_I2C_BLOCK_MAX;
				for (i = 0; i < I2C_SMBUS_I2C_BLOCK_MAX; i++)
					data->block[i+1] = msgbuf1[i];
				break;
		}
	return 0;
}


s32 i2c_smbus_xfer(struct i2c_adapter * adapter, u16 addr, unsigned short flags,
                   char read_write, u8 command, int size, 
                   union i2c_smbus_data * data)
{
	s32 res;
	int swpec = 0;
	u8 partial = 0;

	flags &= I2C_M_TEN | I2C_CLIENT_PEC;
	if((flags & I2C_CLIENT_PEC) &&
	   !(i2c_check_functionality(adapter, I2C_FUNC_SMBUS_HWPEC_CALC))) {
		swpec = 1;
		if(read_write == I2C_SMBUS_READ &&
		   size == I2C_SMBUS_BLOCK_DATA)
			size = I2C_SMBUS_BLOCK_DATA_PEC;
		else if(size == I2C_SMBUS_PROC_CALL)
			size = I2C_SMBUS_PROC_CALL_PEC;
		else if(size == I2C_SMBUS_BLOCK_PROC_CALL) {
			i2c_smbus_add_pec(addr, command,
		                          I2C_SMBUS_BLOCK_DATA, data);
			partial = data->block[data->block[0] + 1];
			size = I2C_SMBUS_BLOCK_PROC_CALL_PEC;
		} else if(read_write == I2C_SMBUS_WRITE &&
		          size != I2C_SMBUS_QUICK &&
		          size != I2C_SMBUS_I2C_BLOCK_DATA)
			size = i2c_smbus_add_pec(addr, command, size, data);
	}

	if (adapter->algo->smbus_xfer) {
		down(&adapter->bus);
		res = adapter->algo->smbus_xfer(adapter,addr,flags,read_write,
		                                command,size,data);
		up(&adapter->bus);
	} else
		res = i2c_smbus_xfer_emulated(adapter,addr,flags,read_write,
	                                      command,size,data);

	if(res >= 0 && swpec &&
	   size != I2C_SMBUS_QUICK && size != I2C_SMBUS_I2C_BLOCK_DATA &&
	   (read_write == I2C_SMBUS_READ || size == I2C_SMBUS_PROC_CALL_PEC ||
	    size == I2C_SMBUS_BLOCK_PROC_CALL_PEC)) {
		if(i2c_smbus_check_pec(addr, command, size, partial, data))
			return -1;
	}
	return res;
}


/* You should always define `functionality'; the 'else' is just for
   backward compatibility. */ 
u32 i2c_get_functionality (struct i2c_adapter *adap)
{
	if (adap->algo->functionality)
		return adap->algo->functionality(adap);
	else
		return 0xffffffff;
}

int i2c_check_functionality (struct i2c_adapter *adap, u32 func)
{
	u32 adap_func = i2c_get_functionality (adap);
	return (func & adap_func) == func;
}

EXPORT_SYMBOL(i2c_add_adapter);
EXPORT_SYMBOL(i2c_del_adapter);
EXPORT_SYMBOL(i2c_add_driver);
EXPORT_SYMBOL(i2c_del_driver);
EXPORT_SYMBOL(i2c_attach_client);
EXPORT_SYMBOL(i2c_detach_client);
EXPORT_SYMBOL(i2c_use_client);
EXPORT_SYMBOL(i2c_release_client);
EXPORT_SYMBOL(i2c_check_addr);

EXPORT_SYMBOL(i2c_master_send);
EXPORT_SYMBOL(i2c_master_recv);
EXPORT_SYMBOL(i2c_control);
EXPORT_SYMBOL(i2c_transfer);
EXPORT_SYMBOL(i2c_adapter_id);
EXPORT_SYMBOL(i2c_probe);

EXPORT_SYMBOL(i2c_smbus_xfer);
EXPORT_SYMBOL(i2c_smbus_write_quick);
EXPORT_SYMBOL(i2c_smbus_read_byte);
EXPORT_SYMBOL(i2c_smbus_write_byte);
EXPORT_SYMBOL(i2c_smbus_read_byte_data);
EXPORT_SYMBOL(i2c_smbus_write_byte_data);
EXPORT_SYMBOL(i2c_smbus_read_word_data);
EXPORT_SYMBOL(i2c_smbus_write_word_data);
EXPORT_SYMBOL(i2c_smbus_process_call);
EXPORT_SYMBOL(i2c_smbus_read_block_data);
EXPORT_SYMBOL(i2c_smbus_write_block_data);
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data);
EXPORT_SYMBOL(i2c_smbus_write_i2c_block_data);

EXPORT_SYMBOL(i2c_get_functionality);
EXPORT_SYMBOL(i2c_check_functionality);

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus main module");
MODULE_LICENSE("GPL");

MODULE_PARM(i2c_debug, "i");
MODULE_PARM_DESC(i2c_debug,"debug level");
