char buffer[1036];
char buffer2[1036];

int main ()
{
	char *p;
	int i, o, o2, l;

	printf ("Testing memset\n");
	for (l = 1; l < 1020; l ++) {
		for (o = 0; o < 4; o++) {
			p = buffer + o + 4;
			for (i = 0; i < l + 12; i++)
				buffer[i] = 0x55;

			memset (p, 0xaa, l);
	
			for (i = 0; i < l; i++)
				if (p[i] != 0xaa)
					printf ("Error: %X+%d\n", p, i);
			if (p[-1] != 0x55 || p[-2] != 0x55 || p[-3] != 0x55 || p[-4] != 0x55)
				printf ("Error before %X\n", p);
			if (p[l] != 0x55 || p[l+1] != 0x55 || p[l+2] != 0x55 || p[l+3] != 0x55)
				printf ("Error at end: %p: %02X %02X %02X %02X\n", p+l, p[l], p[l+1], p[l+2], p[l+3]);
		}
	}

	printf ("Testing memcpy s > d\n");
	for (l = 1; l < 1020; l++) {
		for (o = 0; o < 4; o++) {
			for (o2 = 0; o2 < 4; o2++) {
				char *d, *s;

				for (i = 0; i < l + 12; i++)
					buffer[i] = (i & 0x3f) + 0x40;
				for (i = 0; i < 1036; i++)
					buffer2[i] = 0;

				s = buffer + o;
				d = buffer2 + o2 + 4;

				memcpy (d, s, l);

				for (i = 0; i < l; i++)
					if (s[i] != d[i])
						printf ("Error at %X+%d -> %X+%d (%02X != %02X)\n", s, i, d, i, s[i], d[i]);
				if (d[-1] || d[-2] || d[-3] || d[-4])
					printf ("Error before %X\n", d);
				if (d[l] || d[l+1] || d[l+2] || d[l+3])
					printf ("Error after %X\n", d+l);
			}
		}
	}

	printf ("Testing memcpy s < d\n");
	for (l = 1; l < 1020; l++) {
		for (o = 0; o < 4; o++) {
			for (o2 = 0; o2 < 4; o2++) {
				char *d, *s;

				for (i = 0; i < l + 12; i++)
					buffer2[i] = (i & 0x3f) + 0x40;
				for (i = 0; i < 1036; i++)
					buffer[i] = 0;

				s = buffer2 + o;
				d = buffer + o2 + 4;

				memcpy (d, s, l);

				for (i = 0; i < l; i++)
					if (s[i] != d[i])
						printf ("Error at %X+%d -> %X+%d (%02X != %02X)\n", s, i, d, i, s[i], d[i]);
				if (d[-1] || d[-2] || d[-3] || d[-4])
					printf ("Error before %X\n", d);
				if (d[l] || d[l+1] || d[l+2] || d[l+3])
					printf ("Error after %X\n", d+l);
			}
		}
	}
}
