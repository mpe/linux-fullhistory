/* Rename a few functions. */
#define amiga_request_irq request_irq
#define amiga_free_irq free_irq

#include "../../m68k/amiga/amiints.c"
