/*
 *	Generate devlist.h from the PCI ID file.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <stdio.h>
#include <string.h>

static void
pq(char *c)
{
	while (*c) {
		if (*c == '"')
			printf("\\\"");
		else
			putchar(*c);
		c++;
	}
}

int
main(void)
{
	char line[1024], *c, vend[8];
	int vendors = 0;

	while (fgets(line, sizeof(line)-1, stdin)) {
		if ((c = strchr(line, '\n')))
			*c = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (line[1] == ' ') {
			vend[0] = 0;
			continue;
		}
		if (line[0] == '\t') {
			if (vend[0] && strlen(line) > 5 && line[5] == ' ') {
				c = line + 5;
				while (*c == ' ')
					*c++ = 0;
				printf("\tDEVICE(%s,%s,\"", vend, line+1);
				pq(c);
				puts("\")");
			}
		} else if (strlen(line) > 4 && line[4] == ' ') {
			c = line + 4;
			while (*c == ' ')
				*c++ = 0;
			if (vendors)
				puts("ENDVENDOR()\n");
			vendors++;
			strcpy(vend, line);
			printf("VENDOR(%s,\"", vend);
			pq(c);
			puts("\")");
		}
	}
	puts("ENDVENDOR()\n\
\n\
#undef VENDOR\n\
#undef DEVICE\n\
#undef ENDVENDOR");

	return 0;
}
