/*
 * $Id: kernelcapi.h,v 1.6 2000/03/03 15:50:42 calle Exp $
 * 
 * Kernel CAPI 2.0 Interface for Linux
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: kernelcapi.h,v $
 * Revision 1.6  2000/03/03 15:50:42  calle
 * - kernel CAPI:
 *   - Changed parameter "param" in capi_signal from __u32 to void *.
 *   - rewrote notifier handling in kcapi.c
 *   - new notifier NCCI_UP and NCCI_DOWN
 * - User CAPI:
 *   - /dev/capi20 is now a cloning device.
 *   - middleware extentions prepared.
 * - capidrv.c
 *   - locking of list operations and module count updates.
 *
 * Revision 1.5  2000/01/28 16:45:40  calle
 * new manufacturer command KCAPI_CMD_ADDCARD (generic addcard),
 * will search named driver and call the add_card function if one exist.
 *
 * Revision 1.4  1999/09/10 17:24:19  calle
 * Changes for proposed standard for CAPI2.0:
 * - AK148 "Linux Exention"
 *
 * Revision 1.3  1999/07/01 15:26:56  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 * Revision 1.2  1999/06/21 15:24:26  calle
 * extend information in /proc.
 *
 * Revision 1.1  1997/03/04 21:27:33  calle
 * First version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 * 
 */
#ifndef __KERNELCAPI_H__
#define __KERNELCAPI_H__

#define CAPI_MAXAPPL	20	/*
				   * maximum number of applications 
				 */
#define CAPI_MAXCONTR	10	/*
				   * maximum number of controller 
				 */
#define CAPI_MAXDATAWINDOW	8


typedef struct kcapi_flagdef {
	int contr;
	int flag;
} kcapi_flagdef;

typedef struct kcapi_carddef {
	char		driver[32];
	unsigned int	port;
	unsigned	irq;
	unsigned int	membase;
	int		cardnr;
} kcapi_carddef;

/* new ioctls >= 10 */
#define KCAPI_CMD_TRACE		10
#define KCAPI_CMD_ADDCARD	11	/* add card to named driver */

/* 
 * flag > 2 => trace also data
 * flag & 1 => show trace
 */
#define KCAPI_TRACE_OFF			0
#define KCAPI_TRACE_SHORT_NO_DATA	1
#define KCAPI_TRACE_FULL_NO_DATA	2
#define KCAPI_TRACE_SHORT		3
#define KCAPI_TRACE_FULL		4


#ifdef __KERNEL__

struct capi_interface {
	__u16 (*capi_isinstalled) (void);

	__u16 (*capi_register) (capi_register_params * rparam, __u16 * applidp);
	__u16 (*capi_release) (__u16 applid);
	__u16 (*capi_put_message) (__u16 applid, struct sk_buff * msg);
	__u16 (*capi_get_message) (__u16 applid, struct sk_buff ** msgp);
	__u16 (*capi_set_signal) (__u16 applid,
			      void (*signal) (__u16 applid, void *param),
				  void *param);
	__u16 (*capi_get_manufacturer) (__u32 contr, __u8 buf[CAPI_MANUFACTURER_LEN]);
	__u16 (*capi_get_version) (__u32 contr, struct capi_version * verp);
	 __u16(*capi_get_serial) (__u32 contr, __u8 serial[CAPI_SERIAL_LEN]);
	 __u16(*capi_get_profile) (__u32 contr, struct capi_profile * profp);

	/*
	 * to init controllers, data is always in user memory
	 */
	int (*capi_manufacturer) (unsigned int cmd, void *data);

};

struct capi_ncciinfo {
	__u16 applid;
	__u32 ncci;
};

#define	KCI_CONTRUP	0	/* struct capi_profile */
#define	KCI_CONTRDOWN	1	/* NULL */
#define	KCI_NCCIUP	2	/* struct capi_ncciinfo */
#define	KCI_NCCIDOWN	3	/* struct capi_ncciinfo */

struct capi_interface_user {
	char name[20];
	void (*callback) (unsigned int cmd, __u32 contr, void *data);
	/* internal */
	struct capi_interface_user *next;
};

struct capi_interface *attach_capi_interface(struct capi_interface_user *);
int detach_capi_interface(struct capi_interface_user *);


#define CAPI_NOERROR                      0x0000

#define CAPI_TOOMANYAPPLS		  0x1001
#define CAPI_LOGBLKSIZETOSMALL	          0x1002
#define CAPI_BUFFEXECEEDS64K 	          0x1003
#define CAPI_MSGBUFSIZETOOSMALL	          0x1004
#define CAPI_ANZLOGCONNNOTSUPPORTED	  0x1005
#define CAPI_REGRESERVED		  0x1006
#define CAPI_REGBUSY 		          0x1007
#define CAPI_REGOSRESOURCEERR	          0x1008
#define CAPI_REGNOTINSTALLED 	          0x1009
#define CAPI_REGCTRLERNOTSUPPORTEXTEQUIP  0x100a
#define CAPI_REGCTRLERONLYSUPPORTEXTEQUIP 0x100b

#define CAPI_ILLAPPNR		          0x1101
#define CAPI_ILLCMDORSUBCMDORMSGTOSMALL   0x1102
#define CAPI_SENDQUEUEFULL		  0x1103
#define CAPI_RECEIVEQUEUEEMPTY	          0x1104
#define CAPI_RECEIVEOVERFLOW 	          0x1105
#define CAPI_UNKNOWNNOTPAR		  0x1106
#define CAPI_MSGBUSY 		          0x1107
#define CAPI_MSGOSRESOURCEERR	          0x1108
#define CAPI_MSGNOTINSTALLED 	          0x1109
#define CAPI_MSGCTRLERNOTSUPPORTEXTEQUIP  0x110a
#define CAPI_MSGCTRLERONLYSUPPORTEXTEQUIP 0x110b


#endif				/* __KERNEL__ */

#endif				/* __KERNELCAPI_H__ */
