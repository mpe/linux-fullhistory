/*
 * prototypes for dummy prom_* routines
 */

extern int prom_getintdefault(int node, char *property, int defval);
extern int prom_getbool(int node, char *prop);
extern void prom_printf(char *fmt, ...);
extern void prom_halt(void) __attribute__ ((noreturn));
