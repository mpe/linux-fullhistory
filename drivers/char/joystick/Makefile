#
# Makefile for the joystick drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
MOD_IN_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS)

# The target object and module list name.

O_TARGET	:= js.o
M_OBJS		:=
O_OBJS		:=
#MOD_LIST_NAME	:= INPUT_MODULES

# Objects that export symbols.

export-objs	:= serio.o gameport.o

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT_SERPORT)	+= serport.o serio.o

obj-$(CONFIG_INPUT_NS558)	+= ns558.o gameport.o
obj-$(CONFIG_INPUT_LIGHTNING)	+= lightning.o gameport.o
obj-$(CONFIG_INPUT_PCIGAME)	+= pcigame.o gameport.o

obj-$(CONFIG_INPUT_WARRIOR)	+= warrior.o serio.o
obj-$(CONFIG_INPUT_MAGELLAN)	+= magellan.o serio.o
obj-$(CONFIG_INPUT_SPACEORB)	+= spaceorb.o serio.o
obj-$(CONFIG_INPUT_SPACEBALL)	+= spaceball.o serio.o
obj-$(CONFIG_INPUT_IFORCE_232)	+= serio.o

obj-$(CONFIG_INPUT_ANALOG)	+= analog.o gameport.o
obj-$(CONFIG_INPUT_A3D)		+= a3d.o gameport.o
obj-$(CONFIG_INPUT_ADI)		+= adi.o gameport.o
obj-$(CONFIG_INPUT_COBRA)	+= cobra.o gameport.o
obj-$(CONFIG_INPUT_GF2K)	+= gf2k.o gameport.o
obj-$(CONFIG_INPUT_GRIP)	+= grip.o gameport.o
obj-$(CONFIG_INPUT_INTERACT)	+= interact.o gameport.o
obj-$(CONFIG_INPUT_TMDC)	+= tmdc.o gameport.o
obj-$(CONFIG_INPUT_SIDEWINDER)	+= sidewinder.o gameport.o

obj-$(CONFIG_INPUT_DB9)		+= db9.o
obj-$(CONFIG_INPUT_GAMECON)	+= gamecon.o
obj-$(CONFIG_INPUT_TURBOGRAFX)	+= turbografx.o

obj-$(CONFIG_INPUT_AMIJOY)	+= amijoy.o

# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Translate to Rules.make lists.

O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

# The global Rules.make.

include $(TOPDIR)/Rules.make
