#
# linux/arch/m68k/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.

ifdef CONFIG_AMIGA
AMIGA_BOOTSTRAP = amiga_bootstrap
AMIGA_BOOTOBJS := amiga/bootstrap.o amiga/linuxboot.o
AMIGA_HOSTCC = m68k-cbm-amigados-gcc -I$(TOPDIR)/include
AMIGA_HOSTFLAGS=-m68030 -O2 -Wall -Dlinux
endif

ifdef CONFIG_ATARI
ATARI_BOOTSTRAP = atari_bootstrap
ATARI_BOOTOBJS := atari/bootstrap.o
ATARI_HOSTCC = m68k-mint-gcc -I$(TOPDIR)/include
ATARI_HOSTFLAGS = -m68030 -m68881 -Dlinux -O2 -Wall

# BOOTP/TFTP support in bootstrap?
# USE_BOOTP = y

ifdef USE_BOOTP
ATARI_BOOTOBJS += atari/bootp.o
ATARI_HOSTFLAGS += -DUSE_BOOTP

# low-level Ethernet drivers:

# Lance (RieblCard, PAM-VME)
ATARI_BOOTOBJS += atari/ethlance.o
ATARI_HOSTFLAGS += -DETHLL_LANCE

endif
endif

ifdef CONFIG_ATARI
atari_bootstrap: $(ATARI_BOOTOBJS)
	$(ATARI_HOSTCC) $(ATARI_HOSTFLAGS) -o $@ $(ATARI_BOOTOBJS)
	rm -f ../../../bootstrap
	ln $@ ../../../bootstrap
endif

ifdef CONFIG_AMIGA
amiga_bootstrap: $(AMIGA_BOOTOBJS)
	$(AMIGA_HOSTCC) $(AMIGA_HOSTFLAGS) -o $@ -s -noixemul $(AMIGA_BOOTOBJS)
	rm -f ../../../bootstrap
	ln $@ ../../../bootstrap
endif

$(AMIGA_BOOTOBJS): %.o: %.c
	$(AMIGA_HOSTCC) $(AMIGA_HOSTFLAGS) -c $< -o $@

$(ATARI_BOOTOBJS): %.o: %.c
	$(ATARI_HOSTCC) $(ATARI_HOSTFLAGS) -c $< -o $@

bootstrap: $(AMIGA_BOOTSTRAP) $(ATARI_BOOTSTRAP)

clean:
	rm -f *.o amiga/*.o atari/*.o amiga_bootstrap atari_bootstrap

dep:
