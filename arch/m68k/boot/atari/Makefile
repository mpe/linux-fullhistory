
CC		:= d:/gnu/bin/gcc.ttp
CFLAGS	:= -Wall -O2 -fno-defer-pop -mint -s
LD		:= d:/gnu/bin/gcc.ttp
LDFLAGS	:= -mint -D__GNUC__

bootstra.ttp: bootstra.o
	$(LD) $(LDFLAGS) -o $@ $^
	prgflags 7 7 $@
	cp $@ d:/linux

bootstra.o: bootstra.c bootinfo.h
