#
# Makefile for the Linux 802.x protocol layers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

.c.o:
	$(CC) $(CFLAGS) -c $<
.s.o:
	$(AS) -o $*.o $<


OBJS	:= p8023.o

ifdef CONFIG_TR

OBJS	:= $(OBJS) tr.o

endif

ifdef CONFIG_IPX

OBJS	:= $(OBJS) p8022.o psnap.o

endif

ifdef CONFIG_ATALK
ifndef CONFIG_IPX

OBJS	:= $(OBJS) p8022.o psnap.o

endif
endif

802.o:		$(OBJS)
		$(LD) -r -o 802.o $(OBJS)


dep:
		$(CPP) -M *.c > .depend

tar:
		tar -cvf /dev/f1 .


include $(TOPDIR)/Rules.make
