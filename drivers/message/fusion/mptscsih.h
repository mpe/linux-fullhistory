/*
 *  linux/drivers/message/fusion/mptscsih.h
 *      High performance SCSI / Fibre Channel SCSI Host device driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      (see also mptbase.c)
 *
 *  Copyright (c) 1999-2003 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:netscape.net)
 *  (mailto:mpt_linux_developer@lsil.com)
 *
 *  $Id: mptscsih.h,v 1.21 2002/12/03 21:26:35 pdelaney Exp $
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef SCSIHOST_H_INCLUDED
#define SCSIHOST_H_INCLUDED

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SCSI Public stuff...
 */

/*
 *	Try to keep these at 2^N-1
 */
#define MPT_FC_CAN_QUEUE	127
#if defined MPT_SCSI_USE_NEW_EH
	#define MPT_SCSI_CAN_QUEUE	127
#else
	#define MPT_SCSI_CAN_QUEUE	63
#endif

#define MPT_SCSI_CMD_PER_DEV_HIGH	31
#define MPT_SCSI_CMD_PER_DEV_LOW	7

#define MPT_SCSI_CMD_PER_LUN		7

#define MPT_SCSI_MAX_SECTORS    8192

/*
 * Set the MAX_SGE value based on user input.
 */
#ifdef  CONFIG_FUSION_MAX_SGE
#if     CONFIG_FUSION_MAX_SGE  < 16
#define MPT_SCSI_SG_DEPTH	16
#elif   CONFIG_FUSION_MAX_SGE  > 128
#define MPT_SCSI_SG_DEPTH	128
#else
#define MPT_SCSI_SG_DEPTH	CONFIG_FUSION_MAX_SGE
#endif
#else
#define MPT_SCSI_SG_DEPTH	40
#endif

/* To disable domain validation, uncomment the
 * following line. No effect for FC devices.
 * For SCSI devices, driver will negotiate to
 * NVRAM settings (if available) or to maximum adapter
 * capabilities.
 */
/* #define MPTSCSIH_DISABLE_DOMAIN_VALIDATION */


/* SCSI driver setup structure. Settings can be overridden
 * by command line options.
 */
#define MPTSCSIH_DOMAIN_VALIDATION      1
#define MPTSCSIH_MAX_WIDTH              1
#define MPTSCSIH_MIN_SYNC               0x08

struct mptscsih_driver_setup
{
        u8      dv;
        u8      max_width;
        u8      min_sync_fac;
};


#define MPTSCSIH_DRIVER_SETUP                   \
{                                               \
        MPTSCSIH_DOMAIN_VALIDATION,             \
        MPTSCSIH_MAX_WIDTH,                     \
        MPTSCSIH_MIN_SYNC,                      \
}



/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	Various bits and pieces broke within the lk-2.4.0-testN series:-(
 *	So here are various HACKS to work around them.
 */

/*
 *	Conditionalizing with "#ifdef MODULE/#endif" around:
 *		static Scsi_Host_Template driver_template = XX;
 *		#include <../../scsi/scsi_module.c>
 *	lines was REMOVED @ lk-2.4.0-test9
 *	Issue discovered 20001213 by: sshirron
 */
#define MPT_SCSIHOST_NEED_ENTRY_EXIT_HOOKUPS			1

/*
 *	tq_scheduler disappeared @ lk-2.4.0-test12
 *	(right when <linux/sched.h> newly defined TQ_ACTIVE)
 *	tq_struct reworked in 2.5.41. Include workqueue.h.
 */
#	include <linux/sched.h>
#	include <linux/workqueue.h>
#define SCHEDULE_TASK(x)		\
	if (schedule_work(x) == 0) {	\
		/*MOD_DEC_USE_COUNT*/;	\
	}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#define x_scsi_detect		mptscsih_detect
#define x_scsi_release		mptscsih_release
#define x_scsi_info		mptscsih_info
#define x_scsi_queuecommand	mptscsih_qcmd
#define x_scsi_abort		mptscsih_abort
#define x_scsi_bus_reset	mptscsih_bus_reset
#define x_scsi_dev_reset	mptscsih_dev_reset
#define x_scsi_host_reset	mptscsih_host_reset
#define x_scsi_bios_param	mptscsih_bios_param

#define x_scsi_taskmgmt_bh	mptscsih_taskmgmt_bh
#define x_scsi_old_abort	mptscsih_old_abort
#define x_scsi_old_reset	mptscsih_old_reset
#define x_scsi_slave_alloc	mptscsih_slave_alloc
#define x_scsi_slave_configure	mptscsih_slave_configure
#define x_scsi_slave_destroy	mptscsih_slave_destroy
#define x_scsi_proc_info	mptscsih_proc_info

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	MPT SCSI Host / Initiator decls...
 */
extern	int		 x_scsi_detect(Scsi_Host_Template *);
extern	int		 x_scsi_release(struct Scsi_Host *host);
extern	const char	*x_scsi_info(struct Scsi_Host *);
extern	int		 x_scsi_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern	int		 x_scsi_abort(Scsi_Cmnd *);
extern	int		 x_scsi_bus_reset(Scsi_Cmnd *);
extern	int		 x_scsi_dev_reset(Scsi_Cmnd *);
extern	int		 x_scsi_host_reset(Scsi_Cmnd *);
extern int		 x_scsi_bios_param(struct scsi_device * sdev, struct block_device *bdev,
				sector_t capacity, int *ip);
extern	void		 x_scsi_taskmgmt_bh(void *);
extern	int		 x_scsi_slave_alloc(Scsi_Device *);
extern	int		 x_scsi_slave_configure(Scsi_Device *);
extern	void		 x_scsi_slave_destroy(Scsi_Device *);
extern	int		 x_scsi_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*  include/scsi/scsi.h may not be quite complete...  */
#ifndef RESERVE_10
#define RESERVE_10		0x56
#endif
#ifndef RELEASE_10
#define RELEASE_10		0x57
#endif
#ifndef PERSISTENT_RESERVE_IN
#define PERSISTENT_RESERVE_IN	0x5e
#endif
#ifndef PERSISTENT_RESERVE_OUT
#define PERSISTENT_RESERVE_OUT	0x5f
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#endif

