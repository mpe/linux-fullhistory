/* drivers/atm/atmtcp.c - ATM over TCP "device" driver */

/* Written 1997-1999 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/module.h>
#include <linux/wait.h>
#include <linux/atmdev.h>
#include <linux/atm_tcp.h>
#include <asm/uaccess.h>
#include "../../net/atm/protocols.h" /* @@@ fix this */


#define PRIV(dev) ((struct atmtcp_dev_data *) ((dev)->dev_data))


struct atmtcp_dev_data {
	struct atm_vcc *vcc;	/* control VCC; NULL if detached */
	int persist;		/* non-zero if persistent */
};


#define DEV_LABEL    "atmtcp"

#define MAX_VPI_BITS  8	/* simplifies life */
#define MAX_VCI_BITS 16


/*
 * Hairy code ahead: the control VCC may be closed while we're still
 * waiting for an answer, so we need to re-validate out_vcc every once
 * in a while.
 */


static int atmtcp_send_control(struct atm_vcc *vcc,int type,
    const struct atmtcp_control *msg,unsigned short flag)
{
	struct atm_vcc *out_vcc;
	struct sk_buff *skb;
	struct atmtcp_control *new_msg;
	unsigned short old_flags;

	out_vcc = PRIV(vcc->dev) ? PRIV(vcc->dev)->vcc : NULL;
	if (!out_vcc) return -EUNATCH;
	skb = alloc_skb(sizeof(*msg),GFP_KERNEL);
	if (!skb) return -ENOMEM;
	mb();
	out_vcc = PRIV(vcc->dev) ? PRIV(vcc->dev)->vcc : NULL;
	if (!out_vcc) {
		dev_kfree_skb(skb);
		return -EUNATCH;
	}
	atm_force_charge(out_vcc,skb->truesize);
	new_msg = (struct atmtcp_control *) skb_put(skb,sizeof(*new_msg));
	*new_msg = *msg;
	new_msg->hdr.length = ATMTCP_HDR_MAGIC;
	new_msg->type = type;
	new_msg->vcc = (unsigned long) vcc;
	old_flags = vcc->flags;
	out_vcc->push(out_vcc,skb);
	while (!((vcc->flags ^ old_flags) & flag)) {
		mb();
		out_vcc = PRIV(vcc->dev) ? PRIV(vcc->dev)->vcc : NULL;
		if (!out_vcc) return -EUNATCH;
		sleep_on(&vcc->sleep);
		
	}
	return 0;
}


static int atmtcp_recv_control(const struct atmtcp_control *msg)
{
	struct atm_vcc *vcc = (struct atm_vcc *) msg->vcc;

	vcc->vpi = msg->addr.sap_addr.vpi;
	vcc->vci = msg->addr.sap_addr.vci;
	vcc->qos = msg->qos;
	vcc->reply = msg->result;
	switch (msg->type) {
	    case ATMTCP_CTRL_OPEN:
		vcc->flags ^= ATM_VF_READY;
		break;
	    case ATMTCP_CTRL_CLOSE:
		vcc->flags ^= ATM_VF_ADDR;
		break;
	    default:
		printk(KERN_ERR "atmtcp_recv_control: unknown type %d\n",
		    msg->type);
		return -EINVAL;
	}
	wake_up(&vcc->sleep);
	return 0;
}


static void atmtcp_v_dev_close(struct atm_dev *dev)
{
	MOD_DEC_USE_COUNT;
}


static int atmtcp_v_open(struct atm_vcc *vcc,short vpi,int vci)
{
	struct atmtcp_control msg;
	int error;

	memset(&msg,0,sizeof(msg));
	msg.addr.sap_family = AF_ATMPVC;
	msg.hdr.vpi = htons(vpi);
	msg.addr.sap_addr.vpi = vpi;
	msg.hdr.vci = htons(vci);
	msg.addr.sap_addr.vci = vci;
	error = atm_find_ci(vcc,&msg.addr.sap_addr.vpi,&msg.addr.sap_addr.vci);
	if (error) return error;
	if (vpi == ATM_VPI_UNSPEC || vci == ATM_VCI_UNSPEC) return 0;
	msg.type = ATMTCP_CTRL_OPEN;
	msg.qos = vcc->qos;
	vcc->flags |= ATM_VF_ADDR;
	vcc->flags &= ~ATM_VF_READY; /* just in case ... */
	error = atmtcp_send_control(vcc,ATMTCP_CTRL_OPEN,&msg,ATM_VF_READY);
	if (error) return error;
	return vcc->reply;
}


static void atmtcp_v_close(struct atm_vcc *vcc)
{
	struct atmtcp_control msg;

	memset(&msg,0,sizeof(msg));
	msg.addr.sap_family = AF_ATMPVC;
	msg.addr.sap_addr.vpi = vcc->vpi;
	msg.addr.sap_addr.vci = vcc->vci;
	vcc->flags &= ~ATM_VF_READY;
	(void) atmtcp_send_control(vcc,ATMTCP_CTRL_CLOSE,&msg,ATM_VF_ADDR);
}


