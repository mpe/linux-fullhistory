/*
 * intf.c - class-specific interface management
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include "base.h"


#define to_intf(node) container_of(node,struct device_interface,kobj.entry)

/**
 * intf_dev_link - symlink from interface's directory to device's directory
 *
 */
static int intf_dev_link(struct intf_data * data)
{
	char	linkname[16];

	snprintf(linkname,16,"%u",data->intf_num);
	return sysfs_create_link(&data->intf->kobj,&data->dev->kobj,linkname);
}

static void intf_dev_unlink(struct intf_data * data)
{
	char	linkname[16];
	snprintf(linkname,16,"%u",data->intf_num);
	sysfs_remove_link(&data->intf->kobj,linkname);
}


int interface_register(struct device_interface * intf)
{
	struct device_class * cls = get_devclass(intf->devclass);
	int error = 0;

	if (cls) {
		pr_debug("register interface '%s' with class '%s'\n",
			 intf->name,cls->name);
		strncpy(intf->kobj.name,intf->name,KOBJ_NAME_LEN);
		intf->kobj.subsys = &cls->subsys;
		kobject_register(&intf->kobj);
	} else
		error = -EINVAL;
	return error;
}

void interface_unregister(struct device_interface * intf)
{
	pr_debug("unregistering interface '%s' from class '%s'\n",
		 intf->name,intf->devclass->name);
	kobject_unregister(&intf->kobj);
}

int interface_add(struct device_class * cls, struct device * dev)
{
	struct list_head * node;
	int error = 0;

	pr_debug("adding '%s' to %s class interfaces\n",dev->name,cls->name);

	list_for_each(node,&cls->subsys.list) {
		struct device_interface * intf = to_intf(node);
		if (intf->add_device) {
			error = intf->add_device(dev);
			if (error)
				pr_debug("%s:%s: adding '%s' failed: %d\n",
					 cls->name,intf->name,dev->name,error);
		}
		
	}
	return 0;
}

void interface_remove(struct device_class * cls, struct device * dev)
{
	struct list_head * node;
	struct list_head * next;

	pr_debug("remove '%s' from %s class interfaces: ",dev->name,cls->name);

	list_for_each_safe(node,next,&dev->intf_list) {
		struct intf_data * intf_data = container_of(node,struct intf_data,node);
		list_del_init(&intf_data->node);

		intf_dev_unlink(intf_data);
		pr_debug("%s ",intf_data->intf->name);
		if (intf_data->intf->remove_device)
			intf_data->intf->remove_device(intf_data);
	}
	pr_debug("\n");
}

int interface_add_data(struct intf_data * data)
{
	down_write(&data->intf->devclass->subsys.rwsem);
	list_add_tail(&data->node,&data->dev->intf_list);
	data->intf_num = data->intf->devnum++;
	intf_dev_link(data);
	up_write(&data->intf->devclass->subsys.rwsem);
	return 0;
}

EXPORT_SYMBOL(interface_register);
EXPORT_SYMBOL(interface_unregister);
