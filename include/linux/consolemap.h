/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

extern int hashtable_contents_valid;
extern unsigned char inverse_translate(int glyph);
extern unsigned short *set_translate(int m);
extern int conv_uni_to_pc(long ucs);
