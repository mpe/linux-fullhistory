/*
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * String handling code courtesy of Gerard Roudier's <groudier@club-internet.fr>
 * sym driver.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7xxx_proc.c#13 $
 */
#include "aic7xxx_osm.h"
#include "aic7xxx_inline.h"

static void	copy_mem_info(struct info_str *info, char *data, int len);
static int	copy_info(struct info_str *info, char *fmt, ...);
static u_int	scsi_calc_syncsrate(u_int period_factor);
static void	ahc_dump_target_state(struct ahc_softc *ahc,
				      struct info_str *info,
				      u_int our_id, char channel,
				      u_int target_id, u_int target_offset);
static void	ahc_dump_device_state(struct info_str *info,
				      struct ahc_linux_device *dev);

static void
copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->offset + info->length)
		len = info->offset + info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}

	if (info->pos < info->offset) {
		off_t partial;

		partial = info->offset - info->pos;
		data += partial;
		info->pos += partial;
		len  -= partial;
	}

	if (len > 0) {
		memcpy(info->buffer, data, len);
		info->pos += len;
		info->buffer += len;
	}
}

static int
copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[256];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);
	return (len);
}

/*
 * Table of syncrates that don't follow the "divisible by 4"
 * rule. This table will be expanded in future SCSI specs.
 */
static struct {
	u_int period_factor;
	u_int period;	/* in 10ths of ns */
} scsi_syncrates[] = {
	{ 0x09, 125 },	/* FAST-80 */
	{ 0x0a, 250 },	/* FAST-40 40MHz */
	{ 0x0b, 303 },	/* FAST-40 33MHz */
	{ 0x0c, 500 }	/* FAST-20 */
};
 
/*
 * Return the frequency in kHz corresponding to the given
 * sync period factor.
 */
static u_int
scsi_calc_syncsrate(u_int period_factor)
{
	int i; 
	int num_syncrates;
 
	num_syncrates = sizeof(scsi_syncrates) / sizeof(scsi_syncrates[0]);
	/* See if the period is in the "exception" table */
	for (i = 0; i < num_syncrates; i++) {

		if (period_factor == scsi_syncrates[i].period_factor) {
       			/* Period in kHz */
			return (10000000 / scsi_syncrates[i].period);
		}
	}

	/*
	 * Wasn't in the table, so use the standard
	 * 4 times conversion.
	 */
	return (10000000 / (period_factor * 4 * 10));
}

void
ahc_format_transinfo(struct info_str *info, struct ahc_transinfo *tinfo)
{
	u_int speed;
	u_int freq;
	u_int mb;

        speed = 3300;
        freq = 0;
	if (tinfo->offset != 0) {
		freq = scsi_calc_syncsrate(tinfo->period);
		speed = freq;
	}
	speed *= (0x01 << tinfo->width);
        mb = speed / 1000;
        if (mb > 0)
		copy_info(info, "%d.%03dMB/s transfers", mb, speed % 1000);
        else
		copy_info(info, "%dKB/s transfers", speed);

	if (freq != 0) {
		copy_info(info, " (%d.%03dMHz%s, offset %d",
			 freq / 1000, freq % 1000,
			 (tinfo->ppr_options & MSG_EXT_PPR_DT_REQ) != 0
			 ? " DT" : "", tinfo->offset);
	}

	if (tinfo->width > 0) {
		if (freq != 0) {
			copy_info(info, ", ");
		} else {
			copy_info(info, " (");
		}
		copy_info(info, "%dbit)", 8 * (0x01 << tinfo->width));
	} else if (freq != 0) {
		copy_info(info, ")");
	}
	copy_info(info, "\n");
}

static void
ahc_dump_target_state(struct ahc_softc *ahc, struct info_str *info,
		      u_int our_id, char channel, u_int target_id,
		      u_int target_offset)
{
	struct	ahc_linux_target *targ;
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	int	lun;

	tinfo = ahc_fetch_transinfo(ahc, channel, our_id,
				    target_id, &tstate);
	copy_info(info, "Channel %c Target %d Negotiation Settings\n",
		  channel, target_id);
	copy_info(info, "\tUser: ");
	ahc_format_transinfo(info, &tinfo->user);
	targ = ahc->platform_data->targets[target_offset];
	if (targ == NULL)
		return;

	copy_info(info, "\tGoal: ");
	ahc_format_transinfo(info, &tinfo->goal);
	copy_info(info, "\tCurr: ");
	ahc_format_transinfo(info, &tinfo->curr);

	for (lun = 0; lun < AHC_NUM_LUNS; lun++) {
		struct ahc_linux_device *dev;

		dev = targ->devices[lun];

		if (dev == NULL)
			continue;

		ahc_dump_device_state(info, dev);
	}
}

static void
ahc_dump_device_state(struct info_str *info, struct ahc_linux_device *dev)
{
	copy_info(info, "\tChannel %c Target %d Lun %d Settings\n",
		  dev->target->channel + 'A', dev->target->target, dev->lun);

	copy_info(info, "\t\tCommands Queued %ld\n", dev->commands_issued);
	copy_info(info, "\t\tCommands Active %d\n", dev->active);
	copy_info(info, "\t\tCommand Openings %d\n", dev->openings);
	copy_info(info, "\t\tMax Tagged Openings %d\n", dev->maxtags);
	copy_info(info, "\t\tDevice Queue Frozen Count %d\n", dev->qfrozen);
}

/*
 * Return information to handle /proc support for the driver.
 */
int
ahc_linux_proc_info(char *buffer, char **start, off_t offset,
		  int length, int hostno, int inout)
{
	struct	ahc_softc *ahc;
	struct	info_str info;
	char	ahc_info[256];
	u_int	max_targ;
	u_int	i;

	TAILQ_FOREACH(ahc, &ahc_tailq, links) {
		if (ahc->platform_data->host->host_no == hostno)
			break;
	}

	if (ahc == NULL)
		return (-EINVAL);

	 /* Has data been written to the file? */ 
	if (inout == TRUE)
		return (-ENOSYS);

	if (start)
		*start = buffer;

	info.buffer	= buffer;
	info.length	= length;
	info.offset	= offset;
	info.pos	= 0;

	copy_info(&info, "Adaptec AIC7xxx driver version: %s\n",
		  AIC7XXX_DRIVER_VERSION);
	ahc_controller_info(ahc, ahc_info);
	copy_info(&info, "%s\n", ahc_info);

	max_targ = 15;
	if ((ahc->features & (AHC_WIDE|AHC_TWIN)) == 0)
		max_targ = 7;

	for (i = 0; i <= max_targ; i++) {
		u_int our_id;
		u_int target_id;
		char channel;

		channel = 'A';
		our_id = ahc->our_id;
		target_id = i;
		if (i > 7 && (ahc->features & AHC_TWIN) != 0) {
			channel = 'B';
			our_id = ahc->our_id_b;
			target_id = i % 8;
		}

		ahc_dump_target_state(ahc, &info, our_id,
				      channel, target_id, i);
	}
	return (info.pos > info.offset ? info.pos - info.offset : 0);
}
