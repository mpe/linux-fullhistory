#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <a.out.h>

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long u32;

void die(const char * str, ...)
{
	va_list args;
	va_start(args, str);
	vfprintf(stderr, str, args);
	fputc('\n', stderr);
	exit (1);
}

int main(int argc, char **argv)
{
	void *data;
	struct exec ex;
	FILE *f;
	int totlen;

	if (argc < 2) {
		fprintf(stderr, "Usage: build kernel-name\n");
		exit(1);
	}

	f = fopen(argv[1], "rb");
	if (!f)
		die("Unable to open `%s': %m", argv[1]);

	fread(&ex, 1, sizeof(ex), f);

	if(N_MAGIC(ex) == ZMAGIC) {
		fseek(f, 4096, SEEK_SET);
		totlen = ex.a_text + ex.a_data;
	} else
	if(N_MAGIC(ex) == QMAGIC) {
		unsigned long my_header;
		
		fseek(f, 4, SEEK_SET);

		my_header = 0xea000006;

		fwrite(&my_header, 4, 1, stdout);

		totlen = ex.a_text + ex.a_data - 4;
	} else {
		fprintf(stderr, "Unacceptable a.out header on kernel\n");
		fclose(f);
		exit(1);
	}

	fprintf(stderr, "Kernel is %dk (%dk text, %dk data, %dk bss)\n",
		(ex.a_text + ex.a_data + ex.a_bss)/1024,
		 ex.a_text/1024, ex.a_data/1024, ex.a_bss/1024);

	data = malloc(totlen);
	fread(data, 1, totlen, f);
	fwrite(data, 1, totlen, stdout);

	free(data);
	fclose(f);
	fflush(stdout);
	return 0;
}
