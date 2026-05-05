M = make --no-print-directory

all clean:
	@$M config.mk
	@$M -C sdk $@
	@$M -C boot $@
	@$M -C lib $@
	@$M -C bin $@
	@$M -C sbin $@
	@$M -C kernel $@
	@$M -C distro $@

fuse sdk:
	@$M config.mk
	@$M -C sdk $@

config.mk: Makefile
	@echo "PRJ_PATH = $(shell pwd)" > config.mk
	@echo "default: all" >> config.mk
	@echo ".PHONY: all clean" >> config.mk
	@echo ".FORCE:" >> config.mk
	@echo ".SILENT:" >> config.mk

run: all
	@openmsx -machine Sharp_HB-8000_1.2 -ext DDX_3.0 -diska distro/msx1_microsol_720.img

install: all
	@install -m 755 sdk/mkrfs /usr/local/bin/mkrfs
	@install -m 755 sdk/rfs_* /usr/local/bin/

.FORCE:
.SILENT: