#include <stdio.h>

int main(int argc, char *argv[])
{
	int ch, total=0;

	if (argc > 1)
		printf("const char %s[] %s=\n",
			argv[1], argc > 2 ? argv[2] : "");

	do {
		printf("\t\"");
		while ((ch = getchar()) != EOF)
		{
			total++;
			printf("\\x%02x",ch);
			if (total % 16 == 0)
				break;
		}
		printf("\"\n");
	} while (ch != EOF);

	if (argc > 1)
		printf("\t;\n\nconst int %s_size = %d;\n", argv[1], total);

	return 0;
}
