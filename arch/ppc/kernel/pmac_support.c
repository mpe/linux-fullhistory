/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/cuda.h>
#include <asm/system.h>
#include <asm/prom.h>

void hard_reset_now(void)
{
	struct cuda_request req;

	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
	for (;;)
		cuda_poll();
}

void poweroff_now(void)
{
	struct cuda_request req;

	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
	for (;;)
		cuda_poll();
}

/*
 * Read and write the non-volatile RAM on PowerMacs.
 */
static int nvram_naddrs;
static volatile unsigned char *nvram_addr;
static volatile unsigned char *nvram_data;

void nvram_init(void)
{
	struct device_node *dp;

	dp = find_devices("nvram");
	if (dp == NULL)
		panic("Can't find NVRAM device");
	nvram_naddrs = dp->n_addrs;
	if (nvram_naddrs == 1)
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
	else if (nvram_naddrs == 2) {
		nvram_addr = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_data = ioremap(dp->addrs[1].address, dp->addrs[1].size);
	} else {
		printk("Found %d addresses for NVRAM\n", nvram_naddrs);
		panic("don't understand NVRAM");
	}
}

int nvram_readb(int addr)
{
	switch (nvram_naddrs) {
	case 1:
		return nvram_data[(addr & 0x1fff) << 4];
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		return nvram_data[(addr & 0x1f) << 4];
	}
	return -1;
}

void nvram_writeb(int addr, int val)
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
