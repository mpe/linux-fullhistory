#ifndef _ASM_IA64_SN_SN_SAL_H
#define _ASM_IA64_SN_SN_SAL_H

/*
 * System Abstraction Layer definitions for IA64
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */


#include <linux/config.h>
#include <asm/sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/arch.h>


// SGI Specific Calls
#define  SN_SAL_POD_MODE                           0x02000001
#define  SN_SAL_SYSTEM_RESET                       0x02000002
#define  SN_SAL_PROBE                              0x02000003
#define  SN_SAL_GET_MASTER_NASID                   0x02000004
#define	 SN_SAL_GET_KLCONFIG_ADDR		   0x02000005
#define  SN_SAL_LOG_CE				   0x02000006
#define  SN_SAL_REGISTER_CE			   0x02000007
#define  SN_SAL_GET_PARTITION_ADDR		   0x02000009
#define  SN_SAL_PRINT_ERROR			   0x02000012
#define  SN_SAL_CONSOLE_PUTC                       0x02000021
#define  SN_SAL_CONSOLE_GETC                       0x02000022
#define  SN_SAL_CONSOLE_PUTS                       0x02000023
#define  SN_SAL_CONSOLE_GETS                       0x02000024
#define  SN_SAL_CONSOLE_GETS_TIMEOUT               0x02000025
#define  SN_SAL_CONSOLE_POLL                       0x02000026
#define  SN_SAL_CONSOLE_INTR                       0x02000027
#define  SN_SAL_CONSOLE_PUTB			   0x02000028
#define  SN_SAL_SYSCTL_MODID_GET	           0x02000031
#define  SN_SAL_SYSCTL_GET                         0x02000032
#define  SN_SAL_SYSCTL_IOBRICK_MODULE_GET          0x02000033
#define  SN_SAL_SYSCTL_IO_PORTSPEED_GET            0x02000035
#define  SN_SAL_SYSCTL_SLAB_GET                    0x02000036
#define  SN_SAL_BUS_CONFIG		   	   0x02000037
#define  SN_SAL_SYS_SERIAL_GET			   0x02000038
#define  SN_SAL_PARTITION_SERIAL_GET		   0x02000039
#define  SN_SAL_SYSCTL_PARTITION_GET		   0x0200003a
#define  SN_SAL_SYSTEM_POWER_DOWN		   0x0200003b
#define  SN_SAL_GET_MASTER_BASEIO_NASID		   0x0200003c


/*
 * Service-specific constants
 */
#define SAL_CONSOLE_INTR_IN     0       /* manipulate input interrupts */
#define SAL_CONSOLE_INTR_OUT    1       /* manipulate output low-water
                                         * interrupts
                                         */
#define SAL_CONSOLE_INTR_OFF    0       /* turn the interrupt off */
#define SAL_CONSOLE_INTR_ON     1       /* turn the interrupt on */


/*
 * SN_SAL_GET_PARTITION_ADDR return constants
 */
#define SALRET_MORE_PASSES	1
#define SALRET_OK		0
#define SALRET_INVALID_ARG	-2
#define SALRET_ERROR		-3


/**
 * sn_sal_rev_major - get the major SGI SAL revision number
 *
 * The SGI PROM stores its version in sal_[ab]_rev_(major|minor).
 * This routine simply extracts the major value from the
 * @ia64_sal_systab structure constructed by ia64_sal_init().
 */
static inline int
sn_sal_rev_major(void)
{
	struct ia64_sal_systab *systab = efi.sal_systab;

	return (int)systab->sal_b_rev_major;
}

/**
 * sn_sal_rev_minor - get the minor SGI SAL revision number
 *
 * The SGI PROM stores its version in sal_[ab]_rev_(major|minor).
 * This routine simply extracts the minor value from the
 * @ia64_sal_systab structure constructed by ia64_sal_init().
 */
static inline int
sn_sal_rev_minor(void)
{
	struct ia64_sal_systab *systab = efi.sal_systab;
	
	return (int)systab->sal_b_rev_minor;
}

/*
 * Specify the minimum PROM revsion required for this kernel.
 * Note that they're stored in hex format...
 */
#ifdef CONFIG_IA64_SGI_SN1
#define SN_SAL_MIN_MAJOR	0x0
#define SN_SAL_MIN_MINOR	0x03 /* SN1 PROMs are stuck at rev 0.03 */
#elif defined(CONFIG_IA64_SGI_SN2)
#define SN_SAL_MIN_MAJOR	0x0
#define SN_SAL_MIN_MINOR	0x11
#else
#error "must specify which PROM revisions this kernel needs"
#endif /* CONFIG_IA64_SGI_SN1 */

u64 ia64_sn_probe_io_slot(long paddr, long size, void *data_ptr);

/*
 * Returns the master console nasid, if the call fails, return an illegal
 * value.
 */
static inline u64
ia64_sn_get_console_nasid(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	SAL_CALL(ret_stuff, SN_SAL_GET_MASTER_NASID, 0, 0, 0, 0, 0, 0, 0);

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	/* Master console nasid is in 'v0' */
	return ret_stuff.v0;
}

/*
 * Returns the master baseio nasid, if the call fails, return an illegal
 * value.
 */
static inline u64
ia64_sn_get_master_baseio_nasid(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	SAL_CALL(ret_stuff, SN_SAL_GET_MASTER_BASEIO_NASID, 0, 0, 0, 0, 0, 0, 0);

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	/* Master baseio nasid is in 'v0' */
	return ret_stuff.v0;
}

