/* $Id: mod.c,v 1.3 1997/02/14 12:23:31 fritz Exp $
 *
 * $Log: mod.c,v $
 * Revision 1.3  1997/02/14 12:23:31  fritz
 * Added support for new insmod parameter handling.
 *
 * Revision 1.2  1997/02/10 11:45:14  fritz
 * More changes for Kernel 2.1.X compatibility.
 *
 * Revision 1.1  1996/04/13 10:27:02  fritz
 * Initial revision
 *
 *
 */
#include "teles.h"

extern struct IsdnCard cards[];
extern char   *teles_id;

int             nrcards;

typedef struct {
	byte           *membase;
	int             interrupt;
	unsigned int    iobase;
	unsigned int    protocol;
} io_type;

io_type         io[] =
{
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
};

#ifdef MODULE
#if (LINUX_VERSION_CODE > 0x020111)
MODULE_PARM(io, "1-64i");
MODULE_PARM(teles_id, "s");
#endif
#endif

void
teles_mod_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

void
teles_mod_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

#ifdef MODULE
#define teles_init init_module
#else
void teles_setup(char *str, int *ints)
{
        int  i, j, argc;
	static char sid[20];

        argc = ints[0];
        i = 0;
        j = 1;
        while (argc && (i<16)) {
                if (argc) {
                        io[i].iobase    = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].interrupt = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].membase   = (byte *)ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].protocol  = ints[j];
                        j++; argc--;
                }
                i++;
        }
	if (strlen(str)) {
		strcpy(sid,str);
		teles_id = sid;
	}
}
#endif

int
teles_init(void)
{
	int             i;

	nrcards = 0;
	for (i = 0; i < 16; i++) {
		if (io[i].protocol) {
			cards[i].membase   = io[i].membase;
			cards[i].interrupt = io[i].interrupt;
			cards[i].iobase    = io[i].iobase;
			cards[i].protocol  = io[i].protocol;
		}
	}
	for (i = 0; i < 16; i++)
                if (cards[i].protocol)
                        nrcards++;
	printk(KERN_DEBUG "teles: Total %d card%s defined\n",
               nrcards, (nrcards > 1) ? "s" : "");
	if (teles_inithardware()) {
                /* Install only, if at least one card found */
                Isdnl2New();
                TeiNew();
                CallcNew();
                ll_init();

		/* No symbols to export, hide all symbols */

#ifdef MODULE
#if (LINUX_VERSION_CODE < 0x020111)
			register_symtab(NULL);
#else
			EXPORT_NO_SYMBOLS;
#endif
                printk(KERN_NOTICE "Teles module installed\n");
#endif
                return (0);
        } else
                return -EIO;
}

#ifdef MODULE
void
cleanup_module(void)
{

	ll_stop();
	TeiFree();
	Isdnl2Free();
	CallcFree();
	teles_closehardware();
	ll_unload();
	printk(KERN_NOTICE "Teles module removed\n");

}
#endif
