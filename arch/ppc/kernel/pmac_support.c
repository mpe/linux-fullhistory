/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/reboot.h>
#include <linux/nvram.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/cuda.h>
#include <asm/system.h>
#include <asm/prom.h>

/*
 * Read and write the non-volatile RAM on PowerMacs.
 */
static int nvram_naddrs;
static volatile unsigned char *nvram_addr;
static volatile unsigned char *nvram_data;

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
	if (nvram_naddrs == 1) {
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
	} else if (nvram_naddrs == 2) {
		nvram_addr = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_data = ioremap(dp->addrs[1].address, dp->addrs[1].size);
	} else {
		printk(KERN_ERR "Don't know how to access NVRAM with %d addresses\n",
		       nvram_naddrs);
	}
}

unsigned char nvram_read_byte(int addr)
{
	switch (nvram_naddrs) {
	case 1:
		return nvram_data[(addr & 0x1fff) << 4];
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		return nvram_data[(addr & 0x1f) << 4];
	}
	return 0;
}

void nvram_write_byte(unsigned char val, int addr)
{
	switch (nvram_naddrs) {
	case 1:
		nvram_data[(addr & 0x1fff) << 4] = val;
		break;
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		nvram_data[(addr & 0x1f) << 4] = val;
		break;
	}
	eieio();
}
