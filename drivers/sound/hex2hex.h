/*
 * This file is a part of configure.c
 *
 * hex2hex reads an input file in Intel HEX format and produces
 * an (unsigned char) array which contains the bytes and writes it to the
 * output file using C syntax
 */

#define MAX_SIZE (256*1024)
#define ABANDON(why) { \
		fprintf(stderr, "%s: " why "\n", source); \
		return 0; \
		}

int loadhex(FILE *inf, unsigned char *buf, char *source)
{
	int l=0, c, i;

	while ((c=getc(inf))!=EOF)
	{
		if (c == ':')	/* Sync with beginning of line */
		{
			int n, check;
			unsigned char sum;
			int addr;
			int linetype;

			if (fscanf(inf, "%02x", &n) != 1)
			   ABANDON("File format error");
			sum = n;

			if (fscanf(inf, "%04x", &addr) != 1)
			   ABANDON("File format error");
			sum += addr/256;
			sum += addr%256;

			if (fscanf(inf, "%02x", &linetype) != 1)
			   ABANDON("File format error");
			sum += linetype;

			if (linetype != 0)
			   continue;

			for (i=0;i<n;i++)
			{
				if (fscanf(inf, "%02x", &c) != 1)
			   	   ABANDON("File format error");
				if (addr >= MAX_SIZE)
				   ABANDON("File too large");
				buf[addr++] = c;
				if (addr > l)
				   l = addr;
				sum += c;
			}

			if (fscanf(inf, "%02x", &check) != 1)
			   ABANDON("File format error");

			sum = ~sum + 1;
			if (check != sum)
			   ABANDON("Line checksum error");
		}
	}

	return l;
}

int hex2hex(char *source, char *target, char *varline)
{
	FILE *inf, *outf;

	int i,l;
	unsigned char buf[MAX_SIZE];

	if ((inf=fopen(source, "r"))==NULL)
	{
		perror(source);
		return 0;
	}

	if ((outf=fopen(target, "w"))==NULL)
	{
		perror(target);
		fclose(inf);
		return 0;
	}

	l=loadhex(inf, buf, source);
	if (l<=0)
        {
           fclose(inf);
	   fclose(outf);
	   return l;
	}


	fprintf(outf, "/*\n *\t Computer generated file. Do not edit.\n */\n");
        fprintf(outf, "static int %s_len = %d;\n", varline, l);
	fprintf(outf, "static unsigned char %s[] = {\n", varline);

	for (i=0;i<l;i++)
	{
		if (i) fprintf(outf, ",");
		if (i && !(i % 16)) fprintf(outf, "\n");
		fprintf(outf, "0x%02x", buf[i]);
	}

	fprintf(outf, "\n};\n\n");
	fclose(inf);
	fclose(outf);
	return 1;
}
