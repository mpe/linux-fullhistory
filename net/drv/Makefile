#
# Makefile for the linux device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

OBJS	=
SUBDIRS = #we8003 slip dp8390

.c.s:
		$(CC) $(CFLAGS) -S $<
.c.o:
		$(CC) $(CFLAGS) -c $<
.s.o:
		$(AS) -o $*.o $<

all:		drv.a

drv.a:		$(OBJS) dummy
		@rm -f drv.a
		@$(AR) rcs drv.a
		@for i in $(SUBDIRS); do [ ! -d $$i ] || \
			(cd $$i && echo $$i && $(MAKE) && \
			 $(AR) rcs ../drv.a $$i.o) || \
			exit; \
		 done


clean:
		rm -f core *.o *.a tmp_make .depend
		for i in *.c; do rm -f `basename $$i .c`.s;done
		for i in $(SUBDIRS); do ([ -d $$i ] && cd $$i && $(MAKE) clean); done

dep:
	> .depend
	@for i in $(SUBDIRS); do (cd $$i && echo $$i && $(MAKE) dep) || exit; done

dummy:
#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
