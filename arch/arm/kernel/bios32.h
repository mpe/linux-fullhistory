#define MAX_NR_BUS	2

struct arm_bus_sysdata {
	/*
	 * bitmask of features we can turn.
	 * See PCI command register for more info.
	 */
	u16		features;
	/*
	 * Maximum devsel for this bus.
	 */
	u16		maxdevsel;
};

struct arm_pci_sysdata {
	struct arm_bus_sysdata bus[MAX_NR_BUS];
};

struct hw_pci {
	void		(*init)(void);
	unsigned long	io_start;
	unsigned long	mem_start;
	u8		(*swizzle)(struct pci_dev *dev, u8 *pin);
	int		(*map_irq)(struct pci_dev *dev, u8 slot, u8 pin);
};

void __init dc21285_init(void);
void __init plx90x0_init(void);
