#ifndef _PPC_INIT_H
#define _PPC_INIT_H

/* Throwing the initialization code and data out is not supported yet... */

#define	__init
#define __initdata
#define __initfunc(__arginit) __arginit
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA

#endif
