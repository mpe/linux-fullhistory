#ifndef __MOUSE_H
#define __MOUSE_H

struct mouse {
	int minor;
	const char *name;
	struct file_operations *fops;
	struct mouse * next, * prev;
};

extern int mouse_register(struct mouse * mouse);
extern int mouse_deregister(struct mouse * mouse);

#endif
