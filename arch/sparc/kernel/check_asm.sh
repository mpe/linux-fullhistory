#!/bin/sh
sed -n -e '/struct[ 	]*'$1'_struct[ 	]*{/,/};/p' < $2 | sed '/struct[ 	]*'$1'_struct[ 	]*{/d;/:[0-9]*[ 	]*;/d;/^[ 	]*$/d;/};/d;s/^[ 	]*//;s/volatile[ 	]*//;s/\(unsigned\|signed\|struct\)[ 	]*//;s/\(\[\|__attribute__\).*;[ 	]*$//;s/;[ 	]*$//;s/^[^ 	]*[ 	]*//;s/,/\
/g' | sed 's/^[ 	*]*//;s/[ 	]*$//;s/^.*$/printf ("#define AOFF_'$1'_\0	0x%08x\\n#define ASIZ_'$1'_\0	0x%08x\\n", ((char *)\&_'$1'.\0) - ((char *)\&_'$1'), sizeof(_'$1'.\0));/' >> $3
