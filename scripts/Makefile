HEADER=header.tk
TAIL=tail.tk

kconfig.tk: ../arch/${ARCH}/config.in tkparse ${HEADER} ${TAIL}
	./tkparse < ../arch/${ARCH}/config.in > kconfig.tmp
	cp ${HEADER} ./kconfig.tk
	cat kconfig.tmp >> kconfig.tk
	rm -f kconfig.tmp
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
	rm -f kconfig.tk *.o parse

include $(TOPDIR)/Rules.make
