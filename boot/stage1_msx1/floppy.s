section text
_header:
    db 0xeb, 0xfe, 0x90
cfg_media_type: db 0xf9
    times _header + 0x1e - $ db 0
_start:
    ; configura pilha
    di
    ld sp, 0xd000

    ; copia segunda parte do setor de boot
    ld hl, [0xf351]
    inc h
    ld bc, 256
    ld de, 0xc100
    ldir
    ei 
    
    ; inicializa slots de memoria
    ld a, [0xf342]
    ld hl, 0x4000
    call 0x24
    ld a, [0xf343]
    ld hl, 0x8000
    call 0x24

    ; inicializa tela
    ld a, 40
    ld [0xf3ae], a
    call 0x6c

    ; le o estagio 2
    .try:
        ld a, [cfg_media_type]
        ld c, a
        ld a, 0
        ld b, 5
        ld de, 1
        ld hl, 0xc200
        or a
        call 0x144
        jr c, .try

    ; copia estagio 2
    ld bc, 2560
    ld hl, 0xc200
    ld de, 0x4000
    ldir

    ld a, [cfg_media_type]

    jp 0x4000