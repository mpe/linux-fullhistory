/* net/atm/proc.c - ATM /proc interface */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */

/*
 * The mechanism used here isn't designed for speed but rather for convenience
 * of implementation. We only return one entry per read system call, so we can
 * be reasonably sure not to overrun the page and race conditions may lead to
 * the addition or omission of some lines but never to any corruption of a
 * line's internal structure.
 *
 * Making the whole thing slightly more efficient is left as an exercise to the
 * reader. (Suggestions: wrapper which loops to get several entries per system
 * call; or make --left slightly more clever to avoid O(n^2) characteristics.)
 * I find it fast enough on my unloaded 266 MHz Pentium 2 :-)
 */


#include <linux/config.h>
#include <linux/module.h> /* for EXPORT_SYMBOL */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/netdevice.h>
#include <linux/atmclip.h>
#include <linux/atmarp.h>
#include <linux/if_arp.h>
#include <linux/init.h> /* for __init */
#include <asm/uaccess.h>
#include <asm/param.h> /* for HZ */
#include "resources.h"
#include "common.h" /* atm_proc_init prototype */
#include "signaling.h" /* to get sigd - ugly too */

#ifdef CONFIG_AREQUIPA
#include <linux/arequipa.h>
void atm_push_arequipa(struct atm_vcc *vcc,struct sk_buff *skb);
#endif

#ifdef CONFIG_ATM_CLIP
#include <net/atmclip.h>
#include "ipcommon.h"
extern void clip_push(struct atm_vcc *vcc,struct sk_buff *skb);
#endif

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include "lec.h"
#include "lec_arpc.h"
extern struct atm_lane_ops atm_lane_ops; /* in common.c */
#endif


static ssize_t proc_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);


static struct file_operations proc_atm_operations = {
	NULL,			/* lseek */
	proc_atm_read,		/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release */
	NULL			/* can't fsync */
};

