/* fcp_scsi.h: Generic SCSI on top of FC4 - interface defines.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1998 Jirka Hanika (geo@ff.cuni.cz)
 */

#ifndef _FCP_SCSI_H
#define _FCP_SCSI_H

#include <linux/types.h>
#include <linux/blk.h>
#include "../scsi/scsi.h"

#include "fc.h"
#include "fcp.h"

#ifdef __sparc__

#include <asm/sbus.h>
typedef u32	dma_handle;

#else
#error Need to port FC layer to your architecture
#endif

/* 0 or 1 */
#define	FCP_SCSI_USE_NEW_EH_CODE	0

#define FC_CLASS_OUTBOUND	0x01
#define FC_CLASS_INBOUND	0x02
#define FC_CLASS_SIMPLE		0x03
#define FC_CLASS_IO_WRITE	0x04
#define FC_CLASS_IO_READ	0x05
#define FC_CLASS_UNSOLICITED	0x06
#define FC_CLASS_OFFLINE	0x08

#define PROTO_OFFLINE		0x02

struct _fc_channel; 

typedef struct fcp_cmnd {
	struct fcp_cmnd		*next;
	struct fcp_cmnd		*prev;
	void			(*done)(Scsi_Cmnd *);
	int			proto;
	int		 	token;
	/* FCP SCSI stuff */
	dma_handle		data;
	/* From now on this cannot be touched for proto == TYPE_SCSI_FCP */
	fc_hdr			fch;
	dma_handle		cmd;
	dma_handle		rsp;
	int			cmdlen;
	int			rsplen;
	int			class;
	int			datalen;
	/* This is just used as a verification during login */
	struct _fc_channel	*fc;
} fcp_cmnd;

typedef struct _fc_channel {
	struct _fc_channel	*next;
	int			irq;
	int			state;
	int			sid;
	int			did;
	char			name[16];
	void			(*fcp_register)(struct _fc_channel *, u8, int);
	void			(*reset)(struct _fc_channel *);
	int			(*hw_enque)(struct _fc_channel *, fcp_cmnd *);
	fc_wwn			wwn_node;
	fc_wwn			wwn_nport;
	fc_wwn			wwn_dest;
	common_svc_parm		*common_svc;
	svc_parm		*class_svcs;
#ifdef __sparc__	
	struct linux_sbus_device *dev;
#endif
	struct module		*module;
	/* FCP SCSI stuff */
	short			can_queue;
	short			abort_count;
	int			rsp_size;
	fcp_cmd			*scsi_cmd_pool;
	char			*scsi_rsp_pool;
	dma_handle		dma_scsi_cmd, dma_scsi_rsp;
	long			*scsi_bitmap;
	long			scsi_bitmap_end;
	int			scsi_free;
	int			(*encode_addr)(Scsi_Cmnd *cmnd, u16 *addr);
	fcp_cmnd		*scsi_que;
	char			scsi_name[4];
	fcp_cmnd		**token_tab;
	int			channels;
	int			targets;
	long			*ages;
	Scsi_Cmnd		*rst_pkt;
	/* LOGIN stuff */
	fcp_cmnd		*login;
	void			*ls;
} fc_channel;

extern fc_channel *fc_channels;

#define FC_STATE_UNINITED	0
#define FC_STATE_ONLINE		1
#define FC_STATE_OFFLINE	2
#define FC_STATE_RESETING	3
#define FC_STATE_FPORT_OK	4
#define FC_STATE_MAYBEOFFLINE	5

#define FC_STATUS_OK			0
#define FC_STATUS_P_RJT			2
#define FC_STATUS_F_RJT			3
#define FC_STATUS_P_BSY			4
#define FC_STATUS_F_BSY			5
#define FC_STATUS_ERR_OFFLINE		0x11
#define FC_STATUS_TIMEOUT		0x12
#define FC_STATUS_ERR_OVERRUN		0x13
#define FC_STATUS_UNKNOWN_CQ_TYPE	0x20
#define FC_STATUS_BAD_SEG_CNT		0x21
#define FC_STATUS_MAX_XCHG_EXCEEDED	0x22
#define FC_STATUS_BAD_XID		0x23
#define FC_STATUS_XCHG_BUSY		0x24
#define FC_STATUS_BAD_POOL_ID		0x25
#define FC_STATUS_INSUFFICIENT_CQES	0x26
#define FC_STATUS_ALLOC_FAIL		0x27
#define FC_STATUS_BAD_SID		0x28
#define FC_STATUS_NO_SEQ_INIT		0x29

void fcp_queue_empty(fc_channel *);
int fcp_init(fc_channel *);
void fcp_release(fc_channel *fc_chain, int count);
void fcp_receive_solicited(fc_channel *, int, int, int, fc_hdr *);
void fcp_state_change(fc_channel *, int);

#define for_each_fc_channel(fc)				\
	for (fc = fc_channels; fc; fc = fc->next)
	
#define for_each_online_fc_channel(fc) 			\
	for_each_fc_channel(fc)				\
		if (fc->state == FC_STATE_ONLINE)

int fcp_scsi_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int fcp_old_abort(Scsi_Cmnd *);
int fcp_scsi_abort(Scsi_Cmnd *);
int fcp_scsi_dev_reset(Scsi_Cmnd *);
int fcp_scsi_bus_reset(Scsi_Cmnd *);
int fcp_scsi_host_reset(Scsi_Cmnd *);

#endif /* !(_FCP_SCSI_H) */
