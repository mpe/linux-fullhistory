#ifndef __LINUX_FIREWALL_H
#define __LINUX_FIREWALL_H

/*
 *	Definitions for loadable firewall modules
 */

#define FW_BLOCK	0
#define FW_ACCEPT	1
#define FW_REJECT	(-1)
#define FW_REDIRECT	2
#define FW_MASQUERADE	3
#define FW_SKIP		4

struct firewall_ops
{
	struct firewall_ops *next;
	int (*fw_forward)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg);
	int (*fw_input)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg);
	int (*fw_output)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg);
	/* Data falling in the second 486 cache line isn't used directly
	   during a firewall call and scan, only by insert/delete and other
	   unusual cases
	 */
	int fw_pf;		/* Protocol family 			*/	
	int fw_priority;	/* Priority of chosen firewalls 	*/
};

#ifdef __KERNEL__
extern int register_firewall(int pf, struct firewall_ops *fw);
extern int unregister_firewall(int pf, struct firewall_ops *fw);
extern int call_fw_firewall(int pf, struct device *dev, void *phdr, void *arg);
extern int call_in_firewall(int pf, struct device *dev, void *phdr, void *arg);
extern int call_out_firewall(int pf, struct device *dev, void *phdr, void *arg);
extern void fwchain_init(void);
#endif

#endif
