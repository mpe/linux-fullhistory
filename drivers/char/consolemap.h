/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#define NORM_MAP 0
#define GRAF_MAP 1
#define NULL_MAP 2
#define USER_MAP 3

extern int hashtable_contents_valid;
extern unsigned char inverse_translate(unsigned char c);
extern unsigned char *set_translate(int m);
