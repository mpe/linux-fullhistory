#ifndef _ASMAXP_PARAM_H
#define _ASMAXP_PARAM_H

#include <linux/config.h>

#ifndef HZ
# if defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB66P) || \
     defined(CONFIG_ALPHA_EB64P)
#  define HZ	 977	/* Evaluation Boards seem to be a little odd */
# else
#  define HZ	1024	/* normal value for Alpha systems */
# endif
#endif

#define EXEC_PAGESIZE	8192

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif
