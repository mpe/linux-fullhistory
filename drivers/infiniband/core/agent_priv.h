/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
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
 * $Id: agent_priv.h 1389 2004-12-27 22:56:47Z roland $
 */

#ifndef __IB_AGENT_PRIV_H__
#define __IB_AGENT_PRIV_H__

#include <linux/pci.h>

#define SPFX "ib_agent: "

struct ib_agent_send_wr {
	struct list_head send_list;
	struct ib_ah *ah;
	struct ib_mad_private *mad;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

struct ib_agent_port_private {
	struct list_head port_list;
	struct list_head send_posted_list;
	spinlock_t send_list_lock;
	int port_num;
	struct ib_mad_agent *smp_agent;	      /* SM class */
	struct ib_mad_agent *perf_mgmt_agent; /* PerfMgmt class */
	struct ib_mr *mr;
};

#endif	/* __IB_AGENT_PRIV_H__ */
