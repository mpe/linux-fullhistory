/*
 *	Generate devlist.h and classlist.h from the PCI ID file.
 *
 *	(c) 1999 Martin Mares <mj@suse.cz>
 */

#include <stdio.h>
#include <string.h>

static void
pq(FILE *f, const char *c)
{
	while (*c) {
		if (*c == '"')
			fprintf(f, "\\\"");
		else
			fputc(*c, f);
		c++;
	}
}

int
main(void)
{
	char line[1024], *c, vend[8];
	int vendors = 0;
	int mode = 0;
	FILE *devf, *clsf;

	devf = fopen("devlist.h", "w");
	clsf = fopen("classlist.h", "w");
	if (!devf || !clsf) {
		fprintf(stderr, "Cannot create output file!\n");
		return 1;
	}

	while (fgets(line, sizeof(line)-1, stdin)) {
		if ((c = strchr(line, '\n')))
			*c = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (line[1] == ' ') {
			if (line[0] == 'C' && strlen(line) > 4 && line[4] == ' ') {
				vend[0] = line[2];
				vend[1] = line[3];
				vend[2] = 0;
				mode = 2;
			} else goto err;
		}
		else if (line[0] == '\t') {
			if (line[1] == '\t')
				continue;
			switch (mode) {
			case 1:
				if (strlen(line) > 5 && line[5] == ' ') {
					c = line + 5;
					while (*c == ' ')
						*c++ = 0;
					fprintf(devf, "\tDEVICE(%s,%s,\"", vend, line+1);
					pq(devf, c);
					fputs("\")\n", devf);
				} else goto err;
				break;
			case 2:
				if (strlen(line) > 3 && line[3] == ' ') {
					c = line + 3;
					while (*c == ' ')
						*c++ = 0;
					fprintf(clsf, "CLASS(%s%s, \"%s\")\n", vend, line+1, c);
				} else goto err;
				break;
			default:
				goto err;
			}
		} else if (strlen(line) > 4 && line[4] == ' ') {
			c = line + 4;
			while (*c == ' ')
				*c++ = 0;
			if (vendors)
				fputs("ENDVENDOR()\n\n", devf);
			vendors++;
			strcpy(vend, line);
			fprintf(devf, "VENDOR(%s,\"", vend);
			pq(devf, c);
			fputs("\")\n", devf);
			mode = 1;
		} else {
		err:
			fprintf(stderr, "Syntax error in mode %d: %s\n", mode, line);
			return 1;
		}
	}
	fputs("ENDVENDOR()\n\
\n\
#undef VENDOR\n\
#undef DEVICE\n\
#undef ENDVENDOR\n", devf);
	fputs("\n#undef CLASS", clsf);

	fclose(devf);
	fclose(clsf);

	return 0;
}
