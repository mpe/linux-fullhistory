#ifndef _M68K_PARAM_H
#define _M68K_PARAM_H

#ifndef HZ
#define HZ 100
#endif

#ifndef CONFIG_SUN3
#define EXEC_PAGESIZE	4096
#else
#define EXEC_PAGESIZE	8192
#endif

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _M68K_PARAM_H */
