#include <stdio.h>

int main(int argc, char *argv[])
{
	int c, n=0, len=0;

	printf(	
		".globl _input_data\n"
		".data\n"
		"_input_data:\n");

	while ((c=getchar()) != EOF)
	{
		len++;
		if (!n) printf("\n.byte "); else printf(",");
		printf("%d", c);
		n = (n+1) & 0x1f;
	}

	printf("\n\n");

	fprintf(stderr, "Compressed size %d.\n", len);


	printf(	".globl _input_len\n"
		".align 2\n"
		"_input_len:\n"
		"\t.long %d\n", len);

	exit(0);

}
