/* 
 *  FiberChannel transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  ========
 *
 *  Copyright (C) 2004-2005   James Smart, Emulex Corporation
 *    Rewrite for host, target, device, and remote port attributes,
 *    statistics, and service functions...
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include "scsi_priv.h"

#define FC_PRINTK(x, l, f, a...)	printk(l "scsi(%d:%d:%d:%d): " f, (x)->host->host_no, (x)->channel, (x)->id, (x)->lun , ##a)

/*
 * Redefine so that we can have same named attributes in the
 * sdev/starget/host objects.
 */
#define FC_CLASS_DEVICE_ATTR(_prefix,_name,_mode,_show,_store)		\
struct class_device_attribute class_device_attr_##_prefix##_##_name = 	\
	__ATTR(_name,_mode,_show,_store)

#define fc_enum_name_search(title, table_type, table)			\
static const char *get_fc_##title##_name(enum table_type table_key)	\
{									\
	int i;								\
	char *name = NULL;						\
									\
	for (i = 0; i < sizeof(table)/sizeof(table[0]); i++) {		\
		if (table[i].value == table_key) {			\
			name = table[i].name;				\
			break;						\
		}							\
	}								\
	return name;							\
}

#define fc_enum_name_match(title, table_type, table)			\
static int get_fc_##title##_match(const char *table_key,		\
		enum table_type *value)					\
{									\
	int i;								\
									\
	for (i = 0; i < sizeof(table)/sizeof(table[0]); i++) {		\
		if (strncmp(table_key, table[i].name,			\
				table[i].matchlen) == 0) {		\
			*value = table[i].value;			\
			return 0; /* success */				\
		}							\
	}								\
	return 1; /* failure */						\
}


/* Convert fc_port_type values to ascii string name */
static struct {
	enum fc_port_type	value;
	char			*name;
} fc_port_type_names[] = {
	{ FC_PORTTYPE_UNKNOWN,		"Unknown" },
	{ FC_PORTTYPE_OTHER,		"Other" },
	{ FC_PORTTYPE_NOTPRESENT,	"Not Present" },
	{ FC_PORTTYPE_NPORT,	"NPort (fabric via point-to-point)" },
	{ FC_PORTTYPE_NLPORT,	"NLPort (fabric via loop)" },
	{ FC_PORTTYPE_LPORT,	"LPort (private loop)" },
	{ FC_PORTTYPE_PTP,	"Point-To-Point (direct nport connection" },
};
fc_enum_name_search(port_type, fc_port_type, fc_port_type_names)
#define FC_PORTTYPE_MAX_NAMELEN		50


/* Convert fc_port_state values to ascii string name */
static struct {
	enum fc_port_state	value;
	char			*name;
} fc_port_state_names[] = {
	{ FC_PORTSTATE_UNKNOWN,		"Unknown" },
	{ FC_PORTSTATE_NOTPRESENT,	"Not Present" },
	{ FC_PORTSTATE_ONLINE,		"Online" },
	{ FC_PORTSTATE_OFFLINE,		"Offline" },
	{ FC_PORTSTATE_BLOCKED,		"Blocked" },
	{ FC_PORTSTATE_BYPASSED,	"Bypassed" },
	{ FC_PORTSTATE_DIAGNOSTICS,	"Diagnostics" },
	{ FC_PORTSTATE_LINKDOWN,	"Linkdown" },
	{ FC_PORTSTATE_ERROR,		"Error" },
	{ FC_PORTSTATE_LOOPBACK,	"Loopback" },
};
fc_enum_name_search(port_state, fc_port_state, fc_port_state_names)
#define FC_PORTSTATE_MAX_NAMELEN	20


/* Convert fc_tgtid_binding_type values to ascii string name */
static struct {
	enum fc_tgtid_binding_type	value;
	char				*name;
	int				matchlen;
} fc_tgtid_binding_type_names[] = {
	{ FC_TGTID_BIND_NONE, "none", 4 },
	{ FC_TGTID_BIND_BY_WWPN, "wwpn (World Wide Port Name)", 4 },
	{ FC_TGTID_BIND_BY_WWNN, "wwnn (World Wide Node Name)", 4 },
	{ FC_TGTID_BIND_BY_ID, "port_id (FC Address)", 7 },
};
fc_enum_name_search(tgtid_bind_type, fc_tgtid_binding_type,
		fc_tgtid_binding_type_names)
fc_enum_name_match(tgtid_bind_type, fc_tgtid_binding_type,
		fc_tgtid_binding_type_names)
#define FC_BINDTYPE_MAX_NAMELEN	30


#define fc_bitfield_name_search(title, table)			\
static ssize_t							\
get_fc_##title##_names(u32 table_key, char *buf)		\
{								\
	char *prefix = "";					\
	ssize_t len = 0;					\
	int i;							\
								\
	for (i = 0; i < sizeof(table)/sizeof(table[0]); i++) {	\
		if (table[i].value & table_key) {		\
			len += sprintf(buf + len, "%s%s",	\
				prefix, table[i].name);		\
			prefix = ", ";				\
		}						\
	}							\
	len += sprintf(buf + len, "\n");			\
	return len;						\
}


/* Convert FC_COS bit values to ascii string name */
static struct {
	u32 			value;
	char			*name;
} fc_cos_names[] = {
	{ FC_COS_CLASS1,	"Class 1" },
	{ FC_COS_CLASS2,	"Class 2" },
	{ FC_COS_CLASS3,	"Class 3" },
	{ FC_COS_CLASS4,	"Class 4" },
	{ FC_COS_CLASS6,	"Class 6" },
};
fc_bitfield_name_search(cos, fc_cos_names)


/* Convert FC_PORTSPEED bit values to ascii string name */
static struct {
	u32 			value;
	char			*name;
} fc_port_speed_names[] = {
	{ FC_PORTSPEED_1GBIT,		"1 Gbit" },
	{ FC_PORTSPEED_2GBIT,		"2 Gbit" },
	{ FC_PORTSPEED_4GBIT,		"4 Gbit" },
	{ FC_PORTSPEED_10GBIT,		"10 Gbit" },
	{ FC_PORTSPEED_NOT_NEGOTIATED,	"Not Negotiated" },
};
fc_bitfield_name_search(port_speed, fc_port_speed_names)


static int
show_fc_fc4s (char *buf, u8 *fc4_list)
{
	int i, len=0;

	for (i = 0; i < FC_FC4_LIST_SIZE; i++, fc4_list++)
		len += sprintf(buf + len , "0x%02x ", *fc4_list);
	len += sprintf(buf + len, "\n");
	return len;
}


/* Convert FC_RPORT_ROLE bit values to ascii string name */
static struct {
	u32 			value;
	char			*name;
} fc_remote_port_role_names[] = {
	{ FC_RPORT_ROLE_FCP_TARGET,	"FCP Target" },
	{ FC_RPORT_ROLE_FCP_INITIATOR,	"FCP Initiator" },
	{ FC_RPORT_ROLE_IP_PORT,	"IP Port" },
};
fc_bitfield_name_search(remote_port_roles, fc_remote_port_role_names)

/*
 * Define roles that are specific to port_id. Values are relative to ROLE_MASK.
 */
