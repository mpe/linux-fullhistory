# Makefile for Creative Labs EMU10K1 
#
# 12 Apr 2000 Rui Sousa

ifeq ($(CONFIG_SOUND_EMU10K1),y)
    O_TARGET := emu10k1.o
    O_OBJS = audio.o cardmi.o cardmo.o cardwi.o cardwo.o ecard.o \
             emuadxmg.o hwaccess.o irqmgr.o main.o midi.o mixer.o \
             recmgr.o timer.o voicemgr.o
else
  ifeq ($(CONFIG_SOUND_EMU10K1),m)
    M_OBJS := emu10k1.o
    O_TARGET := emu10k1.o
    O_OBJS = audio.o cardmi.o cardmo.o cardwi.o cardwo.o ecard.o \
             emuadxmg.o hwaccess.o irqmgr.o main.o midi.o mixer.o \
             recmgr.o timer.o voicemgr.o
  endif
endif

ifdef DEBUG
    EXTRA_CFLAGS += -DEMU10K1_DEBUG
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
