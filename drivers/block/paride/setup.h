/*
	setup.h	   (c) 1997 Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU public license.

        This is a table driven setup function for kernel modules
        using the module.variable=val,... command line notation.

*/

#include <linux/ctype.h>
#include <linux/string.h>

struct setup_tab_t {

	char	*tag;	/* variable name */
	int	size;	/* number of elements in array */
	int	*iv;	/* pointer to variable */
};

typedef struct setup_tab_t STT;

/*  t 	  is a table that describes the variables that can be set
	  by gen_setup
    n	  is the number of entries in the table
    ss	  is a string of the form:

		<tag>=[<val>,...]<val>
*/

static void generic_setup( STT t[], int n, char *ss )

{	int	j,k;

	k = 0;
	for (j=0;j<n;j++) {
		k = strlen(t[j].tag);
		if (strncmp(ss,t[j].tag,k) == 0) break;
	}
	if (j == n) return;

	if (ss[k] == 0) {
		t[j].iv[0] = 1;
		return;
	}

	if (ss[k] != '=') return;
	ss += (k+1);

	k = 0;
	while (ss && isdigit(*ss) && (k < t[j].size)) {
		t[j].iv[k++] = simple_strtoul(ss,NULL,0);
		if ((ss = strchr(ss,',')) != NULL) ss++;
	}
}

/* end of setup.h */
