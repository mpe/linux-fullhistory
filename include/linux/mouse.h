#ifndef _LINUX_MOUSE_H
#define _LINUX_MOUSE_H

#define BUSMOUSE_MINOR 0
#define PSMOUSE_MINOR  1
#define MS_BUSMOUSE_MINOR 2
#define ATIXL_BUSMOUSE_MINOR 3

extern int mouse_init(void);

struct mouse {
	int minor;
	const char *name;
	struct file_operations *fops;
	struct mouse * next, * prev;
};

extern int mouse_register(struct mouse * mouse);
extern int mouse_deregister(struct mouse * mouse);

#endif
