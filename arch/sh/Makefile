# $Id$
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#

#
# Select the object file format to substitute into the linker script.
#
tool-prefix	= sh-gniibe-
oformat		= elf

ifdef CONFIG_CROSSCOMPILE
CROSS_COMPILE  = $(tool-prefix)
endif

LINKFLAGS	= # -EL # -static #-N
MODFLAGS	+=

#
#
CFLAGS		+= -m3 # -ml
LINKFLAGS	+=
LDFLAGS		+= # -EL

#
#
HOSTCC        = cc

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#
LINKSCRIPT    = arch/sh/vmlinux.lds
LINKFLAGS     += -T $(word 1,$(LINKSCRIPT)) -e __stext

ifdef LOADADDR
LINKFLAGS     += -Ttext $(word 1,$(LOADADDR))
endif

#
CFLAGS		+= -pipe

HEAD := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

SUBDIRS := $(SUBDIRS) $(addprefix arch/sh/, kernel mm lib)
CORE_FILES := arch/sh/kernel/kernel.o arch/sh/mm/mm.o $(CORE_FILES)
LIBS := $(TOPDIR)/arch/sh/lib/lib.a $(LIBS) $(TOPDIR)/arch/sh/lib/lib.a /home/niibe/lib/gcc-lib/sh-gniibe-elf/egcs-2.91.66/libgcc.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: arch/sh/vmlinux.lds

arch/sh/vmlinux.lds: arch/sh/vmlinux.lds.S FORCE
	gcc -E -C -P -I$(HPATH) -imacros $(HPATH)/linux/config.h -Ush arch/sh/vmlinux.lds.S >arch/sh/vmlinux.lds

FORCE: ;

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zdisk: vmlinux
	@$(MAKEBOOT) zdisk

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean
#	$(MAKE) -C arch/$(ARCH)/tools clean

archmrproper:

archdep:
	@$(MAKEBOOT) dep
