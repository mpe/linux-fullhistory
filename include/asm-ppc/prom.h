/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

typedef void *phandle;
typedef void *ihandle;

#ifndef FB_MAX
#define FB_MAX	8	/* I don't want to include the whole console stuff */
#endif
extern char *prom_display_paths[FB_MAX];
extern unsigned int prom_num_displays;

struct address_range {
	unsigned int space;
	unsigned int address;
	unsigned int size;
};

struct reg_property {
	unsigned int address;
	unsigned int size;
};

struct translation_property {
	unsigned int virt;
	unsigned int size;
	unsigned int phys;
	unsigned int flags;
};

struct property {
	char	*name;
	int	length;
	unsigned char *value;
	struct property *next;
};

struct device_node {
	char	*name;
	char	*type;
	phandle	node;
	int	n_addrs;
	struct	address_range *addrs;
	int	n_intrs;
	int	*intrs;
	char	*full_name;
	struct	property *properties;
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	struct	device_node *next;	/* next device of same type */
	struct	device_node *allnext;	/* next in list of all nodes */
};

struct prom_args;
typedef void (*prom_entry)(struct prom_args *);

/* Prototypes */
void abort(void);
void prom_init(int, int, prom_entry);
void finish_device_tree(void);
struct device_node *find_devices(const char *name);
struct device_node *find_type_devices(const char *type);
struct device_node *find_path_device(const char *path);
struct device_node *find_compatible_devices(const char *type,
					    const char *compat);
struct device_node *find_phandle(phandle);
unsigned char *get_property(struct device_node *node, const char *name,
	int *lenp);
void print_properties(struct device_node *node);
int call_rtas(const char *service, int nargs, int nret,
	      unsigned long *outputs, ...);
