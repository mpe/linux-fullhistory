/*
 * drivers/pcmcia/pci_socket.h
 *
 * (C) Copyright 1999 Linus Torvalds
 */

#ifndef __PCI_SOCKET_H
#define __PCI_SOCKET_H

struct pci_socket_ops;

typedef struct pci_socket {
	struct pci_dev *dev;
	int cb_irq, io_irq;
	void *base;
	void (*handler)(void *, unsigned int);
	void *info;
	struct pci_socket_ops *op;
	socket_cap_t cap;
	wait_queue_head_t wait;
	unsigned int events;

	/* A few words of private data for the low-level driver.. */
	unsigned int private[8];
} pci_socket_t;

struct pci_socket_ops {
	int (*open)(struct pci_socket *);
	void (*close)(struct pci_socket *);

	int (*init)(struct pci_socket *);
	int (*suspend)(struct pci_socket *);
	int (*get_status)(struct pci_socket *, unsigned int *);
	int (*get_socket)(struct pci_socket *, socket_state_t *);
	int (*set_socket)(struct pci_socket *, socket_state_t *);
	int (*get_io_map)(struct pci_socket *, struct pccard_io_map *);
	int (*set_io_map)(struct pci_socket *, struct pccard_io_map *);
	int (*get_mem_map)(struct pci_socket *, struct pccard_mem_map *);
	int (*set_mem_map)(struct pci_socket *, struct pccard_mem_map *);
	void (*proc_setup)(struct pci_socket *, struct proc_dir_entry *base);
};

extern struct pci_socket_ops yenta_operations;
extern struct pci_socket_ops ricoh_operations;

#endif
