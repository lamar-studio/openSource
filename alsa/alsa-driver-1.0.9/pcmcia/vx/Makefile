ifndef SND_TOPDIR
SND_TOPDIR=../..
endif

include $(SND_TOPDIR)/toplevel.config
include $(SND_TOPDIR)/Makefile.conf

clean-files := vxpocket.c vx_entry.c

export-objs = vx_entry.o

include $(SND_TOPDIR)/alsa-kernel/pcmcia/vx/Makefile

include $(SND_TOPDIR)/Rules.make

vxpocket.c: vxpocket.patch $(SND_TOPDIR)/alsa-kernel/pcmcia/vx/vxpocket.c
vxp440.c: vxpocket.c
vx_entry.c: vx_entry.patch vx_entry.inc vx_entry.inc1 $(SND_TOPDIR)/alsa-kernel/pcmcia/vx/vx_entry.c
