.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) $(ASFLAGS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.S.s:
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
.S.o:
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
	$(AS) $(ASFLAGS) -o $*.o $*.s
	rm $*.s

HOST_CC = gcc

L_TARGET = lib.o
L_OBJS  = checksum.o cksum_support.o string.o

${L_TARGET}: $(L_OBJS)
	$(LD) -r -o ${L_TARGET} $(L_OBJS)


fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

modules: 

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