struct inode_operations proc_atm_inode_operations = {
	&proc_atm_operations,	/* default ATM directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};


#define ENTRY(name) static struct proc_dir_entry atm_proc_entry_##name = \
    { 0, sizeof(#name)-1, #name, S_IFREG | S_IRUGO, 1, 0, 0, 0, \
    &proc_atm_inode_operations, NULL }
#define REG(name) if (!error) error = proc_register(&atm_proc_root, \
    &atm_proc_entry_##name)
#define INO(name) (atm_proc_entry_##name.low_ino)


ENTRY(devices);
ENTRY(pvc);
ENTRY(svc);
#ifdef CONFIG_ATM_CLIP
ENTRY(arp);
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
ENTRY(lec);
#endif
#ifdef CONFIG_AREQUIPA
ENTRY(arequipa);
#endif


static int atm_header(ino_t ino,char *buf)
{
	if (ino == INO(devices))
		return sprintf(buf,"Itf Type    ESI/\"MAC\"addr "
		    "AAL(TX,err,RX,err,drop) ...\n");
	if (ino == INO(pvc))
		return sprintf(buf,"Itf VPI VCI   AAL RX(PCR,Class) "
		    "TX(PCR,Class)\n");
	if (ino == INO(svc))
		return sprintf(buf,"Itf VPI VCI   State      Remote\n");
#ifdef CONFIG_ATM_CLIP
	if (ino == INO(arp))
		return sprintf(buf,"IPitf TypeEncp Idle IP address      "
		    "ATM address\n");
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	if (ino == INO(lec))
		return sprintf(buf,"Itf  MAC          ATM destination"
		    "                          Status            Flags "
		    "VPI/VCI Recv VPI/VCI\n");
#endif
#ifdef CONFIG_AREQUIPA
	if (ino == INO(arequipa))
		return sprintf(buf,"Itf VPI VCI   State    Sock# Inode\n");
#endif
	return -EINVAL;
}


static void add_stats(char *buf,const char *aal,
  const struct atm_aal_stats *stats)
{
	sprintf(strchr(buf,0),"%s ( %ld %ld %ld %ld %ld )",aal,stats->tx,
	    stats->tx_err,stats->rx,stats->rx_err,stats->rx_drop);
}


static void dev_info(const struct atm_dev *dev,char *buf)
{
	int off,i;

	off = sprintf(buf,"%3d %-8s",dev->number,dev->type);
	for (i = 0; i < ESI_LEN; i++)
		off += sprintf(buf+off,"%02x",dev->esi[i]);
	strcat(buf,"  ");
	add_stats(buf,"0",&dev->stats.aal0);
	strcat(buf,"  ");
	add_stats(buf,"5",&dev->stats.aal5);
	strcat(buf,"\n");
}


#ifdef CONFIG_ATM_CLIP


static int svc_addr(char *buf,struct sockaddr_atmsvc *addr)
{
	static int code[] = { 1,2,10,6,1,0 };
	static int e164[] = { 1,8,4,6,1,0 };
	int *fields;
	int len,i,j,pos;

	len = 0;
	if (*addr->sas_addr.pub) {
		strcpy(buf,addr->sas_addr.pub);
		len = strlen(addr->sas_addr.pub);
		buf += len;
		if (*addr->sas_addr.pub) {
			*buf += '+';
			len++;
		}
	}
	else if (!*addr->sas_addr.prv) {
			strcpy(buf,"(none)");
			return strlen(buf);
		}
	if (*addr->sas_addr.prv) {
		len += 44;
		pos = 0;
		fields = *addr->sas_addr.prv == ATM_AFI_E164 ? e164 : code;
		for (i = 0; fields[i]; i++) {
			for (j = fields[i]; j; j--) {
				sprintf(buf,"%02X",addr->sas_addr.prv[pos++]);
				buf += 2;
			}
			if (fields[i+1]) *buf++ = '.';
		}
	}
	return len;
}


static void atmarp_info(struct net_device *dev,struct atmarp_entry *entry,
    struct clip_vcc *clip_vcc,char *buf)
{
	unsigned char *ip;
	int svc,off,ip_len;

	svc = !clip_vcc || clip_vcc->vcc->family == AF_ATMSVC;
	off = sprintf(buf,"%-6s%-4s%-4s%5ld ",dev->name,svc ? "SVC" : "PVC",
	    !clip_vcc || clip_vcc->encap ? "LLC" : "NULL",
	    (jiffies-(clip_vcc ? clip_vcc->last_use : entry->neigh->used))/
	    HZ);
	ip = (unsigned char *) &entry->ip;
	ip_len = sprintf(buf+off,"%d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
	off += ip_len;
	while (ip_len++ < 16) buf[off++] = ' ';
	if (!clip_vcc)
		if (time_before(jiffies, entry->expires))
			strcpy(buf+off,"(resolving)\n");
		else sprintf(buf+off,"(expired, ref %d)\n",
			    atomic_read(&entry->neigh->refcnt));
	else if (!svc)
			sprintf(buf+off,"%d.%d.%d\n",clip_vcc->vcc->dev->number,
			    clip_vcc->vcc->vpi,clip_vcc->vcc->vci);
		else {
			off += svc_addr(buf+off,&clip_vcc->vcc->remote);
			strcpy(buf+off,"\n");
		}
}


#endif


static void pvc_info(struct atm_vcc *vcc,char *buf)
{
	static const char *class_name[] = { "off","UBR","CBR","VBR","ABR" };
	static const char *aal_name[] = {
		"---",	"1",	"2",	"3/4",	/*  0- 3 */
		"???",	"5",	"???",	"???",	/*  4- 7 */
		"???",	"???",	"???",	"???",	/*  8-11 */
		"???",	"0",	"???",	"???"};	/* 12-15 */
	int off;

	off = sprintf(buf,"%3d %3d %5d %-3s %7d %-5s %7d %-6s",
	    vcc->dev->number,vcc->vpi,vcc->vci,
	    vcc->qos.aal >= sizeof(aal_name)/sizeof(aal_name[0]) ? "err" :
	    aal_name[vcc->qos.aal],vcc->qos.rxtp.min_pcr,
	    class_name[vcc->qos.rxtp.traffic_class],vcc->qos.txtp.min_pcr,
	    class_name[vcc->qos.txtp.traffic_class]);
#ifdef CONFIG_ATM_CLIP
	if (vcc->push == clip_push) {
		struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
		struct net_device *dev;

		dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : NULL;
		off += sprintf(buf+off,"CLIP, Itf:%s, Encap:",
		    dev ? dev->name : "none?");
		if (clip_vcc->encap) off += sprintf(buf+off,"LLC/SNAP");
		else off += sprintf(buf+off,"None");
	}
#endif
	strcpy(buf+off,"\n");
}


static const char *vcc_state(struct atm_vcc *vcc)
{
	static const char *map[] = { ATM_VS2TXT_MAP };

	return map[ATM_VF2VS(vcc->flags)];
}


static void svc_info(struct atm_vcc *vcc,char *buf)
{
	char *here;
	int i;

	if (!vcc->dev) sprintf(buf,"Unassigned    ");
	else sprintf(buf,"%3d %3d %5d ",vcc->dev->number,vcc->vpi,vcc->vci);
	here = strchr(buf,0);
	here += sprintf(here,"%-10s ",vcc_state(vcc));
	here += sprintf(here,"%s%s",vcc->remote.sas_addr.pub,
	    *vcc->remote.sas_addr.pub && *vcc->remote.sas_addr.prv ? "+" : "");
	if (*vcc->remote.sas_addr.prv)
		for (i = 0; i < ATM_ESA_LEN; i++)
			here += sprintf(here,"%02x",
			    vcc->remote.sas_addr.prv[i]);
	strcat(here,"\n");
}


#ifdef CONFIG_AREQUIPA


static const char *arequipa_state(const struct atm_vcc *vcc)
{
	if (!(vcc->flags & ATM_VF_REGIS) && vcc->family != PF_ATMPVC)
		return "DOOMED";
	if (vcc->upper) return "ATTACHED";
	return "DANGLING";
}


static void arequipa_info(struct atm_vcc *vcc,char *buf)
{
	char *here;
 
	if (!vcc->dev) sprintf(buf,"Unassigned    ");
	else sprintf(buf,"%3d %3d %5d ",vcc->dev->number,vcc->vpi,vcc->vci);
        here = strchr(buf,0);
        here += sprintf(here,"%-8s ",arequipa_state(vcc));
	if (vcc->upper)
		here += sprintf(here,"%5d %ld",vcc->upper->num,
		    vcc->upper->socket && SOCK_INODE(vcc->upper->socket) ?
		    SOCK_INODE(vcc->upper->socket)->i_ino : 0);
	strcat(here,"\n");
}


#endif


#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)

static char*
lec_arp_get_status_string(unsigned char status)
{
  switch(status) {
  case ESI_UNKNOWN:
    return "ESI_UNKNOWN       ";
  case ESI_ARP_PENDING:
    return "ESI_ARP_PENDING   ";
  case ESI_VC_PENDING:
    return "ESI_VC_PENDING    ";
  case ESI_FLUSH_PENDING:
    return "ESI_FLUSH_PENDING ";
  case ESI_FORWARD_DIRECT:
    return "ESI_FORWARD_DIRECT";
  default:
    return "<Unknown>         ";
  }
}

static void 
lec_info(struct lec_arp_table *entry, char *buf)
{
        int j, offset=0;
        

        for(j=0;j<ETH_ALEN;j++) {
                offset+=sprintf(buf+offset,"%2.2x",0xff&entry->mac_addr[j]);
        }
        offset+=sprintf(buf+offset, " ");
        for(j=0;j<ATM_ESA_LEN;j++) {
                offset+=sprintf(buf+offset,"%2.2x",0xff&entry->atm_addr[j]);
        }
        offset+=sprintf(buf+offset, " %s %4.4x",
                        lec_arp_get_status_string(entry->status),
                        entry->flags&0xffff);
        if (entry->vcc) {
                offset+=sprintf(buf+offset, "%3d %3d ", entry->vcc->vpi, 
                                entry->vcc->vci);                
        } else
                offset+=sprintf(buf+offset, "        ");
        if (entry->recv_vcc) {
                offset+=sprintf(buf+offset, "     %3d %3d", 
                                entry->recv_vcc->vpi, entry->recv_vcc->vci);
        }

        sprintf(buf+offset,"\n");
}

#endif


/*
 * FIXME: it isn't safe to walk the VCC list without turning off interrupts.
 * What is really needed is some lock on the devices. Ditto for ATMARP.
 */

static int atm_info(ino_t ino,loff_t *pos,char *buf)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc;
	int left;

	if (ino == INO(devices)) {
		left = *pos-1;
		for (dev = atm_devs; dev && left; dev = dev->next) left--;
		if (!dev) return 0;
		dev_info(dev,buf);
		return strlen(buf);
	}
	if (ino == INO(pvc)) {
		left = *pos-1;
		for (dev = atm_devs; dev; dev = dev->next)
			for (vcc = dev->vccs; vcc; vcc = vcc->next)
				if (vcc->family == PF_ATMPVC &&
				    vcc->dev && !left--) {
					pvc_info(vcc,buf);
					return strlen(buf);
				}
		return 0;
	}
	if (ino == INO(svc)) {
		left = *pos-1;
		for (dev = atm_devs; dev; dev = dev->next)
			for (vcc = dev->vccs; vcc; vcc = vcc->next)
				if (vcc->family == PF_ATMSVC && !left--) {
					svc_info(vcc,buf);
					return strlen(buf);
				}
		for (vcc = nodev_vccs; vcc; vcc = vcc->next)
			if (vcc->family == PF_ATMSVC && !left--) {
				svc_info(vcc,buf);
				return strlen(buf);
			}
		return 0;
	}
#ifdef CONFIG_ATM_CLIP
	if (ino == INO(arp)) {
		struct neighbour *n;
		int i,count;

		count = *pos;
		read_lock_bh(&clip_tbl.lock);
		for (i = 0; i <= NEIGH_HASHMASK; i++)
			for (n = clip_tbl.hash_buckets[i]; n; n = n->next) {
				struct atmarp_entry *entry = NEIGH2ENTRY(n);
				struct clip_vcc *vcc;

				if (!entry->vccs) {
					if (--count) continue;
					atmarp_info(n->dev,entry,NULL,buf);
					read_unlock_bh(&clip_tbl.lock);
					return strlen(buf);
				}
				for (vcc = entry->vccs; vcc;
				    vcc = vcc->next) {
					if (--count) continue;
					atmarp_info(n->dev,entry,vcc,buf);
					read_unlock_bh(&clip_tbl.lock);
					return strlen(buf);
				}
			}
		read_unlock_bh(&clip_tbl.lock);
		return 0;
	}
#endif
#ifdef CONFIG_AREQUIPA
	if (ino == INO(arequipa)) {
		left = *pos-1;
		for (dev = atm_devs; dev; dev = dev->next)
			for (vcc = dev->vccs; vcc; vcc = vcc->next)
				if (vcc->push == atm_push_arequipa && !left--) {
					arequipa_info(vcc,buf);
					return strlen(buf);
				}
		return 0;
	}
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	if (ino == INO(lec)) {
		struct lec_priv *priv;
		struct lec_arp_table *entry;
		int i, count, d, e;
		struct net_device **dev_lec;

		if (atm_lane_ops.get_lecs == NULL)
			return 0; /* the lane module is not there yet */
		else
			dev_lec = atm_lane_ops.get_lecs();

		count = *pos;
		for(d=0;d<MAX_LEC_ITF;d++) {
			if (!dev_lec[d] || !(priv =
			    (struct lec_priv *) dev_lec[d]->priv)) continue;
			for(i=0;i<LEC_ARP_TABLE_SIZE;i++) {
				entry = priv->lec_arp_tables[i];
				for(;entry;entry=entry->next) {
					if (--count) continue;
					e=sprintf(buf,"%s ",
					    dev_lec[d]->name);
					lec_info(entry,buf+e);
					return strlen(buf);
				}
			}
			for(entry=priv->lec_arp_empty_ones; entry;
			    entry=entry->next) {
				if (--count) continue;
				e=sprintf(buf,"%s ",dev_lec[d]->name);
				lec_info(entry, buf+e);
				return strlen(buf);
			}
			for(entry=priv->lec_no_forward; entry;
			    entry=entry->next) {
				if (--count) continue;
				e=sprintf(buf,"%s ",dev_lec[d]->name);
				lec_info(entry, buf+e);
				return strlen(buf);
			}
			for(entry=priv->mcast_fwds; entry;
			    entry=entry->next) {
				if (--count) continue;
				e=sprintf(buf,"%s ",dev_lec[d]->name);
				lec_info(entry, buf+e);
				return strlen(buf);
			}
		}
		return 0;
	}
#endif
	return -EINVAL;
}


static ssize_t proc_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos)
{
	struct atm_dev *dev;
	unsigned long page;
	int ino = file->f_dentry->d_inode->i_ino;
	int length;

	if (count < 0) return -EINVAL;
	page = get_free_page(GFP_KERNEL);
	if (!page) return -ENOMEM;
	for (dev = atm_devs; dev; dev = dev->next)
		if (dev->ops->proc_read && dev->proc_entry->low_ino == ino)
			break;
	if (dev) length = dev->ops->proc_read(dev,pos,(char *) page);
	else if (*pos) length = atm_info(ino,pos,(char *) page);
		else length = atm_header(ino,(char *) page);
	if (length > count) length = -EINVAL;
	if (length >= 0) {
		if (copy_to_user(buf,(char *) page,length)) length = -EFAULT;
		(*pos)++;
	}
	free_page(page);
	return length;
}


struct proc_dir_entry atm_proc_root = { 0, 3, "atm",
    S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0, 0, &proc_dir_inode_operations,
    NULL, NULL, NULL, NULL, NULL };


EXPORT_SYMBOL(atm_proc_root);


int atm_proc_dev_register(struct atm_dev *dev)
{
	ENTRY(template);
	int digits,num;
	int error;

	error = -ENOMEM;
	digits = 0;
	for (num = dev->number; num; num /= 10) digits++;
	if (!digits) digits++;
	dev->proc_entry = kmalloc(sizeof(*dev->proc_entry),GFP_KERNEL);
	if (!dev->proc_entry) goto fail0;
	dev->proc_name = kmalloc(strlen(dev->type)+digits+2,GFP_KERNEL);
	if (!dev->proc_name) goto fail1;
	*dev->proc_entry = atm_proc_entry_template;
	dev->proc_entry->name = dev->proc_name;
	dev->proc_entry->namelen = sprintf(dev->proc_name,"%s:%d",dev->type,
	    dev->number);
	error = proc_register(&atm_proc_root,dev->proc_entry);
	if (!error) return 0;
	kfree(dev->proc_name);
fail1:
	kfree(dev->proc_entry);
fail0:
	return error;
}


void atm_proc_dev_deregister(struct atm_dev *dev)
{
	proc_unregister(&atm_proc_root,dev->proc_entry->low_ino);
	kfree(dev->proc_entry);
	kfree(dev->proc_name);
}


int __init atm_proc_init(void)
{
	int error;

	error = proc_register(&proc_root,&atm_proc_root);
	REG(devices);
	REG(pvc);
	REG(svc);
#ifdef CONFIG_ATM_CLIP
	REG(arp);
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	REG(lec);
#endif
#ifdef CONFIG_AREQUIPA
	REG(arequipa);
#endif
	return error;
}
