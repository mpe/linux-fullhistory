/*
 * drivers/pcmcia/pci_socket.h
 *
 * (C) Copyright 1999 Linus Torvalds
 */

struct pci_socket_ops;

typedef struct pci_socket {
	struct pci_dev *dev;
	int cb_irq, io_irq;
	void *base;
	void (*handler)(void *, unsigned int);
	void *info;
	struct pci_socket_ops *op;
	socket_cap_t cap;
} pci_socket_t;

struct pci_socket_ops {
	int (*open)(struct pci_socket *);
	void (*close)(struct pci_socket *);

	int (*init)(struct pci_socket *);
	int (*suspend)(struct pci_socket *);
	int (*inquire)(struct pci_socket *, socket_cap_t *cap);
	int (*get_status)(struct pci_socket *, unsigned int *);
	int (*get_socket)(struct pci_socket *, socket_state_t *);
	int (*set_socket)(struct pci_socket *, socket_state_t *);
	int (*get_io_map)(struct pci_socket *, struct pccard_io_map *);
	int (*set_io_map)(struct pci_socket *, struct pccard_io_map *);
	int (*get_mem_map)(struct pci_socket *, struct pccard_mem_map *);
	int (*set_mem_map)(struct pci_socket *, struct pccard_mem_map *);
	int (*get_bridge)(struct pci_socket *, struct cb_bridge_map *);
	int (*set_bridge)(struct pci_socket *, struct cb_bridge_map *);
	void (*proc_setup)(struct pci_socket *, struct proc_dir_entry *base);
};

extern struct pci_socket_ops yenta_operations;
extern struct pci_socket_ops ricoh_operations;

