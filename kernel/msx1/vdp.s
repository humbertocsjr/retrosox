
obj_video: equ 963
    .x: equ 0
    .y: equ 1
    .changed: equ 2
    .buffer: equ 3
    .buffer__size: equ 960

section bss

    vdp_stack_base: resb 200
    vdp_stack_top:

    vdp_buffer_0: resb obj_video
    vdp_buffer_1: resb obj_video

    vdp_int_enabled: resb 1

section text

    global vdp_init
    vdp_init:
        ; inicia o processo de gerenciamento do vdp
        ld hl, vdp_main
        ld de, vdp_stack_top
        call proc_new
        
        ; define dispositivo
        ld b, dev_id_vdp
        call dev_set_process
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

        ; encerra se o buffer atual nao foi alterado
        ld ix, [sysvar_vdp_current]
        ld a, [ix+obj_video.changed]
        cp 0
        ret z

        ; redefine marcador de buffer alterado
        ld [ix+obj_video.changed], 0

        ; copia buffer para vram
        ld a, 0
        out [0x99], a
        ld a, 0x40
        out [0x99], a

        ; define parametros para copia
        ld hl, [sysvar_vdp_current]
        ld de, obj_video.buffer
        add hl, de
        ld bc, obj_video.buffer__size
        ld a, b
        inc a
        ld b, c
        ld c, 0x98
        .loop:
            ; copia buffer
            outi
            jr nz, .loop
            dec a
            jr nz, .loop

        ret

