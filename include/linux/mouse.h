#if !defined _LINUX_MOUSE_H
#define _LINUX_MOUSE_H

#define BUSMOUSE_MINOR 0
#define PSMOUSE_MINOR  1
#define MS_BUSMOUSE_MINOR 2

long mouse_init(long);

#endif
