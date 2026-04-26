
all sdk: $(OUT)
	@true


fuse: $(OUT) $(OUT_FUSE)
	@true

clean:
	@rm -f $(OUT) $(OUT_FUSE) 2> /dev/null | true

../rfs_mount: rfs_mount.c rfs.h
	@echo "C.....: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@mkdir -p $(dir $@)
	@gcc -Wall $< -o $@ -D_FILE_OFFSET_BITS=64 -I/usr/local/include/osxfuse -L/usr/local/lib -lfuse

../%: %.c $(wildcard *.h)
	@echo "C.....: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@mkdir -p $(dir $@)
	@gcc -o $@ $<