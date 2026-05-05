
OBJ = $(SRC:.s=.obj)

all: $(OUT)

clean:
	@rm -f $(OUT) $(OBJ) $(OUT:=.sym) *.obj *.lib *.sym | true

%.obj: %.s Makefile $(PRJ_PATH)/common.mk
	@echo "ASM...: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hcasm-z80 -o $@ $<

%.boot %.bin: $(OBJ)
	@echo "LINK..: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hclink-bin -text $(OFFSET_TEXT) -sym $@.sym -o $@ $^

%.kern: $(OBJ)
	@echo "LINK..: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hclink-bin -text 0x0000 -bss 0xc200 -sym $@.sym -o $@ $^

%.font: $(OBJ)
	@echo "LINK..: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hclink-bin -o $@ $^

%.app: $(OBJ) $(PRJ_PATH)/lib/app.lib $(pathsubst %,$(PRJ_PATH)/lib/%,$(LIBS))
	@echo "LINK..: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hclink-bin -text 0x4000 -sym $@.sym -o $@ $(PRJ_PATH)/lib/app.lib $^ $(pathsubst %,$(PRJ_PATH)/lib/%,$(LIBS))

%.lib: $(OBJ)
	@echo "LIB...: $(patsubst $(PRJ_PATH)%,%,$(abspath $(shell pwd)/$(@)))"
	@hclib $@ $^

.FORCE:
.SILENT: