#ifndef _LINUX_TASKS_H
#define _LINUX_TASKS_H

/*
 * This is the maximum nr of tasks - change it if you need to
 */
#define NR_TASKS	512

#define MAX_TASKS_PER_USER (NR_TASKS/2)
#define MIN_TASKS_LEFT_FOR_ROOT 4

#endif
