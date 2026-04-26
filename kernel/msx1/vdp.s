
obj_video: equ 963
    .buffer: equ 0
    .buffer__size: equ 960
    .x: equ 960
    .y: equ 961
    .changed: equ 962

section bss

    vdp_stack_base: resb 200
    vdp_stack_top:

    vdp_buffer_0: resb obj_video
    vdp_buffer_1: resb obj_video

    vdp_int_enabled: resb 1

section text

global vdp_init
vdp_init:
    ld hl, vdp_main
    ld de, vdp_stack_top
    call proc_new
    ret

vdp_main:
    ; define buffer atual na tela fisica
    ld hl, vdp_buffer_0
    ld [sysvar_vdp_current], hl

    ; inicializa buffer 0
    ld de, vdp_buffer_0
    ld a, ' '
    ld bc, obj_video.buffer__size
    call mem_fill_byte
    ld a, 1
    ld [vdp_buffer_0 + obj_video.changed], a

    ; inicializa buffer 1 (copiando buffer 0 recem inicializado)
    ld de, vdp_buffer_1
    ld hl, vdp_buffer_0
    ld bc, obj_video
    ldir

    ; ativa interrupcao do vdp
    ld a, 1
    ld [vdp_int_enabled], a

    ; loop infinito
    .infinity:
        halt
        jp .infinity

global vdp_int_handler
vdp_int_handler:

    ; reseta o estado do VDP
    in a, [0x99] ; reseta as interrupcoes do VDP Primario

    ; encerra se interrupcao desativada programaticamente
    ld a, [vdp_int_enabled]
    cp 0
    ret z

    ret

