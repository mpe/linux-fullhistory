/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_provider.c 1397 2004-12-28 05:09:00Z roland $
 */

#include <ib_smi.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"

static int mthca_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *props)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	struct mthca_dev* mdev = to_mdev(ibdev);

	u8 status;

	in_mad  = kmalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	props->fw_ver              = mdev->fw_ver;

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->base_version       = 1;
	in_mad->mgmt_class     	   = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	in_mad->class_version  	   = 1;
	in_mad->method         	   = IB_MGMT_METHOD_GET;
	in_mad->attr_id   	   = IB_SMP_ATTR_NODE_INFO;

	err = mthca_MAD_IFC(mdev, 1, 1,
			    1, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	props->device_cap_flags = mdev->device_cap_flags;
	props->vendor_id        = be32_to_cpup((u32 *) (out_mad->data + 36)) &
		0xffffff;
	props->vendor_part_id   = be16_to_cpup((u16 *) (out_mad->data + 30));
	props->hw_ver           = be16_to_cpup((u16 *) (out_mad->data + 32));
	memcpy(&props->sys_image_guid, out_mad->data +  4, 8);
	memcpy(&props->node_guid,      out_mad->data + 12, 8);

	err = 0;
 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mthca_query_port(struct ib_device *ibdev,
			    u8 port, struct ib_port_attr *props)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kmalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->base_version       = 1;
	in_mad->mgmt_class     	   = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	in_mad->class_version  	   = 1;
	in_mad->method         	   = IB_MGMT_METHOD_GET;
	in_mad->attr_id   	   = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod           = cpu_to_be32(port);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	props->lid               = be16_to_cpup((u16 *) (out_mad->data + 16));
	props->lmc               = out_mad->data[34] & 0x7;
	props->sm_lid            = be16_to_cpup((u16 *) (out_mad->data + 18));
	props->sm_sl             = out_mad->data[36] & 0xf;
	props->state             = out_mad->data[32] & 0xf;
	props->phys_state        = out_mad->data[33] >> 4;
	props->port_cap_flags    = be32_to_cpup((u32 *) (out_mad->data + 20));
	props->gid_tbl_len       = to_mdev(ibdev)->limits.gid_table_len;
	props->pkey_tbl_len      = to_mdev(ibdev)->limits.pkey_table_len;
	props->qkey_viol_cntr    = be16_to_cpup((u16 *) (out_mad->data + 48));
	props->active_width      = out_mad->data[31] & 0xf;
	props->active_speed      = out_mad->data[35] >> 4;

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mthca_modify_port(struct ib_device *ibdev,
			     u8 port, int port_modify_mask,
			     struct ib_port_modify *props)
{
	struct mthca_set_ib_param set_ib;
	struct ib_port_attr attr;
	int err;
	u8 status;

	if (down_interruptible(&to_mdev(ibdev)->cap_mask_mutex))
		return -ERESTARTSYS;

	err = mthca_query_port(ibdev, port, &attr);
	if (err)
		goto out;

	set_ib.set_si_guid     = 0;
	set_ib.reset_qkey_viol = !!(port_modify_mask & IB_PORT_RESET_QKEY_CNTR);

	set_ib.cap_mask = (attr.port_cap_flags | props->set_port_cap_mask) &
		~props->clr_port_cap_mask;

	err = mthca_SET_IB(to_mdev(ibdev), &set_ib, port, &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

out:
	up(&to_mdev(ibdev)->cap_mask_mutex);
	return err;
}

static int mthca_query_pkey(struct ib_device *ibdev,
			    u8 port, u16 index, u16 *pkey)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kmalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->base_version       = 1;
	in_mad->mgmt_class     	   = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	in_mad->class_version  	   = 1;
	in_mad->method         	   = IB_MGMT_METHOD_GET;
	in_mad->attr_id   	   = IB_SMP_ATTR_PKEY_TABLE;
	in_mad->attr_mod           = cpu_to_be32(index / 32);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	*pkey = be16_to_cpu(((u16 *) out_mad->data)[index % 32]);

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mthca_query_gid(struct ib_device *ibdev, u8 port,
			   int index, union ib_gid *gid)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kmalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->base_version       = 1;
	in_mad->mgmt_class     	   = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	in_mad->class_version  	   = 1;
	in_mad->method         	   = IB_MGMT_METHOD_GET;
	in_mad->attr_id   	   = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod           = cpu_to_be32(port);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	memcpy(gid->raw, out_mad->data + 8, 8);

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->base_version       = 1;
	in_mad->mgmt_class     	   = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	in_mad->class_version  	   = 1;
	in_mad->method         	   = IB_MGMT_METHOD_GET;
	in_mad->attr_id   	   = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod           = cpu_to_be32(index / 8);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	memcpy(gid->raw + 8, out_mad->data + (index % 8) * 16, 8);

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static struct ib_pd *mthca_alloc_pd(struct ib_device *ibdev)
{
	struct mthca_pd *pd;
	int err;

	pd = kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	err = mthca_pd_alloc(to_mdev(ibdev), pd);
	if (err) {
		kfree(pd);
		return ERR_PTR(err);
	}

	return &pd->ibpd;
}

static int mthca_dealloc_pd(struct ib_pd *pd)
{
	mthca_pd_free(to_mdev(pd->device), to_mpd(pd));
	kfree(pd);

	return 0;
}

static struct ib_ah *mthca_ah_create(struct ib_pd *pd,
				     struct ib_ah_attr *ah_attr)
{
	int err;
	struct mthca_ah *ah;

	ah = kmalloc(sizeof *ah, GFP_KERNEL);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	err = mthca_create_ah(to_mdev(pd->device), to_mpd(pd), ah_attr, ah);
	if (err) {
		kfree(ah);
		return ERR_PTR(err);
	}

	return &ah->ibah;
}

static int mthca_ah_destroy(struct ib_ah *ah)
{
	mthca_destroy_ah(to_mdev(ah->device), to_mah(ah));
	kfree(ah);

	return 0;
}

static struct ib_qp *mthca_create_qp(struct ib_pd *pd,
				     struct ib_qp_init_attr *init_attr)
{
	struct mthca_qp *qp;
	int err;

	switch (init_attr->qp_type) {
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_UD:
	{
		qp = kmalloc(sizeof *qp, GFP_KERNEL);
		if (!qp)
			return ERR_PTR(-ENOMEM);

		qp->sq.max    = init_attr->cap.max_send_wr;
		qp->rq.max    = init_attr->cap.max_recv_wr;
		qp->sq.max_gs = init_attr->cap.max_send_sge;
		qp->rq.max_gs = init_attr->cap.max_recv_sge;

		err = mthca_alloc_qp(to_mdev(pd->device), to_mpd(pd),
				     to_mcq(init_attr->send_cq),
				     to_mcq(init_attr->recv_cq),
				     init_attr->qp_type, init_attr->sq_sig_type,
				     qp);
		qp->ibqp.qp_num = qp->qpn;
		break;
	}
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	{
		qp = kmalloc(sizeof (struct mthca_sqp), GFP_KERNEL);
		if (!qp)
			return ERR_PTR(-ENOMEM);

		qp->sq.max    = init_attr->cap.max_send_wr;
		qp->rq.max    = init_attr->cap.max_recv_wr;
		qp->sq.max_gs = init_attr->cap.max_send_sge;
		qp->rq.max_gs = init_attr->cap.max_recv_sge;

		qp->ibqp.qp_num = init_attr->qp_type == IB_QPT_SMI ? 0 : 1;

		err = mthca_alloc_sqp(to_mdev(pd->device), to_mpd(pd),
				      to_mcq(init_attr->send_cq),
				      to_mcq(init_attr->recv_cq),
				      init_attr->sq_sig_type,
				      qp->ibqp.qp_num, init_attr->port_num,
				      to_msqp(qp));
		break;
	}
	default:
		/* Don't support raw QPs */
		return ERR_PTR(-ENOSYS);
	}

	if (err) {
		kfree(qp);
		return ERR_PTR(err);
	}

        init_attr->cap.max_inline_data = 0;

	return &qp->ibqp;
}

static int mthca_destroy_qp(struct ib_qp *qp)
{
	mthca_free_qp(to_mdev(qp->device), to_mqp(qp));
	kfree(qp);
	return 0;
}

static struct ib_cq *mthca_create_cq(struct ib_device *ibdev, int entries)
{
	struct mthca_cq *cq;
	int nent;
	int err;

	cq = kmalloc(sizeof *cq, GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	for (nent = 1; nent <= entries; nent <<= 1)
		; /* nothing */

	err = mthca_init_cq(to_mdev(ibdev), nent, cq);
	if (err) {
		kfree(cq);
		cq = ERR_PTR(err);
	}

	return &cq->ibcq;
}

static int mthca_destroy_cq(struct ib_cq *cq)
{
	mthca_free_cq(to_mdev(cq->device), to_mcq(cq));
	kfree(cq);

	return 0;
}

static inline u32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MTHCA_MPT_FLAG_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MTHCA_MPT_FLAG_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MTHCA_MPT_FLAG_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MTHCA_MPT_FLAG_LOCAL_WRITE  : 0) |
	       MTHCA_MPT_FLAG_LOCAL_READ;
}

static struct ib_mr *mthca_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mthca_mr *mr;
	int err;

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	err = mthca_mr_alloc_notrans(to_mdev(pd->device),
				     to_mpd(pd)->pd_num,
				     convert_access(acc), mr);

	if (err) {
		kfree(mr);
		return ERR_PTR(err);
	}

	return &mr->ibmr;
}

static struct ib_mr *mthca_reg_phys_mr(struct ib_pd       *pd,
				       struct ib_phys_buf *buffer_list,
				       int                 num_phys_buf,
				       int                 acc,
				       u64                *iova_start)
{
	struct mthca_mr *mr;
	u64 *page_list;
	u64 total_size;
	u64 mask;
	int shift;
	int npages;
	int err;
	int i, j, n;

	/* First check that we have enough alignment */
	if ((*iova_start & ~PAGE_MASK) != (buffer_list[0].addr & ~PAGE_MASK))
		return ERR_PTR(-EINVAL);

	if (num_phys_buf > 1 &&
	    ((buffer_list[0].addr + buffer_list[0].size) & ~PAGE_MASK))
		return ERR_PTR(-EINVAL);

	mask = 0;
	total_size = 0;
	for (i = 0; i < num_phys_buf; ++i) {
		if (buffer_list[i].addr & ~PAGE_MASK)
			return ERR_PTR(-EINVAL);
		if (i != 0 && i != num_phys_buf - 1 &&
		    (buffer_list[i].size & ~PAGE_MASK))
			return ERR_PTR(-EINVAL);

		total_size += buffer_list[i].size;
		if (i > 0)
			mask |= buffer_list[i].addr;
	}

	/* Find largest page shift we can use to cover buffers */
	for (shift = PAGE_SHIFT; shift < 31; ++shift)
		if (num_phys_buf > 1) {
			if ((1ULL << shift) & mask)
				break;
		} else {
			if (1ULL << shift >=
			    buffer_list[0].size +
			    (buffer_list[0].addr & ((1ULL << shift) - 1)))
				break;
		}

	buffer_list[0].size += buffer_list[0].addr & ((1ULL << shift) - 1);
	buffer_list[0].addr &= ~0ull << shift;

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	npages = 0;
	for (i = 0; i < num_phys_buf; ++i)
		npages += (buffer_list[i].size + (1ULL << shift) - 1) >> shift;

	if (!npages)
		return &mr->ibmr;

	page_list = kmalloc(npages * sizeof *page_list, GFP_KERNEL);
	if (!page_list) {
		kfree(mr);
		return ERR_PTR(-ENOMEM);
	}

	n = 0;
	for (i = 0; i < num_phys_buf; ++i)
		for (j = 0;
		     j < (buffer_list[i].size + (1ULL << shift) - 1) >> shift;
		     ++j)
			page_list[n++] = buffer_list[i].addr + ((u64) j << shift);

	mthca_dbg(to_mdev(pd->device), "Registering memory at %llx (iova %llx) "
		  "in PD %x; shift %d, npages %d.\n",
		  (unsigned long long) buffer_list[0].addr,
		  (unsigned long long) *iova_start,
		  to_mpd(pd)->pd_num,
		  shift, npages);

	err = mthca_mr_alloc_phys(to_mdev(pd->device),
				  to_mpd(pd)->pd_num,
				  page_list, shift, npages,
				  *iova_start, total_size,
				  convert_access(acc), mr);

	if (err) {
		kfree(mr);
		return ERR_PTR(err);
	}

	kfree(page_list);
	return &mr->ibmr;
}

static int mthca_dereg_mr(struct ib_mr *mr)
{
	mthca_free_mr(to_mdev(mr->device), to_mmr(mr));
	kfree(mr);
	return 0;
}

static ssize_t show_rev(struct class_device *cdev, char *buf)
{
	struct mthca_dev *dev = container_of(cdev, struct mthca_dev, ib_dev.class_dev);
	return sprintf(buf, "%x\n", dev->rev_id);
}

static ssize_t show_fw_ver(struct class_device *cdev, char *buf)
{
	struct mthca_dev *dev = container_of(cdev, struct mthca_dev, ib_dev.class_dev);
	return sprintf(buf, "%x.%x.%x\n", (int) (dev->fw_ver >> 32),
		       (int) (dev->fw_ver >> 16) & 0xffff,
		       (int) dev->fw_ver & 0xffff);
}

static ssize_t show_hca(struct class_device *cdev, char *buf)
{
	struct mthca_dev *dev = container_of(cdev, struct mthca_dev, ib_dev.class_dev);
	switch (dev->hca_type) {
	case TAVOR:        return sprintf(buf, "MT23108\n");
	case ARBEL_COMPAT: return sprintf(buf, "MT25208 (MT23108 compat mode)\n");
	case ARBEL_NATIVE: return sprintf(buf, "MT25208\n");
	default:           return sprintf(buf, "unknown\n");
	}
}

static CLASS_DEVICE_ATTR(hw_rev,   S_IRUGO, show_rev,    NULL);
static CLASS_DEVICE_ATTR(fw_ver,   S_IRUGO, show_fw_ver, NULL);
static CLASS_DEVICE_ATTR(hca_type, S_IRUGO, show_hca,    NULL);

static struct class_device_attribute *mthca_class_attributes[] = {
	&class_device_attr_hw_rev,
	&class_device_attr_fw_ver,
	&class_device_attr_hca_type
};

int mthca_register_device(struct mthca_dev *dev)
{
	int ret;
	int i;

	strlcpy(dev->ib_dev.name, "mthca%d", IB_DEVICE_NAME_MAX);
	dev->ib_dev.node_type            = IB_NODE_CA;
	dev->ib_dev.phys_port_cnt        = dev->limits.num_ports;
	dev->ib_dev.dma_device           = &dev->pdev->dev;
	dev->ib_dev.class_dev.dev        = &dev->pdev->dev;
	dev->ib_dev.query_device         = mthca_query_device;
	dev->ib_dev.query_port           = mthca_query_port;
	dev->ib_dev.modify_port          = mthca_modify_port;
	dev->ib_dev.query_pkey           = mthca_query_pkey;
	dev->ib_dev.query_gid            = mthca_query_gid;
	dev->ib_dev.alloc_pd             = mthca_alloc_pd;
	dev->ib_dev.dealloc_pd           = mthca_dealloc_pd;
	dev->ib_dev.create_ah            = mthca_ah_create;
	dev->ib_dev.destroy_ah           = mthca_ah_destroy;
	dev->ib_dev.create_qp            = mthca_create_qp;
	dev->ib_dev.modify_qp            = mthca_modify_qp;
	dev->ib_dev.destroy_qp           = mthca_destroy_qp;
	dev->ib_dev.create_cq            = mthca_create_cq;
	dev->ib_dev.destroy_cq           = mthca_destroy_cq;
	dev->ib_dev.poll_cq              = mthca_poll_cq;
	dev->ib_dev.get_dma_mr           = mthca_get_dma_mr;
	dev->ib_dev.reg_phys_mr          = mthca_reg_phys_mr;
	dev->ib_dev.dereg_mr             = mthca_dereg_mr;
	dev->ib_dev.attach_mcast         = mthca_multicast_attach;
	dev->ib_dev.detach_mcast         = mthca_multicast_detach;
	dev->ib_dev.process_mad          = mthca_process_mad;

	if (dev->hca_type == ARBEL_NATIVE) {
		dev->ib_dev.req_notify_cq = mthca_arbel_arm_cq;
		dev->ib_dev.post_send     = mthca_arbel_post_send;
		dev->ib_dev.post_recv     = mthca_arbel_post_receive;
	} else {
		dev->ib_dev.req_notify_cq = mthca_tavor_arm_cq;
		dev->ib_dev.post_send     = mthca_tavor_post_send;
		dev->ib_dev.post_recv     = mthca_tavor_post_receive;
	}

	init_MUTEX(&dev->cap_mask_mutex);

	ret = ib_register_device(&dev->ib_dev);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(mthca_class_attributes); ++i) {
		ret = class_device_create_file(&dev->ib_dev.class_dev,
					       mthca_class_attributes[i]);
		if (ret) {
			ib_unregister_device(&dev->ib_dev);
			return ret;
		}
	}

	return 0;
}

void mthca_unregister_device(struct mthca_dev *dev)
{
	ib_unregister_device(&dev->ib_dev);
}
