struct hw_pci {
	void (*init)(void);
	unsigned long	io_start;
	unsigned long	mem_start;
	u8   (*swizzle)(struct pci_dev *dev, u8 *pin);
	int  (*map_irq)(struct pci_dev *dev, u8 slot, u8 pin);
};

void __init dc21285_init(void);
void __init plx90x0_init(void);
