#ifndef _SPARC_UNISTD_H
#define _SPARC_UNISTD_H

/*
 * System calls under the Sparc.
 *
 * This is work in progress.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name (type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
} 

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5) \
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
}

#endif /* _ALPHA_UNISTD_H */
