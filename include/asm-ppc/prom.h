/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

typedef void *phandle;
typedef void *ihandle;

extern ihandle prom_stdout;
extern ihandle prom_chosen;
extern phandle cpu_node;
extern char prom_display_path[];

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
	struct	reg_property *addrs;
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

/* Prototypes */
void abort(void);
void prom_exit(void);
void *call_prom(const char *service, int nargs, int nret, ...);
void prom_print(const char *msg);
void prom_init(char *params, int unused, void (*)(void *));
void set_prom_callback(void);
unsigned long copy_device_tree(unsigned long, unsigned long);
struct device_node *find_devices(const char *name);
struct device_node *find_type_devices(const char *type);
struct device_node *find_path_device(const char *path);
unsigned char *get_property(struct device_node *node, const char *name,
	int *lenp);
void print_properties(struct device_node *node);
