#ifndef _LINUX_MISCDEVICE_H
#define _LINUX_MISCDEVICE_H

#define BUSMOUSE_MINOR 0
#define PSMOUSE_MINOR  1
#define MS_BUSMOUSE_MINOR 2
#define ATIXL_BUSMOUSE_MINOR 3
#define AMIGAMOUSE_MINOR 4
#define ATARIMOUSE_MINOR 5
#define SUN_MOUSE_MINOR 6
#define MISC_DYNAMIC_MINOR 255

extern int misc_init(void);

struct miscdevice 
{
	int minor;
	const char *name;
	struct file_operations *fops;
	struct miscdevice * next, * prev;
};

extern int misc_register(struct miscdevice * misc);
extern int misc_deregister(struct miscdevice * misc);

#endif
