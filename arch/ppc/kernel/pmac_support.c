/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/reboot.h>
#include <linux/nvram.h>
#include <asm/init.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/adb.h>
#include <asm/pmu.h>

/*
 * Read and write the non-volatile RAM on PowerMacs and CHRP machines.
 */
static int nvram_naddrs;
static volatile unsigned char *nvram_addr;
static volatile unsigned char *nvram_data;
static int nvram_mult;

#define NVRAM_SIZE	0x2000	/* 8kB of non-volatile RAM */


__init
void pmac_nvram_init(void)
{
	struct device_node *dp;

	dp = find_devices("nvram");
	if (dp == NULL) {
		printk(KERN_ERR "Can't find NVRAM device\n");
		nvram_naddrs = 0;
		return;
	}
	nvram_naddrs = dp->n_addrs;
	if (_machine == _MACH_chrp && nvram_naddrs == 1) {
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_mult = 1;
	} else if (nvram_naddrs == 1) {
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_mult = (dp->addrs[0].size + NVRAM_SIZE - 1) / NVRAM_SIZE;
	} else if (nvram_naddrs == 2) {
		nvram_addr = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_data = ioremap(dp->addrs[1].address, dp->addrs[1].size);
	} else if (nvram_naddrs == 0 && adb_hardware == ADB_VIAPMU) {
		nvram_naddrs = -1;
	} else {
		printk(KERN_ERR "Don't know how to access NVRAM with %d addresses\n",
		       nvram_naddrs);
	}
}

unsigned char nvram_read_byte(int addr)
{
	struct adb_request req;

	switch (nvram_naddrs) {
	case -1:
		if (pmu_request(&req, NULL, 3, PMU_READ_NVRAM,
				(addr >> 8) & 0xff, addr & 0xff))
			break;
		while (!req.complete)
			pmu_poll();
		return req.reply[1];
	case 1:
		return nvram_data[(addr & (NVRAM_SIZE - 1)) * nvram_mult];
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		return nvram_data[(addr & 0x1f) << 4];
	}
	return 0;
}

void nvram_write_byte(unsigned char val, int addr)
{
	struct adb_request req;

	switch (nvram_naddrs) {
	case -1:
		if (pmu_request(&req, NULL, 4, PMU_WRITE_NVRAM,
				(addr >> 8) & 0xff, addr & 0xff, val))
			break;
		while (!req.complete)
			pmu_poll();
		break;
	case 1:
		nvram_data[(addr & (NVRAM_SIZE - 1)) * nvram_mult] = val;
		break;
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		nvram_data[(addr & 0x1f) << 4] = val;
		break;
	}
	eieio();
}
