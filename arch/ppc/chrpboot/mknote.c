struct desc
{
	unsigned long namesz;
	unsigned long descrsz;
	unsigned long type;
	char name[8];
	unsigned long real_mode;
	unsigned long real_base;
	unsigned long real_size;
	unsigned long virt_base;
	unsigned long virt_size;
	unsigned long load_base;
};

int main(void)
{
	struct desc ns;
	ns.namesz = 8;
	ns.descrsz = 24;
	ns.type = 0x1275;
	strcpy(ns.name, "PowerPC");
	ns.load_base = -1;
	write( 1, &ns, sizeof(struct desc));
	return 0;
}
