HOSTCFLAGS:=$(HOSTCFLAGS) -g -Wall
HEADER=header.tk
TAIL=tail.tk

# Previous versions always remade kconfig.tk because they always depended
# on soundscript.  This runs fairly fast, and I can't find all the
# Config.in files to depend on anyways.  So I'll force it to remake.

kconfig.tk: dummy

kconfig.tk: ${TOPDIR}/Makefile ${TOPDIR}/arch/${ARCH}/config.in \
		tkparse ${HEADER} ${TAIL}
	@if [ -f /usr/local/bin/wish ];	then \
		echo '#!'"/usr/local/bin/wish -f" > kconfig.tk; \
	else \
		echo '#!'"/usr/bin/wish -f" > kconfig.tk; \
	fi
	cat ${HEADER} >> ./kconfig.tk
	./tkparse < ../arch/${ARCH}/config.in >> kconfig.tk
	echo "set defaults \"arch/${ARCH}/defconfig\"" >> kconfig.tk
	cat ${TAIL} >> kconfig.tk
	chmod 755 kconfig.tk

tkparse: tkparse.o tkcond.o tkgen.o
	${HOSTCC} -o tkparse tkparse.o tkcond.o tkgen.o

tkparse.o: tkparse.c tkparse.h
	$(HOSTCC) $(HOSTCFLAGS) -c -o tkparse.o tkparse.c

tkcond.o: tkcond.c tkparse.h
	$(HOSTCC) $(HOSTCFLAGS) -c -o tkcond.o tkcond.c

tkgen.o: tkgen.c tkparse.h
	$(HOSTCC) $(HOSTCFLAGS) -c -o tkgen.o tkgen.c

clean:
	rm -f *~ kconfig.tk *.o tkparse

include $(TOPDIR)/Rules.make