#define FC_WELLKNOWN_PORTID_MASK	0xfffff0
#define FC_WELLKNOWN_ROLE_MASK  	0x00000f
#define FC_FPORT_PORTID			0x00000e
#define FC_FABCTLR_PORTID		0x00000d
#define FC_DIRSRVR_PORTID		0x00000c
#define FC_TIMESRVR_PORTID		0x00000b
#define FC_MGMTSRVR_PORTID		0x00000a


static void fc_timeout_blocked_rport(void *data);
static void fc_scsi_scan_rport(void *data);
static void fc_rport_terminate(struct fc_rport  *rport);

/*
 * Attribute counts pre object type...
 * Increase these values if you add attributes
 */
#define FC_STARGET_NUM_ATTRS 	3
#define FC_RPORT_NUM_ATTRS	9
#define FC_HOST_NUM_ATTRS	15

struct fc_internal {
	struct scsi_transport_template t;
	struct fc_function_template *f;

	/*
	 * For attributes : each object has :
	 *   An array of the actual attributes structures
	 *   An array of null-terminated pointers to the attribute
	 *     structures - used for mid-layer interaction.
	 *
	 * The attribute containers for the starget and host are are
	 * part of the midlayer. As the remote port is specific to the
	 * fc transport, we must provide the attribute container.
	 */
	struct class_device_attribute private_starget_attrs[
							FC_STARGET_NUM_ATTRS];
	struct class_device_attribute *starget_attrs[FC_STARGET_NUM_ATTRS + 1];

	struct class_device_attribute private_host_attrs[FC_HOST_NUM_ATTRS];
	struct class_device_attribute *host_attrs[FC_HOST_NUM_ATTRS + 1];

	struct transport_container rport_attr_cont;
	struct class_device_attribute private_rport_attrs[FC_RPORT_NUM_ATTRS];
	struct class_device_attribute *rport_attrs[FC_RPORT_NUM_ATTRS + 1];
};

#define to_fc_internal(tmpl)	container_of(tmpl, struct fc_internal, t)

static int fc_target_setup(struct device *dev)
{
	struct scsi_target *starget = to_scsi_target(dev);
	struct fc_rport *rport = starget_to_rport(starget);

	/*
	 * if parent is remote port, use values from remote port.
	 * Otherwise, this host uses the fc_transport, but not the
	 * remote port interface. As such, initialize to known non-values.
	 */
	if (rport) {
		fc_starget_node_name(starget) = rport->node_name;
		fc_starget_port_name(starget) = rport->port_name;
		fc_starget_port_id(starget) = rport->port_id;
	} else {
		fc_starget_node_name(starget) = -1;
		fc_starget_port_name(starget) = -1;
		fc_starget_port_id(starget) = -1;
	}

	return 0;
}

static DECLARE_TRANSPORT_CLASS(fc_transport_class,
			       "fc_transport",
			       fc_target_setup,
			       NULL,
			       NULL);

static int fc_host_setup(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);

	/* 
	 * Set default values easily detected by the midlayer as
	 * failure cases.  The scsi lldd is responsible for initializing
	 * all transport attributes to valid values per host.
	 */
	fc_host_node_name(shost) = -1;
	fc_host_port_name(shost) = -1;
	fc_host_supported_classes(shost) = FC_COS_UNSPECIFIED;
	memset(fc_host_supported_fc4s(shost), 0,
		sizeof(fc_host_supported_fc4s(shost)));
	memset(fc_host_symbolic_name(shost), 0,
		sizeof(fc_host_symbolic_name(shost)));
	fc_host_supported_speeds(shost) = FC_PORTSPEED_UNKNOWN;
	fc_host_maxframe_size(shost) = -1;
	memset(fc_host_serial_number(shost), 0,
		sizeof(fc_host_serial_number(shost)));

	fc_host_port_id(shost) = -1;
	fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
	fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
	memset(fc_host_active_fc4s(shost), 0,
		sizeof(fc_host_active_fc4s(shost)));
	fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
	fc_host_fabric_name(shost) = -1;

	fc_host_tgtid_bind_type(shost) = FC_TGTID_BIND_BY_WWPN;

	INIT_LIST_HEAD(&fc_host_rports(shost));
	INIT_LIST_HEAD(&fc_host_rport_bindings(shost));
	fc_host_next_rport_number(shost) = 0;
	fc_host_next_target_id(shost) = 0;

	return 0;
}

static DECLARE_TRANSPORT_CLASS(fc_host_class,
			       "fc_host",
			       fc_host_setup,
			       NULL,
			       NULL);

/*
 * Setup and Remove actions for remote ports are handled
 * in the service functions below.
 */
static DECLARE_TRANSPORT_CLASS(fc_rport_class,
			       "fc_remote_ports",
			       NULL,
			       NULL,
			       NULL);

/*
 * Module Parameters
 */

/*
 * dev_loss_tmo: the default number of seconds that the FC transport
 *   should insulate the loss of a remote port.
 *   The maximum will be capped by the value of SCSI_DEVICE_BLOCK_MAX_TIMEOUT.
 */
static unsigned int fc_dev_loss_tmo = SCSI_DEVICE_BLOCK_MAX_TIMEOUT;

module_param_named(dev_loss_tmo, fc_dev_loss_tmo, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dev_loss_tmo,
		 "Maximum number of seconds that the FC transport should"
		 " insulate the loss of a remote port. Once this value is"
		 " exceeded, the scsi target is removed. Value should be"
		 " between 1 and SCSI_DEVICE_BLOCK_MAX_TIMEOUT.");


static __init int fc_transport_init(void)
{
	int error = transport_class_register(&fc_host_class);
	if (error)
		return error;
	error = transport_class_register(&fc_rport_class);
	if (error)
		return error;
	return transport_class_register(&fc_transport_class);
}

static void __exit fc_transport_exit(void)
{
	transport_class_unregister(&fc_transport_class);
	transport_class_unregister(&fc_rport_class);
	transport_class_unregister(&fc_host_class);
}

/*
 * FC Remote Port Attribute Management
 */

#define fc_rport_show_function(field, format_string, sz, cast)		\
static ssize_t								\
show_fc_rport_##field (struct class_device *cdev, char *buf)		\
{									\
	struct fc_rport *rport = transport_class_to_rport(cdev);	\
	struct Scsi_Host *shost = rport_to_shost(rport);		\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	if (i->f->get_rport_##field)					\
		i->f->get_rport_##field(rport);				\
	return snprintf(buf, sz, format_string, cast rport->field); 	\
}

#define fc_rport_store_function(field)					\
static ssize_t								\
store_fc_rport_##field(struct class_device *cdev, const char *buf,	\
			   size_t count)				\
{									\
	int val;							\
	struct fc_rport *rport = transport_class_to_rport(cdev);	\
	struct Scsi_Host *shost = rport_to_shost(rport);		\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_rport_##field(rport, val);				\
	return count;							\
}

#define fc_rport_rd_attr(field, format_string, sz)			\
	fc_rport_show_function(field, format_string, sz, )		\
