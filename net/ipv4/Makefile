#
# Makefile for the Linux TCP/IP (INET) layer.
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

IPV4_OBJS	:= utils.o route.o proc.o timer.o protocol.o packet.o \
		   arp.o ip.o raw.o icmp.o tcp.o udp.o devinet.o af_inet.o \
		   igmp.o ip_fw.o ipip.o

MODULES	:=

ifdef CONFIG_INET_RARP
IPV4_OBJS := $(IPV4_OBJS) rarp.o
else
MODULES := $(MODULES) rarp.o
endif

ifdef CONFIG_INET

ipv4.o:		$(IPV4_OBJS)
		$(LD) -r -o ipv4.o $(IPV4_OBJS)

else

ipv4.o:
		$(AR) rcs ipv4.o

endif

ifdef MODULES
dep:
		$(CPP) -M $(IPV4_OBJS:.o=.c) > .depend
		$(CPP) -M -DMODULE $(MODULES:.o=.c) >> .depend
else
dep:
		$(CPP) -M $(IPV4_OBJS:.o=.c) > .depend
endif

tar:
		tar -cvf /dev/f1 .

ifdef MODULES

modules: $(MODULES)
	echo $(MODULES) > ../../modules/IPV4_MODULES
	cd ../../modules; \
		for i in $(MODULES); do ln -sf ../net/ipv4/$$i .; done

else

modules:

endif

include $(TOPDIR)/Rules.make