static int atmtcp_v_ioctl(struct atm_dev *dev,unsigned int cmd,void *arg)
{
	struct atm_cirange ci;
	struct atm_vcc *vcc;

	if (cmd != ATM_SETCIRANGE) return -EINVAL;
	if (copy_from_user(&ci,(void *) arg,sizeof(ci))) return -EFAULT;
	if (ci.vpi_bits == ATM_CI_MAX) ci.vpi_bits = MAX_VPI_BITS;
	if (ci.vci_bits == ATM_CI_MAX) ci.vci_bits = MAX_VCI_BITS;
	if (ci.vpi_bits > MAX_VPI_BITS || ci.vpi_bits < 0 ||
	    ci.vci_bits > MAX_VCI_BITS || ci.vci_bits < 0) return -EINVAL;
	for (vcc = dev->vccs; vcc; vcc = vcc->next)
		if ((vcc->vpi >> ci.vpi_bits) ||
		    (vcc->vci >> ci.vci_bits)) return -EBUSY;
	dev->ci_range = ci;
	return 0;
}


static int atmtcp_v_send(struct atm_vcc *vcc,struct sk_buff *skb)
{
	struct atmtcp_dev_data *dev_data;
	struct atm_vcc *out_vcc;
	struct sk_buff *new_skb;
	struct atmtcp_hdr *hdr;
	int size;

	dev_data = PRIV(vcc->dev);
	if (dev_data) out_vcc = dev_data->vcc;
	if (!dev_data || !out_vcc) {
		if (vcc->pop) vcc->pop(vcc,skb);
		else dev_kfree_skb(skb);
		if (dev_data) return 0;
		vcc->stats->tx_err++;
		return -ENOLINK;
	}
	size = skb->len+sizeof(struct atmtcp_hdr);
	new_skb = atm_alloc_charge(out_vcc,size,GFP_ATOMIC);
	if (!new_skb) {
		if (vcc->pop) vcc->pop(vcc,skb);
		else dev_kfree_skb(skb);
		vcc->stats->tx_err++;
		return -ENOBUFS;
	}
	hdr = (void *) skb_put(new_skb,sizeof(struct atmtcp_hdr));
	hdr->vpi = htons(vcc->vpi);
	hdr->vci = htons(vcc->vci);
	hdr->length = htonl(skb->len);
	memcpy(skb_put(new_skb,skb->len),skb->data,skb->len);
	if (vcc->pop) vcc->pop(vcc,skb);
	else dev_kfree_skb(skb);
	out_vcc->push(out_vcc,new_skb);
	return 0;
}


static int atmtcp_v_proc(struct atm_dev *dev,loff_t *pos,char *page)
{
	struct atmtcp_dev_data *dev_data = PRIV(dev);

	if (*pos) return 0;
	if (!dev_data->persist) return sprintf(page,"ephemeral\n");
	return sprintf(page,"persistent, %sconnected\n",
	    dev_data->vcc ? "" : "dis");
}


static void atmtcp_c_close(struct atm_vcc *vcc)
{
	struct atm_dev *atmtcp_dev;
	struct atmtcp_dev_data *dev_data;
	struct atm_vcc *walk;

	atmtcp_dev = (struct atm_dev *) vcc->dev_data;
	dev_data = PRIV(atmtcp_dev);
	dev_data->vcc = NULL;
	if (dev_data->persist) return;
	PRIV(atmtcp_dev) = NULL;
	kfree(dev_data);
	shutdown_atm_dev(atmtcp_dev);
	vcc->dev_data = NULL;
	for (walk = atmtcp_dev->vccs; walk; walk = walk->next)
		wake_up(&walk->sleep);
}


static int atmtcp_c_send(struct atm_vcc *vcc,struct sk_buff *skb)
{
	struct atm_dev *dev;
	struct atmtcp_hdr *hdr;
	struct atm_vcc *out_vcc;
	struct sk_buff *new_skb;
	int result = 0;

	if (!skb->len) return 0;
	dev = vcc->dev_data;
	hdr = (struct atmtcp_hdr *) skb->data;
	if (hdr->length == ATMTCP_HDR_MAGIC) {
		result = atmtcp_recv_control(
		    (struct atmtcp_control *) skb->data);
		goto done;
	}
	for (out_vcc = dev->vccs; out_vcc; out_vcc = out_vcc->next)
		if (out_vcc->vpi == ntohs(hdr->vpi) &&
		    out_vcc->vci == ntohs(hdr->vci) &&
		    out_vcc->qos.rxtp.traffic_class != ATM_NONE)
			break;
	if (!out_vcc) {
		vcc->stats->tx_err++;
		goto done;
	}
	skb_pull(skb,sizeof(struct atmtcp_hdr));
	new_skb = atm_alloc_charge(out_vcc,skb->len,GFP_KERNEL);
	if (!new_skb) {
		result = -ENOBUFS;
		goto done;
	}
	new_skb->stamp = xtime;
	memcpy(skb_put(new_skb,skb->len),skb->data,skb->len);
	out_vcc->push(out_vcc,new_skb);
done:
	if (vcc->pop) vcc->pop(vcc,skb);
	else dev_kfree_skb(skb);
	return result;
}