static FC_CLASS_DEVICE_ATTR(rport, field, S_IRUGO,			\
			 show_fc_rport_##field, NULL)

#define fc_rport_rd_attr_cast(field, format_string, sz, cast)		\
	fc_rport_show_function(field, format_string, sz, (cast))	\
static FC_CLASS_DEVICE_ATTR(rport, field, S_IRUGO,			\
			  show_fc_rport_##field, NULL)

#define fc_rport_rw_attr(field, format_string, sz)			\
	fc_rport_show_function(field, format_string, sz, )		\
	fc_rport_store_function(field)					\
static FC_CLASS_DEVICE_ATTR(rport, field, S_IRUGO | S_IWUSR,		\
			show_fc_rport_##field,				\
			store_fc_rport_##field)


#define fc_private_rport_show_function(field, format_string, sz, cast)	\
static ssize_t								\
show_fc_rport_##field (struct class_device *cdev, char *buf)		\
{									\
	struct fc_rport *rport = transport_class_to_rport(cdev);	\
	return snprintf(buf, sz, format_string, cast rport->field); 	\
}

#define fc_private_rport_rd_attr(field, format_string, sz)		\
	fc_private_rport_show_function(field, format_string, sz, )	\
static FC_CLASS_DEVICE_ATTR(rport, field, S_IRUGO,			\
			 show_fc_rport_##field, NULL)

#define fc_private_rport_rd_attr_cast(field, format_string, sz, cast)	\
	fc_private_rport_show_function(field, format_string, sz, (cast)) \
static FC_CLASS_DEVICE_ATTR(rport, field, S_IRUGO,			\
			  show_fc_rport_##field, NULL)


#define fc_private_rport_rd_enum_attr(title, maxlen)			\
static ssize_t								\
show_fc_rport_##title (struct class_device *cdev, char *buf)		\
{									\
	struct fc_rport *rport = transport_class_to_rport(cdev);	\
	const char *name;						\
	name = get_fc_##title##_name(rport->title);			\
	if (!name)							\
		return -EINVAL;						\
	return snprintf(buf, maxlen, "%s\n", name);			\
}									\
static FC_CLASS_DEVICE_ATTR(rport, title, S_IRUGO,			\
			show_fc_rport_##title, NULL)


#define SETUP_RPORT_ATTRIBUTE_RD(field)					\
	i->private_rport_attrs[count] = class_device_attr_rport_##field; \
	i->private_rport_attrs[count].attr.mode = S_IRUGO;		\
	i->private_rport_attrs[count].store = NULL;			\
	i->rport_attrs[count] = &i->private_rport_attrs[count];		\
	if (i->f->show_rport_##field)					\
		count++

#define SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(field)				\
	i->private_rport_attrs[count] = class_device_attr_rport_##field; \
	i->private_rport_attrs[count].attr.mode = S_IRUGO;		\
	i->private_rport_attrs[count].store = NULL;			\
	i->rport_attrs[count] = &i->private_rport_attrs[count];		\
	count++

#define SETUP_RPORT_ATTRIBUTE_RW(field)					\
	i->private_rport_attrs[count] = class_device_attr_rport_##field; \
	if (!i->f->set_rport_##field) {					\
		i->private_rport_attrs[count].attr.mode = S_IRUGO;	\
		i->private_rport_attrs[count].store = NULL;		\
	}								\
	i->rport_attrs[count] = &i->private_rport_attrs[count];		\
	if (i->f->show_rport_##field)					\
		count++


/* The FC Transport Remote Port Attributes: */

/* Fixed Remote Port Attributes */

fc_private_rport_rd_attr(maxframe_size, "%u bytes\n", 20);

static ssize_t
show_fc_rport_supported_classes (struct class_device *cdev, char *buf)
{
	struct fc_rport *rport = transport_class_to_rport(cdev);
	if (rport->supported_classes == FC_COS_UNSPECIFIED)
		return snprintf(buf, 20, "unspecified\n");
	return get_fc_cos_names(rport->supported_classes, buf);
}
static FC_CLASS_DEVICE_ATTR(rport, supported_classes, S_IRUGO,
		show_fc_rport_supported_classes, NULL);

/* Dynamic Remote Port Attributes */

fc_rport_rw_attr(dev_loss_tmo, "%d\n", 20);


/* Private Remote Port Attributes */

fc_private_rport_rd_attr_cast(node_name, "0x%llx\n", 20, unsigned long long);
fc_private_rport_rd_attr_cast(port_name, "0x%llx\n", 20, unsigned long long);
fc_private_rport_rd_attr(port_id, "0x%06x\n", 20);

static ssize_t
show_fc_rport_roles (struct class_device *cdev, char *buf)
{
	struct fc_rport *rport = transport_class_to_rport(cdev);

	/* identify any roles that are port_id specific */
	if ((rport->port_id != -1) &&
	    (rport->port_id & FC_WELLKNOWN_PORTID_MASK) ==
					FC_WELLKNOWN_PORTID_MASK) {
		switch (rport->port_id & FC_WELLKNOWN_ROLE_MASK) {
		case FC_FPORT_PORTID:
			return snprintf(buf, 30, "Fabric Port\n");
		case FC_FABCTLR_PORTID:
			return snprintf(buf, 30, "Fabric Controller\n");
		case FC_DIRSRVR_PORTID:
			return snprintf(buf, 30, "Directory Server\n");
		case FC_TIMESRVR_PORTID:
			return snprintf(buf, 30, "Time Server\n");
		case FC_MGMTSRVR_PORTID:
			return snprintf(buf, 30, "Management Server\n");
		default:
			return snprintf(buf, 30, "Unknown Fabric Entity\n");
		}
	} else {
		if (rport->roles == FC_RPORT_ROLE_UNKNOWN)
			return snprintf(buf, 20, "unknown\n");
		return get_fc_remote_port_roles_names(rport->roles, buf);
	}
}
static FC_CLASS_DEVICE_ATTR(rport, roles, S_IRUGO,
		show_fc_rport_roles, NULL);

fc_private_rport_rd_enum_attr(port_state, FC_PORTSTATE_MAX_NAMELEN);
fc_private_rport_rd_attr(scsi_target_id, "%d\n", 20);



/*
 * FC SCSI Target Attribute Management
 */

/*
 * Note: in the target show function we recognize when the remote
 *  port is in the heirarchy and do not allow the driver to get
 *  involved in sysfs functions. The driver only gets involved if
 *  it's the "old" style that doesn't use rports.
 */
#define fc_starget_show_function(field, format_string, sz, cast)	\
static ssize_t								\
show_fc_starget_##field (struct class_device *cdev, char *buf)		\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	struct fc_rport *rport = starget_to_rport(starget);		\
	if (rport)							\
		fc_starget_##field(starget) = rport->field;		\
	else if (i->f->get_starget_##field)				\
		i->f->get_starget_##field(starget);			\
	return snprintf(buf, sz, format_string, 			\
		cast fc_starget_##field(starget)); 			\
}

#define fc_starget_rd_attr(field, format_string, sz)			\
	fc_starget_show_function(field, format_string, sz, )		\
static FC_CLASS_DEVICE_ATTR(starget, field, S_IRUGO,			\
			 show_fc_starget_##field, NULL)

#define fc_starget_rd_attr_cast(field, format_string, sz, cast)		\
	fc_starget_show_function(field, format_string, sz, (cast))	\
static FC_CLASS_DEVICE_ATTR(starget, field, S_IRUGO,			\
			  show_fc_starget_##field, NULL)

#define SETUP_STARGET_ATTRIBUTE_RD(field)				\
	i->private_starget_attrs[count] = class_device_attr_starget_##field; \
	i->private_starget_attrs[count].attr.mode = S_IRUGO;		\
	i->private_starget_attrs[count].store = NULL;			\
	i->starget_attrs[count] = &i->private_starget_attrs[count];	\
	if (i->f->show_starget_##field)					\
		count++

#define SETUP_STARGET_ATTRIBUTE_RW(field)				\
	i->private_starget_attrs[count] = class_device_attr_starget_##field; \
	if (!i->f->set_starget_##field) {				\
		i->private_starget_attrs[count].attr.mode = S_IRUGO;	\
		i->private_starget_attrs[count].store = NULL;		\
	}								\
	i->starget_attrs[count] = &i->private_starget_attrs[count];	\
	if (i->f->show_starget_##field)					\
		count++

/* The FC Transport SCSI Target Attributes: */
fc_starget_rd_attr_cast(node_name, "0x%llx\n", 20, unsigned long long);
fc_starget_rd_attr_cast(port_name, "0x%llx\n", 20, unsigned long long);
fc_starget_rd_attr(port_id, "0x%06x\n", 20);


/*
 * Host Attribute Management
 */

#define fc_host_show_function(field, format_string, sz, cast)		\
static ssize_t								\
show_fc_host_##field (struct class_device *cdev, char *buf)		\
{									\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	if (i->f->get_host_##field)					\
		i->f->get_host_##field(shost);				\
	return snprintf(buf, sz, format_string, cast fc_host_##field(shost)); \
}

#define fc_host_store_function(field)					\
static ssize_t								\
store_fc_host_##field(struct class_device *cdev, const char *buf,	\
			   size_t count)				\
{									\
	int val;							\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_host_##field(shost, val);				\
	return count;							\
}

#define fc_host_rd_attr(field, format_string, sz)			\
	fc_host_show_function(field, format_string, sz, )		\
static FC_CLASS_DEVICE_ATTR(host, field, S_IRUGO,			\
			 show_fc_host_##field, NULL)

#define fc_host_rd_attr_cast(field, format_string, sz, cast)		\
	fc_host_show_function(field, format_string, sz, (cast))		\
static FC_CLASS_DEVICE_ATTR(host, field, S_IRUGO,			\
			  show_fc_host_##field, NULL)

#define fc_host_rw_attr(field, format_string, sz)			\
	fc_host_show_function(field, format_string, sz, )		\
	fc_host_store_function(field)					\
static FC_CLASS_DEVICE_ATTR(host, field, S_IRUGO | S_IWUSR,		\
			show_fc_host_##field,				\
			store_fc_host_##field)

#define fc_host_rd_enum_attr(title, maxlen)				\
static ssize_t								\
show_fc_host_##title (struct class_device *cdev, char *buf)		\
{									\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	const char *name;						\
	if (i->f->get_host_##title)					\
		i->f->get_host_##title(shost);				\
	name = get_fc_##title##_name(fc_host_##title(shost));		\
	if (!name)							\
		return -EINVAL;						\
	return snprintf(buf, maxlen, "%s\n", name);			\
}									\
static FC_CLASS_DEVICE_ATTR(host, title, S_IRUGO, show_fc_host_##title, NULL)

#define SETUP_HOST_ATTRIBUTE_RD(field)					\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	i->private_host_attrs[count].attr.mode = S_IRUGO;		\
	i->private_host_attrs[count].store = NULL;			\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	if (i->f->show_host_##field)					\
		count++

#define SETUP_HOST_ATTRIBUTE_RW(field)					\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	if (!i->f->set_host_##field) {					\
		i->private_host_attrs[count].attr.mode = S_IRUGO;	\
		i->private_host_attrs[count].store = NULL;		\
	}								\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	if (i->f->show_host_##field)					\
		count++


#define fc_private_host_show_function(field, format_string, sz, cast)	\
static ssize_t								\
show_fc_host_##field (struct class_device *cdev, char *buf)		\
{									\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	return snprintf(buf, sz, format_string, cast fc_host_##field(shost)); \
}

#define fc_private_host_rd_attr(field, format_string, sz)		\
	fc_private_host_show_function(field, format_string, sz, )	\
static FC_CLASS_DEVICE_ATTR(host, field, S_IRUGO,			\
			 show_fc_host_##field, NULL)

#define fc_private_host_rd_attr_cast(field, format_string, sz, cast)	\
	fc_private_host_show_function(field, format_string, sz, (cast)) \
static FC_CLASS_DEVICE_ATTR(host, field, S_IRUGO,			\
			  show_fc_host_##field, NULL)

#define SETUP_PRIVATE_HOST_ATTRIBUTE_RD(field)			\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	i->private_host_attrs[count].attr.mode = S_IRUGO;		\
	i->private_host_attrs[count].store = NULL;			\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	count++

#define SETUP_PRIVATE_HOST_ATTRIBUTE_RW(field)			\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	count++


/* Fixed Host Attributes */

static ssize_t
show_fc_host_supported_classes (struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);

	if (fc_host_supported_classes(shost) == FC_COS_UNSPECIFIED)
		return snprintf(buf, 20, "unspecified\n");

	return get_fc_cos_names(fc_host_supported_classes(shost), buf);
}
static FC_CLASS_DEVICE_ATTR(host, supported_classes, S_IRUGO,
		show_fc_host_supported_classes, NULL);

static ssize_t
show_fc_host_supported_fc4s (struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	return (ssize_t)show_fc_fc4s(buf, fc_host_supported_fc4s(shost));
}
static FC_CLASS_DEVICE_ATTR(host, supported_fc4s, S_IRUGO,
		show_fc_host_supported_fc4s, NULL);

static ssize_t
show_fc_host_supported_speeds (struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);

	if (fc_host_supported_speeds(shost) == FC_PORTSPEED_UNKNOWN)
		return snprintf(buf, 20, "unknown\n");

	return get_fc_port_speed_names(fc_host_supported_speeds(shost), buf);
}
static FC_CLASS_DEVICE_ATTR(host, supported_speeds, S_IRUGO,
		show_fc_host_supported_speeds, NULL);


fc_private_host_rd_attr_cast(node_name, "0x%llx\n", 20, unsigned long long);
fc_private_host_rd_attr_cast(port_name, "0x%llx\n", 20, unsigned long long);
fc_private_host_rd_attr(symbolic_name, "%s\n", (FC_SYMBOLIC_NAME_SIZE +1));
fc_private_host_rd_attr(maxframe_size, "%u bytes\n", 20);
fc_private_host_rd_attr(serial_number, "%s\n", (FC_SERIAL_NUMBER_SIZE +1));


/* Dynamic Host Attributes */

static ssize_t
show_fc_host_active_fc4s (struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct fc_internal *i = to_fc_internal(shost->transportt);

	if (i->f->get_host_active_fc4s)
		i->f->get_host_active_fc4s(shost);

	return (ssize_t)show_fc_fc4s(buf, fc_host_active_fc4s(shost));
}
static FC_CLASS_DEVICE_ATTR(host, active_fc4s, S_IRUGO,
		show_fc_host_active_fc4s, NULL);

static ssize_t
show_fc_host_speed (struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct fc_internal *i = to_fc_internal(shost->transportt);

	if (i->f->get_host_speed)
		i->f->get_host_speed(shost);

	if (fc_host_speed(shost) == FC_PORTSPEED_UNKNOWN)
		return snprintf(buf, 20, "unknown\n");

	return get_fc_port_speed_names(fc_host_speed(shost), buf);
}
static FC_CLASS_DEVICE_ATTR(host, speed, S_IRUGO,
		show_fc_host_speed, NULL);


fc_host_rd_attr(port_id, "0x%06x\n", 20);
fc_host_rd_enum_attr(port_type, FC_PORTTYPE_MAX_NAMELEN);
fc_host_rd_enum_attr(port_state, FC_PORTSTATE_MAX_NAMELEN);
fc_host_rd_attr_cast(fabric_name, "0x%llx\n", 20, unsigned long long);


/* Private Host Attributes */

static ssize_t
show_fc_private_host_tgtid_bind_type(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	const char *name;

	name = get_fc_tgtid_bind_type_name(fc_host_tgtid_bind_type(shost));
	if (!name)
		return -EINVAL;
	return snprintf(buf, FC_BINDTYPE_MAX_NAMELEN, "%s\n", name);
}

static ssize_t
store_fc_private_host_tgtid_bind_type(struct class_device *cdev,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct fc_rport *rport, *next_rport;
 	enum fc_tgtid_binding_type val;
	unsigned long flags;

	if (get_fc_tgtid_bind_type_match(buf, &val))
		return -EINVAL;

	/* if changing bind type, purge all unused consistent bindings */
	if (val != fc_host_tgtid_bind_type(shost)) {
		spin_lock_irqsave(shost->host_lock, flags);
		list_for_each_entry_safe(rport, next_rport,
				&fc_host_rport_bindings(shost), peers)
			fc_rport_terminate(rport);
		spin_unlock_irqrestore(shost->host_lock, flags);
	}

	fc_host_tgtid_bind_type(shost) = val;
	return count;
}

static FC_CLASS_DEVICE_ATTR(host, tgtid_bind_type, S_IRUGO | S_IWUSR,
			show_fc_private_host_tgtid_bind_type,
			store_fc_private_host_tgtid_bind_type);

/*
 * Host Statistics Management
 */

/* Show a given an attribute in the statistics group */
static ssize_t
fc_stat_show(const struct class_device *cdev, char *buf, unsigned long offset)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct fc_internal *i = to_fc_internal(shost->transportt);
	struct fc_host_statistics *stats;
	ssize_t ret = -ENOENT;

	if (offset > sizeof(struct fc_host_statistics) ||
	    offset % sizeof(u64) != 0)
		WARN_ON(1);

	if (i->f->get_fc_host_stats) {
		stats = (i->f->get_fc_host_stats)(shost);
		if (stats)
			ret = snprintf(buf, 20, "0x%llx\n",
			      (unsigned long long)*(u64 *)(((u8 *) stats) + offset));
	}
	return ret;
}


/* generate a read-only statistics attribute */
#define fc_host_statistic(name)						\
static ssize_t show_fcstat_##name(struct class_device *cd, char *buf) 	\
{									\
	return fc_stat_show(cd, buf, 					\
			    offsetof(struct fc_host_statistics, name));	\
}									\
static FC_CLASS_DEVICE_ATTR(host, name, S_IRUGO, show_fcstat_##name, NULL)

fc_host_statistic(seconds_since_last_reset);
fc_host_statistic(tx_frames);
fc_host_statistic(tx_words);
fc_host_statistic(rx_frames);
fc_host_statistic(rx_words);
fc_host_statistic(lip_count);
fc_host_statistic(nos_count);
fc_host_statistic(error_frames);
fc_host_statistic(dumped_frames);
fc_host_statistic(link_failure_count);
fc_host_statistic(loss_of_sync_count);
fc_host_statistic(loss_of_signal_count);
fc_host_statistic(prim_seq_protocol_err_count);
fc_host_statistic(invalid_tx_word_count);
fc_host_statistic(invalid_crc_count);
fc_host_statistic(fcp_input_requests);
fc_host_statistic(fcp_output_requests);
fc_host_statistic(fcp_control_requests);
fc_host_statistic(fcp_input_megabytes);
fc_host_statistic(fcp_output_megabytes);

static ssize_t
fc_reset_statistics(struct class_device *cdev, const char *buf,
			   size_t count)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct fc_internal *i = to_fc_internal(shost->transportt);

	/* ignore any data value written to the attribute */
	if (i->f->reset_fc_host_stats) {
		i->f->reset_fc_host_stats(shost);
		return count;
	}

	return -ENOENT;
}
static FC_CLASS_DEVICE_ATTR(host, reset_statistics, S_IWUSR, NULL,
				fc_reset_statistics);


static struct attribute *fc_statistics_attrs[] = {
	&class_device_attr_host_seconds_since_last_reset.attr,
	&class_device_attr_host_tx_frames.attr,
	&class_device_attr_host_tx_words.attr,
	&class_device_attr_host_rx_frames.attr,
	&class_device_attr_host_rx_words.attr,
	&class_device_attr_host_lip_count.attr,
	&class_device_attr_host_nos_count.attr,
	&class_device_attr_host_error_frames.attr,
	&class_device_attr_host_dumped_frames.attr,
	&class_device_attr_host_link_failure_count.attr,
	&class_device_attr_host_loss_of_sync_count.attr,
	&class_device_attr_host_loss_of_signal_count.attr,
	&class_device_attr_host_prim_seq_protocol_err_count.attr,
	&class_device_attr_host_invalid_tx_word_count.attr,
	&class_device_attr_host_invalid_crc_count.attr,
	&class_device_attr_host_fcp_input_requests.attr,
	&class_device_attr_host_fcp_output_requests.attr,
	&class_device_attr_host_fcp_control_requests.attr,
	&class_device_attr_host_fcp_input_megabytes.attr,
	&class_device_attr_host_fcp_output_megabytes.attr,
	&class_device_attr_host_reset_statistics.attr,
	NULL
};

static struct attribute_group fc_statistics_group = {
	.name = "statistics",
	.attrs = fc_statistics_attrs,
};

static int fc_host_match(struct attribute_container *cont,
			  struct device *dev)
{
	struct Scsi_Host *shost;
	struct fc_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &fc_host_class.class)
		return 0;

	i = to_fc_internal(shost->transportt);

	return &i->t.host_attrs.ac == cont;
}

static int fc_target_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct fc_internal *i;

	if (!scsi_is_target_device(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &fc_host_class.class)
		return 0;

	i = to_fc_internal(shost->transportt);

	return &i->t.target_attrs.ac == cont;
}

static void fc_rport_dev_release(struct device *dev)
{
	struct fc_rport *rport = dev_to_rport(dev);
	put_device(dev->parent);
	kfree(rport);
}

int scsi_is_fc_rport(const struct device *dev)
{
	return dev->release == fc_rport_dev_release;
}
EXPORT_SYMBOL(scsi_is_fc_rport);

static int fc_rport_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct fc_internal *i;

	if (!scsi_is_fc_rport(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &fc_host_class.class)
		return 0;

	i = to_fc_internal(shost->transportt);

	return &i->rport_attr_cont.ac == cont;
}

struct scsi_transport_template *
fc_attach_transport(struct fc_function_template *ft)
{
	struct fc_internal *i = kmalloc(sizeof(struct fc_internal),
					GFP_KERNEL);
	int count;

	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct fc_internal));

	i->t.target_attrs.ac.attrs = &i->starget_attrs[0];
	i->t.target_attrs.ac.class = &fc_transport_class.class;
	i->t.target_attrs.ac.match = fc_target_match;
	i->t.target_size = sizeof(struct fc_starget_attrs);
	transport_container_register(&i->t.target_attrs);

	i->t.host_attrs.ac.attrs = &i->host_attrs[0];
	i->t.host_attrs.ac.class = &fc_host_class.class;
	i->t.host_attrs.ac.match = fc_host_match;
	i->t.host_size = sizeof(struct fc_host_attrs);
	if (ft->get_fc_host_stats)
		i->t.host_attrs.statistics = &fc_statistics_group;
	transport_container_register(&i->t.host_attrs);

	i->rport_attr_cont.ac.attrs = &i->rport_attrs[0];
	i->rport_attr_cont.ac.class = &fc_rport_class.class;
	i->rport_attr_cont.ac.match = fc_rport_match;
	transport_container_register(&i->rport_attr_cont);

	i->f = ft;

	/* Transport uses the shost workq for scsi scanning */
	i->t.create_work_queue = 1;
	
	/*
	 * Setup SCSI Target Attributes.
	 */
	count = 0;
	SETUP_STARGET_ATTRIBUTE_RD(node_name);
	SETUP_STARGET_ATTRIBUTE_RD(port_name);
	SETUP_STARGET_ATTRIBUTE_RD(port_id);

	BUG_ON(count > FC_STARGET_NUM_ATTRS);

	i->starget_attrs[count] = NULL;


	/*
	 * Setup SCSI Host Attributes.
	 */
	count=0;
	SETUP_HOST_ATTRIBUTE_RD(node_name);
	SETUP_HOST_ATTRIBUTE_RD(port_name);
	SETUP_HOST_ATTRIBUTE_RD(supported_classes);
	SETUP_HOST_ATTRIBUTE_RD(supported_fc4s);
	SETUP_HOST_ATTRIBUTE_RD(symbolic_name);
	SETUP_HOST_ATTRIBUTE_RD(supported_speeds);
	SETUP_HOST_ATTRIBUTE_RD(maxframe_size);
	SETUP_HOST_ATTRIBUTE_RD(serial_number);

	SETUP_HOST_ATTRIBUTE_RD(port_id);
	SETUP_HOST_ATTRIBUTE_RD(port_type);
	SETUP_HOST_ATTRIBUTE_RD(port_state);
	SETUP_HOST_ATTRIBUTE_RD(active_fc4s);
	SETUP_HOST_ATTRIBUTE_RD(speed);
	SETUP_HOST_ATTRIBUTE_RD(fabric_name);

	/* Transport-managed attributes */
	SETUP_PRIVATE_HOST_ATTRIBUTE_RW(tgtid_bind_type);

	BUG_ON(count > FC_HOST_NUM_ATTRS);

	i->host_attrs[count] = NULL;

	/*
	 * Setup Remote Port Attributes.
	 */
	count=0;
	SETUP_RPORT_ATTRIBUTE_RD(maxframe_size);
	SETUP_RPORT_ATTRIBUTE_RD(supported_classes);
	SETUP_RPORT_ATTRIBUTE_RW(dev_loss_tmo);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(node_name);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(port_name);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(port_id);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(roles);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(port_state);
	SETUP_PRIVATE_RPORT_ATTRIBUTE_RD(scsi_target_id);

	BUG_ON(count > FC_RPORT_NUM_ATTRS);

	i->rport_attrs[count] = NULL;

	return &i->t;
}
EXPORT_SYMBOL(fc_attach_transport);

void fc_release_transport(struct scsi_transport_template *t)
{
	struct fc_internal *i = to_fc_internal(t);

	transport_container_unregister(&i->t.target_attrs);
	transport_container_unregister(&i->t.host_attrs);
	transport_container_unregister(&i->rport_attr_cont);

	kfree(i);
}
EXPORT_SYMBOL(fc_release_transport);


/**
 * fc_remove_host - called to terminate any fc_transport-related elements
 *                  for a scsi host.
 * @rport:	remote port to be unblocked.
 *
 * This routine is expected to be called immediately preceeding the
 * a driver's call to scsi_remove_host().
 *
 * WARNING: A driver utilizing the fc_transport, which fails to call
 *   this routine prior to scsi_remote_host(), will leave dangling
 *   objects in /sys/class/fc_remote_ports. Access to any of these
 *   objects can result in a system crash !!!
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
void
fc_remove_host(struct Scsi_Host *shost)
{
	struct fc_rport *rport, *next_rport;

	/* Remove any remote ports */
	list_for_each_entry_safe(rport, next_rport,
			&fc_host_rports(shost), peers)
		fc_rport_terminate(rport);
	list_for_each_entry_safe(rport, next_rport,
			&fc_host_rport_bindings(shost), peers)
		fc_rport_terminate(rport);
}
EXPORT_SYMBOL(fc_remove_host);

/**
 * fc_rport_create - allocates and creates a remote FC port.
 * @shost:	scsi host the remote port is connected to.
 * @channel:	Channel on shost port connected to.
 * @ids:	The world wide names, fc address, and FC4 port
 *		roles for the remote port.
 *
 * Allocates and creates the remoter port structure, including the
 * class and sysfs creation.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
struct fc_rport *
fc_rport_create(struct Scsi_Host *shost, int channel,
	struct fc_rport_identifiers  *ids)
{
	struct fc_host_attrs *fc_host =
			(struct fc_host_attrs *)shost->shost_data;
	struct fc_internal *fci = to_fc_internal(shost->transportt);
	struct fc_rport *rport;
	struct device *dev;
	unsigned long flags;
	int error;
	size_t size;

	size = (sizeof(struct fc_rport) + fci->f->dd_fcrport_size);
	rport = kmalloc(size, GFP_KERNEL);
	if (unlikely(!rport)) {
		printk(KERN_ERR "%s: allocation failure\n", __FUNCTION__);
		return NULL;
	}
	memset(rport, 0, size);

	rport->maxframe_size = -1;
	rport->supported_classes = FC_COS_UNSPECIFIED;
	rport->dev_loss_tmo = fc_dev_loss_tmo;
	memcpy(&rport->node_name, &ids->node_name, sizeof(rport->node_name));
	memcpy(&rport->port_name, &ids->port_name, sizeof(rport->port_name));
	rport->port_id = ids->port_id;
	rport->roles = ids->roles;
	rport->port_state = FC_PORTSTATE_ONLINE;
	if (fci->f->dd_fcrport_size)
		rport->dd_data = &rport[1];
	rport->channel = channel;

	INIT_WORK(&rport->dev_loss_work, fc_timeout_blocked_rport, rport);
	INIT_WORK(&rport->scan_work, fc_scsi_scan_rport, rport);

	spin_lock_irqsave(shost->host_lock, flags);

	rport->number = fc_host->next_rport_number++;
	if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
		rport->scsi_target_id = fc_host->next_target_id++;
	else
		rport->scsi_target_id = -1;
	list_add_tail(&rport->peers, &fc_host_rports(shost));
	get_device(&shost->shost_gendev);

	spin_unlock_irqrestore(shost->host_lock, flags);

	dev = &rport->dev;
	device_initialize(dev);
	dev->parent = get_device(&shost->shost_gendev);
	dev->release = fc_rport_dev_release;
	sprintf(dev->bus_id, "rport-%d:%d-%d",
		shost->host_no, channel, rport->number);
	transport_setup_device(dev);

	error = device_add(dev);
	if (error) {
		printk(KERN_ERR "FC Remote Port device_add failed\n");
		goto delete_rport;
	}
	transport_add_device(dev);
	transport_configure_device(dev);

	if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
		/* initiate a scan of the target */
		scsi_queue_work(shost, &rport->scan_work);

	return rport;

delete_rport:
	transport_destroy_device(dev);
	put_device(dev->parent);
	spin_lock_irqsave(shost->host_lock, flags);
	list_del(&rport->peers);
	put_device(&shost->shost_gendev);
	spin_unlock_irqrestore(shost->host_lock, flags);
	put_device(dev->parent);
	kfree(rport);
	return NULL;
}

/**
 * fc_remote_port_add - notifies the fc transport of the existence
 *		of a remote FC port.
 * @shost:	scsi host the remote port is connected to.
 * @channel:	Channel on shost port connected to.
 * @ids:	The world wide names, fc address, and FC4 port
 *		roles for the remote port.
 *
 * The LLDD calls this routine to notify the transport of the existence
 * of a remote port. The LLDD provides the unique identifiers (wwpn,wwn)
 * of the port, it's FC address (port_id), and the FC4 roles that are
 * active for the port.
 *
 * For ports that are FCP targets (aka scsi targets), the FC transport
 * maintains consistent target id bindings on behalf of the LLDD.
 * A consistent target id binding is an assignment of a target id to
 * a remote port identifier, which persists while the scsi host is
 * attached. The remote port can disappear, then later reappear, and
 * it's target id assignment remains the same. This allows for shifts
 * in FC addressing (if binding by wwpn or wwnn) with no apparent
 * changes to the scsi subsystem which is based on scsi host number and
 * target id values.  Bindings are only valid during the attachment of
 * the scsi host. If the host detaches, then later re-attaches, target
 * id bindings may change.
 *
 * This routine is responsible for returning a remote port structure.
 * The routine will search the list of remote ports it maintains
 * internally on behalf of consistent target id mappings. If found, the
 * remote port structure will be reused. Otherwise, a new remote port
 * structure will be allocated.
 *
 * Whenever a remote port is allocated, a new fc_remote_port class
 * device is created.
 *
 * Should not be called from interrupt context.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
struct fc_rport *
fc_remote_port_add(struct Scsi_Host *shost, int channel,
	struct fc_rport_identifiers  *ids)
{
	struct fc_rport *rport;
	unsigned long flags;
	int match = 0;

	if (likely((ids->roles & FC_RPORT_ROLE_FCP_TARGET) &&
		(fc_host_tgtid_bind_type(shost) != FC_TGTID_BIND_NONE))) {

		/* search for a matching consistent binding */

		spin_lock_irqsave(shost->host_lock, flags);

		list_for_each_entry(rport, &fc_host_rport_bindings(shost),
					peers) {
			if (rport->channel != channel)
				continue;

			switch (fc_host_tgtid_bind_type(shost)) {
			case FC_TGTID_BIND_BY_WWPN:
				if (rport->port_name == ids->port_name)
					match = 1;
				break;
			case FC_TGTID_BIND_BY_WWNN:
				if (rport->node_name == ids->node_name)
					match = 1;
				break;
			case FC_TGTID_BIND_BY_ID:
				if (rport->port_id == ids->port_id)
					match = 1;
				break;
			case FC_TGTID_BIND_NONE: /* to keep compiler happy */
				break;
			}

			if (match) {
				list_move_tail(&rport->peers,
					&fc_host_rports(shost));
				break;
			}
		}

		spin_unlock_irqrestore(shost->host_lock, flags);

		if (match) {
			memcpy(&rport->node_name, &ids->node_name,
				sizeof(rport->node_name));
			memcpy(&rport->port_name, &ids->port_name,
				sizeof(rport->port_name));
			rport->port_id = ids->port_id;
			rport->roles = ids->roles;
			rport->port_state = FC_PORTSTATE_ONLINE;

			if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
				/* initiate a scan of the target */
				scsi_queue_work(shost, &rport->scan_work);

			return rport;
		}
	}

	/* No consistent binding found - create new remote port entry */
	rport = fc_rport_create(shost, channel, ids);

	return rport;
}
EXPORT_SYMBOL(fc_remote_port_add);

/*
 * fc_rport_tgt_remove - Removes the scsi target on the remote port
 * @rport:	The remote port to be operated on
 */
static void
fc_rport_tgt_remove(struct fc_rport *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);

	scsi_target_unblock(&rport->dev);

	/* Stop anything on the workq */
	if (!cancel_delayed_work(&rport->dev_loss_work))
		flush_scheduled_work();
	scsi_flush_work(shost);

	scsi_remove_target(&rport->dev);
}

/*
 * fc_rport_terminate - this routine tears down and deallocates a remote port.
 * @rport:	The remote port to be terminated
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 */
static void
fc_rport_terminate(struct fc_rport  *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct device *dev = &rport->dev;
	unsigned long flags;

	fc_rport_tgt_remove(rport);

	transport_remove_device(dev);
	device_del(dev);
	transport_destroy_device(dev);
	spin_lock_irqsave(shost->host_lock, flags);
	list_del(&rport->peers);
	spin_unlock_irqrestore(shost->host_lock, flags);
	put_device(&shost->shost_gendev);
}

/**
 * fc_remote_port_delete - notifies the fc transport that a remote
 *		port is no longer in existence.
 * @rport:	The remote port that no longer exists
 *
 * The LLDD calls this routine to notify the transport that a remote
 * port is no longer part of the topology. Note: Although a port
 * may no longer be part of the topology, it may persist in the remote
 * ports displayed by the fc_host. This is done so that target id
 * mappings (managed via the remote port structures), are always visible
 * as long as the mapping is valid, regardless of port state,
 *
 * If the remote port is not an FCP Target, it will be fully torn down
 * and deallocated, including the fc_remote_port class device.
 *
 * If the remote port is an FCP Target, the port structure will be
 * marked as Not Present, but will remain as long as there is a valid
 * SCSI target id mapping associated with the port structure. Validity
 * is determined by the binding type. If binding by wwpn, then the port
 * structure is always valid and will not be deallocated until the host
 * is removed.  If binding by wwnn, then the port structure is valid
 * until another port with the same node name is found in the topology.
 * If binding by port id (fc address), then the port structure is valid
 * valid until another port with the same address is identified.
 *
 * Called from interrupt or normal process context.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
void
fc_remote_port_delete(struct fc_rport  *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	unsigned long flags;

	/* If no scsi target id mapping or consistent binding type, delete it */
	if ((rport->scsi_target_id == -1) ||
	    (fc_host_tgtid_bind_type(shost) == FC_TGTID_BIND_NONE)) {
		fc_rport_terminate(rport);
		return;
	}

	fc_rport_tgt_remove(rport);

	spin_lock_irqsave(shost->host_lock, flags);
	list_move_tail(&rport->peers, &fc_host_rport_bindings(shost));
	spin_unlock_irqrestore(shost->host_lock, flags);

	/*
	 * Note: We do not remove or clear the hostdata area. This allows
	 *   host-specific target data to persist along with the
	 *   scsi_target_id. It's up to the host to manage it's hostdata area.
	 */

	/*
	 * Reinitialize port attributes that may change if the port comes back.
	 */
	rport->maxframe_size = -1;
	rport->supported_classes = FC_COS_UNSPECIFIED;
	rport->roles = FC_RPORT_ROLE_UNKNOWN;
	rport->port_state = FC_PORTSTATE_NOTPRESENT;

	/* remove the identifiers that aren't used in the consisting binding */
	switch (fc_host_tgtid_bind_type(shost)) {
	case FC_TGTID_BIND_BY_WWPN:
		rport->node_name = -1;
		rport->port_id = -1;
		break;
	case FC_TGTID_BIND_BY_WWNN:
		rport->port_name = -1;
		rport->port_id = -1;
		break;
	case FC_TGTID_BIND_BY_ID:
		rport->node_name = -1;
		rport->port_name = -1;
		break;
	case FC_TGTID_BIND_NONE:	/* to keep compiler happy */
		break;
	}
}
EXPORT_SYMBOL(fc_remote_port_delete);

/**
 * fc_remote_port_rolechg - notifies the fc transport that the roles
 *		on a remote may have changed.
 * @rport:	The remote port that changed.
 *
 * The LLDD calls this routine to notify the transport that the roles
 * on a remote port may have changed. The largest effect of this is
 * if a port now becomes a FCP Target, it must be allocated a
 * scsi target id.  If the port is no longer a FCP target, any
 * scsi target id value assigned to it will persist in case the
 * role changes back to include FCP Target. No changes in the scsi
 * midlayer will be invoked if the role changes (in the expectation
 * that the role will be resumed. If it doesn't normal error processing
 * will take place).
 *
 * Should not be called from interrupt context.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
void
fc_remote_port_rolechg(struct fc_rport  *rport, u32 roles)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct fc_host_attrs *fc_host =
			(struct fc_host_attrs *)shost->shost_data;
	unsigned long flags;
	int create = 0;

	rport->roles = roles;

	spin_lock_irqsave(shost->host_lock, flags);
	if ((rport->scsi_target_id == -1) &&
	    (rport->roles & FC_RPORT_ROLE_FCP_TARGET)) {
		rport->scsi_target_id = fc_host->next_target_id++;
		create = 1;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	if (create)
		/* initiate a scan of the target */
		scsi_queue_work(shost, &rport->scan_work);
}
EXPORT_SYMBOL(fc_remote_port_rolechg);

/**
 * fc_timeout_blocked_rport - Timeout handler for blocked remote port
 *                       that fails to return in the alloted time.
 * @data:	scsi target that failed to reappear in the alloted time.
 **/
static void
fc_timeout_blocked_rport(void  *data)
{
	struct fc_rport *rport = (struct fc_rport *)data;

	rport->port_state = FC_PORTSTATE_OFFLINE;

	dev_printk(KERN_ERR, &rport->dev,
		"blocked FC remote port time out: removing target\n");

	/*
	 * As this only occurs if the remote port (scsi target)
	 * went away and didn't come back - we'll remove
	 * all attached scsi devices.
	 */
	scsi_target_unblock(&rport->dev);
	scsi_remove_target(&rport->dev);
}

/**
 * fc_remote_port_block - temporarily block any scsi traffic to a remote port.
 * @rport:	remote port to be blocked.
 *
 * scsi lldd's with a FC transport call this routine to temporarily stop
 * all scsi traffic to a remote port. If the port is not a SCSI target,
 * no action is taken. If the port is a SCSI target, all attached devices
 * are placed into a SDEV_BLOCK state and a timer is started. The timer is
 * represents the maximum amount of time the port may be blocked. If the
 * timer expires, the port is considered non-existent and the attached
 * scsi devices will be removed.
 *
 * Called from interrupt or normal process context.
 *
 * Returns zero if successful or error if not
 *
 * Notes:
 *      This routine assumes no locks are held on entry.
 *
 *	The timeout and timer types are extracted from the fc transport 
 *	attributes from the caller's rport pointer.
 **/
int
fc_remote_port_block(struct fc_rport *rport)
{
	int timeout = rport->dev_loss_tmo;
	struct work_struct *work = &rport->dev_loss_work;

	if (timeout < 0 || timeout > SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		return -EINVAL;

	scsi_target_block(&rport->dev);

	/* cap the length the devices can be blocked */
	schedule_delayed_work(work, timeout * HZ);

	rport->port_state = FC_PORTSTATE_BLOCKED;
	return 0;
}
EXPORT_SYMBOL(fc_remote_port_block);

/**
 * fc_remote_port_unblock - restart any blocked scsi traffic to a remote port.
 * @rport:	remote port to be unblocked.
 *
 * scsi lld's with a FC transport call this routine to restart IO to all
 * devices associated with the caller's scsi target following a fc_target_block
 * request.  Called from interrupt or normal process context.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
 void
fc_remote_port_unblock(struct fc_rport *rport)
{
	struct work_struct *work = &rport->dev_loss_work;
	struct Scsi_Host *shost = rport_to_shost(rport);

	/*
	 * Stop the target timer first. Take no action on the del_timer
	 * failure as the state machine state change will validate the
	 * transaction.
	 */
	if (!cancel_delayed_work(work))
		flush_scheduled_work();

	if (rport->port_state == FC_PORTSTATE_OFFLINE)
		/*
		 * initiate a scan of the target as the target has
		 * been torn down.
		 */
		scsi_queue_work(shost, &rport->scan_work);
	else
		scsi_target_unblock(&rport->dev);

	rport->port_state = FC_PORTSTATE_ONLINE;
}
EXPORT_SYMBOL(fc_remote_port_unblock);

/**
 * fc_scsi_scan_rport - called to perform a scsi scan on a remote port.
 * @data:	remote port to be scanned.
 **/
static void
fc_scsi_scan_rport(void *data)
{
	struct fc_rport *rport = (struct fc_rport *)data;

	scsi_scan_target(&rport->dev, rport->channel, rport->scsi_target_id,
			SCAN_WILD_CARD, 1);
}


MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("FC Transport Attributes");
MODULE_LICENSE("GPL");

module_init(fc_transport_init);
module_exit(fc_transport_exit);
