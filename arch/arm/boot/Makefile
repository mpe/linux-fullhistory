#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995, 1996 Russell King
#

SYSTEM	=$(TOPDIR)/vmlinux

Image:	$(CONFIGURE) $(SYSTEM)
	$(OBJCOPY) $(SYSTEM) $@

zImage:	$(CONFIGURE) compressed/vmlinux
	$(OBJCOPY) compressed/vmlinux $@

compressed/vmlinux: $(TOPDIR)/vmlinux dep
	@$(MAKE) -C compressed vmlinux

install: $(CONFIGURE) Image
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) Image $(TOPDIR)/System.map "$(INSTALL_PATH)"

zinstall: $(CONFIGURE) zImage
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) zImage $(TOPDIR)/System.map "$(INSTALL_PATH)"

clean:
	rm -f Image zImage
	@$(MAKE) -C compressed clean

dep:
