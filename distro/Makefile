OUT = msx1_microsol_720.img

include ../config.mk

all: $(OUT)
	@true

clean:
	@rm -f $(OUT) *.img.info 2> /dev/null | true

common_image: .FORCE
	@echo "IMG...: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(IMG)))"
	@../sdk/mkrfs $(IMG) $(IMG_CYLS) $(IMG_HEADS) $(IMG_SECTORS)
	@../sdk/rfs_boot $(IMG) ../boot/stage1_$(BOOT).boot ../boot/stage2_$(BOOT).bin
	@../sdk/rfs_mkdir $(IMG) /bin 755 0 0
	@../sdk/rfs_mkdir $(IMG) /boot 755 0 0
	@../sdk/rfs_mkdir $(IMG) /dev 755 0 0
	@../sdk/rfs_mkdir $(IMG) /etc 755 0 0
	@../sdk/rfs_mkdir $(IMG) /root 700 0 0
	@../sdk/rfs_mkdir $(IMG) /lib 755 0 0
	@../sdk/rfs_mkdir $(IMG) /home 755 0 0
	@../sdk/rfs_mkdir $(IMG) /home/user 700 1000 1000
	@../sdk/rfs_mkdir $(IMG) /usr 755 0 0
	@../sdk/rfs_mkdir $(IMG) /sbin 755 0 0
	@../sdk/rfs_add $(IMG)  ../etc/passwd /etc/passwd 600 0 0
	@../sdk/rfs_add $(IMG)  ../etc/group /etc/group 600 0 0
	@../sdk/rfs_mknod $(IMG) /dev/keyb c 1 0
	@../sdk/rfs_mknod $(IMG) /dev/vdp c 2 0
	@../sdk/rfs_mknod $(IMG) /dev/disk0 b 3 0
	@../sdk/rfs_mknod $(IMG) /dev/disk1 b 3 1
	@../sdk/rfs_add $(IMG)  ../license /etc/license 444 root root
	@../sdk/rfs_add $(IMG)  ../bin/sh.app /bin/sh 777 root root
	@../sdk/rfs_add $(IMG)  ../boot/stage1_$(BOOT).boot /boot/stage1.boot 744 root root
	@../sdk/rfs_add $(IMG)  ../boot/stage2_$(BOOT).bin /boot/stage2.bin 744 root root
	@../sdk/rfs_add $(IMG)  ../lib/app.lib /lib/app.lib 444 root root
	@../sdk/rfs_add $(IMG)  ../readme.md /root/retrosox.md 666 root root
	@../sdk/rfs_add $(IMG)  ../sbin/init.app /sbin/init 700 root root
	@../sdk/rfs_add $(IMG)  ../kernel/retroSOX_$(KERNEL).kern /retroSOX 744 root root
	@../sdk/rfs_info $(IMG) > $(IMG).info
	@../sdk/rfs_tree $(IMG) / >> $(IMG).info

msx1_microsol_720.img: $(wildcard ../bin/*.app ../etc/* ../lib/*.lib ../boot/*.boot ../boot/*.bin ../kernel/*.kern ../sdk/mkrfs ../sdk/rfs_*) ../license ../readme.md Makefile
	@IMG_CYLS=40 IMG_HEADS=2 IMG_SECTORS=18 IMG=$@ KERNEL=msx1_microsol BOOT=msx1 make --no-print-directory common_image
	@true