static inline u64
ia64_sn_get_klconfig_addr(nasid_t nasid)
{
	struct ia64_sal_retval ret_stuff;
	extern u64 klgraph_addr[];
	int cnodeid;

	cnodeid = 0 /* nasid_to_cnodeid(nasid) */;
	if (klgraph_addr[cnodeid] == 0) {
		ret_stuff.status = (uint64_t)0;
		ret_stuff.v0 = (uint64_t)0;
		ret_stuff.v1 = (uint64_t)0;
		ret_stuff.v2 = (uint64_t)0;
		SAL_CALL(ret_stuff, SN_SAL_GET_KLCONFIG_ADDR, (u64)nasid, 0, 0, 0, 0, 0, 0);

		/*
	 	* We should panic if a valid cnode nasid does not produce
	 	* a klconfig address.
	 	*/
		if (ret_stuff.status != 0) {
			panic("ia64_sn_get_klconfig_addr: Returned error %lx\n", ret_stuff.status);
		}

		klgraph_addr[cnodeid] = ret_stuff.v0;
	}
	return(klgraph_addr[cnodeid]);
}

/*
 * Returns the next console character.
 */
static inline u64
ia64_sn_console_getc(int *ch)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_CONSOLE_GETC, 0, 0, 0, 0, 0, 0, 0);

	/* character is in 'v0' */
	*ch = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/*
 * Sends the given character to the console.
 */
static inline u64
ia64_sn_console_putc(char ch)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_CONSOLE_PUTC, (uint64_t)ch, 0, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Sends the given buffer to the console.
 */
static inline u64
ia64_sn_console_putb(char *buf, int len)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_CONSOLE_PUTB, (uint64_t)buf, (uint64_t)len, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Print a platform error record
 */
static inline u64
ia64_sn_plat_specific_err_print(int (*hook)(const char*, ...), char *rec)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_PRINT_ERROR, (uint64_t)hook, (uint64_t)rec, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Check for Platform errors
 */
static inline u64
ia64_sn_plat_cpei_handler(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_LOG_CE, 0, 0, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Checks for console input.
 */
static inline u64
ia64_sn_console_check(int *result)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	__SAL_CALL(ret_stuff, SN_SAL_CONSOLE_POLL, 0, 0, 0, 0, 0, 0, 0);

	/* result is in 'v0' */
	*result = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/*
 * Returns the iobrick module Id
 */
static inline u64
ia64_sn_sysctl_iobrick_module_get(nasid_t nasid, int *result)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	SAL_CALL(ret_stuff, SN_SAL_SYSCTL_IOBRICK_MODULE_GET, nasid, 0, 0, 0, 0, 0, 0);

	/* result is in 'v0' */
	*result = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/**
 * ia64_sn_pod_mode - call the SN_SAL_POD_MODE function
 *
 * SN_SAL_POD_MODE actually takes an argument, but it's always
 * 0 when we call it from the kernel, so we don't have to expose
 * it to the caller.
 */
static inline u64
ia64_sn_pod_mode(void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SN_SAL_POD_MODE, 0, 0, 0, 0, 0, 0, 0);
	if (isrv.status)
		return 0;
	return isrv.v0;
}

/*
 * Retrieve the system serial number as an ASCII string.
 */
static inline u64
ia64_sn_sys_serial_get(char *buf)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_SYS_SERIAL_GET, buf, 0, 0, 0, 0, 0, 0);
	return ret_stuff.status;
}

extern char sn_system_serial_number_string[];
extern u64 sn_partition_serial_number;

static inline char *
sn_system_serial_number(void) {
	if (sn_system_serial_number_string[0]) {
		return(sn_system_serial_number_string);
	} else {
		ia64_sn_sys_serial_get(sn_system_serial_number_string);
		return(sn_system_serial_number_string);
	}
}
	

/*
 * Returns a unique id number for this system and partition (suitable for
 * use with license managers), based in part on the system serial number.
 */
static inline u64
ia64_sn_partition_serial_get(void)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_PARTITION_SERIAL_GET, 0, 0, 0, 0, 0, 0, 0);
	if (ret_stuff.status != 0)
	    return 0;
	return ret_stuff.v0;
}

static inline u64
sn_partition_serial_number_val(void) {
	if (sn_partition_serial_number) {
		return(sn_partition_serial_number);
	} else {
		return(sn_partition_serial_number = ia64_sn_partition_serial_get());
	}
}

/*
 * Returns the partition id of the nasid passed in as an argument,
 * or INVALID_PARTID if the partition id cannot be retrieved.
 */
static inline partid_t
ia64_sn_sysctl_partition_get(nasid_t nasid)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_SYSCTL_PARTITION_GET, nasid,
		 0, 0, 0, 0, 0, 0);
	if (ret_stuff.status != 0)
	    return INVALID_PARTID;
	return ((partid_t)ret_stuff.v0);
}

#ifdef CONFIG_IA64_SGI_SN2
/*
 * Returns the partition id of the current processor.
 */

extern partid_t sn_partid;

static inline partid_t
sn_local_partid(void) {
	if (sn_partid < 0) {
		return (sn_partid = ia64_sn_sysctl_partition_get(cpuid_to_nasid(smp_processor_id())));
	} else {
		return sn_partid;
	}
}

#endif /* CONFIG_IA64_SGI_SN2 */

/*
 * Turns off system power.
 */
static inline void
ia64_sn_power_down(void)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_SYSTEM_POWER_DOWN, 0, 0, 0, 0, 0, 0, 0);
	while(1);
	/* never returns */
}


#endif /* _ASM_IA64_SN_SN_SAL_H */