/*
 * Device operations for the virtual ATM devices created by ATMTCP.
 */


static struct atmdev_ops atmtcp_v_dev_ops = {
	atmtcp_v_dev_close,
	atmtcp_v_open,
	atmtcp_v_close,
	atmtcp_v_ioctl,
	NULL,		/* no getsockopt */
	NULL,		/* no setsockopt */
	atmtcp_v_send,
	NULL,		/* no direct writes */
	NULL,		/* no send_oam */
	NULL,		/* no phy_put */
	NULL,		/* no phy_get */
	NULL,		/* no feedback */
	NULL,		/* no change_qos */
	NULL,		/* no free_rx_skb */
	atmtcp_v_proc	/* proc_read */
};


/*
 * Device operations for the ATMTCP control device.
 */


static struct atmdev_ops atmtcp_c_dev_ops = {
	NULL,		/* no dev_close */
	NULL,		/* no open */
	atmtcp_c_close,
	NULL,		/* no ioctl */
	NULL,		/* no getsockopt */
	NULL,		/* no setsockopt */
	atmtcp_c_send,
	NULL,		/* no sg_send */
	NULL,		/* no send_oam */
	NULL,		/* no phy_put */
	NULL,		/* no phy_get */
	NULL,		/* no feedback */
	NULL,		/* no change_qos */
	NULL,		/* no free_rx_skb */
	NULL		/* no proc_read */
};


static struct atm_dev atmtcp_control_dev = {
	&atmtcp_c_dev_ops,
	NULL,		/* no PHY */
	"atmtcp",	/* type */
	999,		/* dummy device number */
	NULL,NULL,	/* pretend not to have any VCCs */
	NULL,NULL,	/* no data */
	0,		/* no flags */
	NULL,		/* no local address */
	{ 0 }		/* no ESI, no statistics */
};


static int atmtcp_create(int itf,int persist,struct atm_dev **result)
{
	struct atmtcp_dev_data *dev_data;
	struct atm_dev *dev;

	dev_data = kmalloc(sizeof(*dev_data),GFP_KERNEL);
	if (!dev_data) return -ENOMEM;
	dev = atm_dev_register(DEV_LABEL,&atmtcp_v_dev_ops,itf,0);
	if (!dev) {
		kfree(dev_data);
		return itf == -1 ? -ENOMEM : -EBUSY;
	}
	MOD_INC_USE_COUNT;
	dev->ci_range.vpi_bits = MAX_VPI_BITS;
	dev->ci_range.vci_bits = MAX_VCI_BITS;
	PRIV(dev) = dev_data;
	PRIV(dev)->vcc = NULL;
	PRIV(dev)->persist = persist;
	if (result) *result = dev;
	return 0;
}


int atmtcp_attach(struct atm_vcc *vcc,int itf)
{
	struct atm_dev *dev;

	dev = NULL;
	if (itf != -1) dev = atm_find_dev(itf);
	if (dev) {
		if (dev->ops != &atmtcp_v_dev_ops) return -EMEDIUMTYPE;
		if (PRIV(dev)->vcc) return -EBUSY;
	}
	else {
		int error;

		error = atmtcp_create(itf,0,&dev);
		if (error) return error;
	}
	PRIV(dev)->vcc = vcc;
	bind_vcc(vcc,&atmtcp_control_dev);
	vcc->flags |= ATM_VF_READY | ATM_VF_META;
	vcc->dev_data = dev;
	(void) atm_init_aal5(vcc); /* @@@ losing AAL in transit ... */
	vcc->stats = &atmtcp_control_dev.stats.aal5;
	return dev->number;
}


int atmtcp_create_persistent(int itf)
{
	return atmtcp_create(itf,1,NULL);
}


int atmtcp_remove_persistent(int itf)
{
	struct atm_dev *dev;
	struct atmtcp_dev_data *dev_data;

	dev = atm_find_dev(itf);
	if (!dev) return -ENODEV;
	if (dev->ops != &atmtcp_v_dev_ops) return -EMEDIUMTYPE;
	dev_data = PRIV(dev);
	if (!dev_data->persist) return 0;
	dev_data->persist = 0;
	if (PRIV(dev)->vcc) return 0;
	kfree(dev_data);
	shutdown_atm_dev(dev);
	return 0;
}


#ifdef MODULE

int init_module(void)
{
	atm_tcp_ops.attach = atmtcp_attach;
	atm_tcp_ops.create_persistent = atmtcp_create_persistent;
	atm_tcp_ops.remove_persistent = atmtcp_remove_persistent;
	return 0;
}


void cleanup_module(void)
{
	atm_tcp_ops.attach = NULL;
	atm_tcp_ops.create_persistent = NULL;
	atm_tcp_ops.remove_persistent = NULL;
}

#else

struct atm_tcp_ops atm_tcp_ops = {
	atmtcp_attach,			/* attach */
	atmtcp_create_persistent,	/* create_persistent */
	atmtcp_remove_persistent	/* remove_persistent */
};

#endif
