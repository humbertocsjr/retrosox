
global dev_id_keyb
dev_id_keyb: equ 1

global dev_id_vdp
dev_id_vdp: equ 2

global dev_id_disk
dev_id_disk: equ 3

dev_list__max: equ 32 ; tem que ser expoente de 2

section bss

    dev_list: resb dev_list__max * 2

section text

    global dev_init
    dev_init:
        ret
        

    global dev_get_process
    ; b = dispositivo
    ; le o numero do processo do dispositivo
    ; ret cf = 1=ok | 0=nao encontrado
    ; ret hl = processo
    dev_get_process:
        ; salva registradores
        push hl
        push de

        ; compara se esta dentro do range
        ld a, b
        cp dev_list__max
        jr c, .ok
            xor a
            jr .end
        .ok:

        ; calcula posicao
        ld l, b
        ld h, 0
        add hl, hl
        ld bc, dev_list
        add hl, bc

        ; le processo
        ld e, [hl]
        inc hl
        ld d, [hl]
        ex de, hl

        ; verifica se existe
        ld a, e
        or d
        jr .existe
            xor a
            jr .end
        .existe:

        xor a
        scf
        .end:
        ; restaura registradores
        pop de
        pop hl
        ret

    global dev_set_process
    ; hl = processo
    ; b = dispositivo
    ; define um dispositivo
    ; ret cf = 1=ok | 0=fora do limite
    dev_set_process:
        ; salva registradores
        push bc
        push hl
        push de

        ; compara se esta dentro do range
        ld a, b
        cp dev_list__max
        jr c, .ok
            xor a
            jr .end
        .ok:

        ; calcula posicao
        ex de, hl
        ld l, b
        ld h, 0
        add hl, hl
        ld bc, dev_list
        add hl, bc

        ; salva numero do processo
        ld [hl], e
        inc hl
        ld [hl], d

        xor a
        scf
        .end:
        ; restaura registradores
        pop de
        pop hl
        pop bc
        ret