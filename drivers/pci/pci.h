/* Functions internal to the PCI core code */

extern int pci_hotplug (struct device *dev, char **envp, int num_envp,
			 char *buffer, int buffer_size);
extern int pci_create_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_remove_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_cleanup_rom(struct pci_dev *dev);
extern int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
				  unsigned long size, unsigned long align,
				  unsigned long min, unsigned int type_mask,
				  void (*alignf)(void *, struct resource *,
					  	 unsigned long, unsigned long),
				  void *alignf_data);
/* PCI /proc functions */
#ifdef CONFIG_PROC_FS
extern int pci_proc_attach_device(struct pci_dev *dev);
extern int pci_proc_detach_device(struct pci_dev *dev);
extern int pci_proc_attach_bus(struct pci_bus *bus);
extern int pci_proc_detach_bus(struct pci_bus *bus);
#else
static inline int pci_proc_attach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_attach_bus(struct pci_bus *bus) { return 0; }
static inline int pci_proc_detach_bus(struct pci_bus *bus) { return 0; }
#endif

/* Functions for PCI Hotplug drivers to use */
extern struct pci_bus * pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr);
extern unsigned int pci_do_scan_bus(struct pci_bus *bus);
extern int pci_remove_device_safe(struct pci_dev *dev);
extern unsigned char pci_max_busnr(void);
extern unsigned char pci_bus_max_busnr(struct pci_bus *bus);
extern int pci_bus_find_capability (struct pci_bus *bus, unsigned int devfn, int cap);

struct pci_dev_wrapped {
	struct pci_dev	*dev;
	void		*data;
};

struct pci_bus_wrapped {
	struct pci_bus	*bus;
	void		*data;
};

struct pci_visit {
	int (* pre_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);
	int (* post_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);

	int (* pre_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* visit_pci_dev)		(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* post_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
};

extern int pci_visit_dev(struct pci_visit *fn,
			 struct pci_dev_wrapped *wrapped_dev,
			 struct pci_bus_wrapped *wrapped_parent);
extern void pci_remove_legacy_files(struct pci_bus *bus);

/* Lock for read/write access to pci device and bus lists */
extern spinlock_t pci_bus_lock;

#ifdef CONFIG_X86_IO_APIC
extern int pci_msi_quirk;
#else
#define pci_msi_quirk 0
#endif

extern int pcie_mch_quirk;
extern struct device_attribute pci_dev_attrs[];
extern struct class_device_attribute class_device_attr_cpuaffinity;

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *                        PCI device id structure
 * @id: single PCI device id structure to match
 * @dev: the PCI device structure to match against
 * 
 * Returns the matching pci_device_id structure or %NULL if there is no match.
 */
static inline const struct pci_device_id *
pci_match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

