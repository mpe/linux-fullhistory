#
# linux/arch/m68k/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.

ifdef CONFIG_AMIGA
AMIGA_BOOTOBJS := amiga/bootstrap.o
endif

ifdef CONFIG_ATARI
ATARI_BOOTOBJS := atari/bootstrap.o
HOSTCC += -b m68k-mint
endif

ifdef CONFIG_ATARI
atari_bootstrap: $(ATARI_BOOTOBJS)
	$(HOSTCC) $(HOSTFLAGS) -o $@ $(ATARI_BOOTOBJS)
	rm -f ../../../bootstrap
	ln $@ ../../../bootstrap
endif

ifdef CONFIG_AMIGA
amiga_bootstrap: $(AMIGA_BOOTOBJS)
	$(HOSTCC) $(HOSTFLAGS) -o $@ $(AMIGA_BOOTOBJS)
	rm -f ../../../bootstrap
	ln $@ ../../../bootstrap
endif

$(AMIGA_BOOTOBJS) $(ATARI_BOOTOBJS): %.o: %.c
	$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

clean:
	rm -f *.o

dep:
