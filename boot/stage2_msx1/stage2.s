section data

    cfg_media_type: db 0
    cfg_disk_id: db 0
    buf_disk: equ 0xc000
    buf_disk_size: equ 512
    rfs_index: times obj_rfs_index db 0
    rfs_signature: db "RFS1"
    rfs_curr_block: dw 0
    kernel_temp_address: equ 0x8000
    kernel_curr_address: dw kernel_temp_address
    kernel_name: db "retroSOX",0
    kernel_name_size: db $ - kernel_name

    obj_rfs_index: equ 22
        .signature: equ 0
        .signature__size: equ 4
        .total_blocks: equ 4
        .block_map_start: equ 6
        .block_map_blocks: equ 8
        .block_map_end: equ 10
        .root_dir_start: equ 12
        .geometry_cylinders: equ 14
        .geometry_heads: equ 16
        .geometry_sectors: equ 18
        .geometry_sectors_per_cylinder: equ 20

    obj_rfs_dir_entry: equ 64
        .mode: equ 0
        .name: equ 2
        .name__size: equ 32
        .starting_block: equ 34
        .file_size: equ 36
        .creation_time: equ 40
        .modification_time: equ 48
        .user_id: equ 56
        .group_id: equ 58
        .reserved: equ 60

section text

global _start
_start:
    ; guarda a configuracao do disco
    ld [cfg_media_type], a

    ; =======
    ; Etapa 1 - Monta o disco
    ; =======
    call tty_print
    db "Sistema de Inicializacao do retroSOX",13,10,10
    db "- Montando disco",0

    ; carrega o indice do disco
    ld de, buf_disk
    ld hl, 9
    call disk_read_raw
    call panic_if_fail

    ; copia o indice para o cache
    ld hl, buf_disk
    ld de, rfs_index
    ld bc, obj_rfs_index
    ldir

    ; Verifica assinatura
    ld hl, rfs_signature
    ld de, rfs_index + obj_rfs_index.signature
    ld b, obj_rfs_index.signature__size
    call str_equal
    call panic_if_fail

    call tty_print_ok

    ; =======
    ; Etapa 2 - Busca e carrega o nucleo
    ; =======
    call tty_print
    db "- Buscando nucleo",0

    ; define a raiz como item atual
    ld hl, [rfs_index + obj_rfs_index.root_dir_start]
    ld [rfs_curr_block], hl

    .find:
        ; carrega o bloco atual do diretorio
        ld hl, [rfs_curr_block]
        ld de, buf_disk
        call disk_read_raw
        call panic_if_fail

        ; Define loop de 8 repeticoes, para verificar as entradas do diretorio
        ld b, 8
        ld hl, buf_disk
        .loop:
            ; salva os registradores
            push bc
            push hl
            ; verifica se o modo da entrada esta preenchido (se a entrada é valida)
            ld a, [hl]
            inc hl
            or [hl]
            jr nz, .item_ok
                ; se a entrada nao existe pula comparacao
                or a
                jr .item_done
            .item_ok:
            inc hl
            ; efetura a comparacao entre o nome no registro e o nome do nucleo
            ld de, kernel_name
            ld a, [kernel_name_size]
            ld b, a 
            call str_equal
            .item_done:
            ; restaura os registradores do loop
            pop hl
            pop bc
            ; se encontrou, pula para o codigo correspondente
            jr c, .found
            ; vai para o proximo item
            ld de, obj_rfs_dir_entry
            add hl, de
            djnz .loop
        .next_block:
        ; não encontrado nesse bloco; carrega o mapa pra buscar o proximo
        ld hl, [rfs_curr_block]
        ld a, h
        ld l, a
        ld h, 0
        ld de, [rfs_index + obj_rfs_index.block_map_start]
        add hl, de
        ld de, buf_disk
        call disk_read_raw
        call panic_if_fail
        ; calcula posicao no mapa carregado
        ld a, [rfs_curr_block]
        ld l, a
        ld h, 0
        add hl, hl
        ld de, buf_disk
        add hl, de
        ; le posicao do proximo bloco
        ld e, [hl]
        inc hl
        ld d, [hl]
        ex de, hl
        ; verifica se existe o proximo bloco
        ld a, h
        cp 0xff
        jr nz, .apply_next_id
        ld a, l
        cp 0xff
        jr z, .not_found
        cp 0xfe
        jr z, .not_found
        .apply_next_id:
        ld [rfs_curr_block], hl
        jp .find
    .not_found:
    call panic
    .found:
    call tty_print_ok

    ; define o bloco atual para o inicio do arquivo do kernel
    ld de, obj_rfs_dir_entry.starting_block
    add hl, de
    ld e, [hl]
    inc hl
    ld d, [hl]
    ex de, hl
    ld [rfs_curr_block], hl

    ; =======
    ; Etapa 3 - Carrega o kernel para o endereco temporario
    ; =======
    call tty_print
    db "- Carregando nucleo",0

    ld b, 32
    .load_block:
        ; guarda contador
        push bc
        ; le o bloco atual
        ld de, [kernel_curr_address]
        ld hl, [rfs_curr_block]
        call disk_read
        call panic_if_fail

        ; calcula proximo endereco de destino
        ld hl, [kernel_curr_address]
        ld de, 512
        add hl, de
        ld [kernel_curr_address], hl

        ; carrega o mapa pra buscar o proximo bloco
        ld hl, [rfs_curr_block]
        ld a, h
        ld l, a
        ld h, 0
        ld de, [rfs_index + obj_rfs_index.block_map_start]
        add hl, de
        ld de, buf_disk
        call disk_read_raw
        call panic_if_fail

        ; calcula posicao no mapa carregado
        ld a, [rfs_curr_block]
        ld l, a
        ld h, 0
        add hl, hl
        ld de, buf_disk
        add hl, de

        ; le posicao do proximo bloco
        ld e, [hl]
        inc hl
        ld d, [hl]
        ex de, hl

        ; verifica se existe o proximo bloco
        ld a, h
        cp 0xff
        jr nz, .apply_next_block_to_load
        ld a, l
        cp 0xff
        jr z, .load_done
        cp 0xfe
        jr z, .load_done
        .apply_next_block_to_load:
        ld [rfs_curr_block], hl

        ; restaura contador
        pop bc
        djnz .load_block
    call tty_print
    db "[ESTOURO DO TAM. DO NUCLEO]",0
    call panic
    
    .load_done:
    call tty_print_ok

    ; desliga o motor do disco
    ld iy, [0xf347] ; Chama a funcao que desliga o motor do disco (ignorado em alguns modelos. ex.: DDX 3.0)
    ld ix, 0x4029
    call 0x1c
    ; HACK - para contornar limitação da BIOS da DDX 3.0 é enviado o comando de desligar o motor manualmente
    ld a, 64
    out [0xd4], a

    ; =======
    ; Etapa 4 - Migra nucleo para o endereço 0x0000 e inicia o nucleo
    ; =======
    di

    ; Le layout do mapeamento da memoria
    in a, [0xa8]
    ld c, a
    ld a, [0xf341]
    ld b, a
    ; b = pagina da memoria em 0x0000
    ; c = leiaute anterior do mapeamento da memoria
    ; d = temporario
    bit 7, a
    jr z, .not_extended
        ld a, b ; restaura slot objetivo
        and 3 ; filtra parte primaria
        rrca ; rotaciona o endereco novo da parte baixa para a alta do byte (00000011 -> 11000000)
        rrca
        ld d, a ; guarda no temp
        ld a, c ; le a copia do leiaute das paginas atual
        and 0x3f ; filtra a parte alta para ser substituida pelo endereco novo (11111111 -> 00111111)
        or d ; copia o endereco novo
        out [0xa8], a ; define o endereco novo como atual do mapa fisico
        ld a, b ; restaura o endereco novo completo
        and 0xc ; filtra a parte secundaria do endereco novo (11111111 -> 00001100)
        rrca ; rotaciona a parte nova para ocupar a base (00001100 -> 00000011)
        rrca
        ld d, a ; guarda o endereco secundario novo em temporario
        ld a, [0xffff] ; le o mapa fisico da pagina secundaria (valor invertido na leitura)
        cpl ; corrige inversao do valor
        and 0xfc ; filtra o endereco que sera alterado no mapa fisico (11111111 -> 11111100)
        or d ; copia o endereco novo secundario para a copia do mapa fisico
        ld [0xffff], a ; define no mapa fisico o endereco secundario novo
        ld a, c  ; le a copia do mapa fisico principal
        out [0xa8], a ; restaura o mapa fisico da pagina principal (alterado temporariamente para mudar o leioute secundario)
    .not_extended:

    ld a, b ; le o endereco novo
    and 3 ; filtra apenas a parte do endereco principal
    ld d, a ; armazena no temporario
    ld a, c ; le o mapa fisico principal
    and 0xfc ; filtra preparando para copiar o endereco novo (11111111 -> 11111100)
    or d ; copia o endereco novo
    out [0xa8], a ; define o mapa fisico principal

    ; copia o nucleo para o endereco final
    ld de, 0x0000
    ld hl, 0x8000
    ld bc, 0x4000
    ldir
    ; executa o nucleo

    jp 0x0000

panic_if_fail:
    ret c
panic:
    ; desliga o motor do disco
    ld iy, [0xf347] ; Chama a funcao que desliga o motor do disco (ignorado em alguns modelos. ex.: DDX 3.0)
    ld ix, 0x4029
    call 0x1c
    ; HACK - para contornar limitação da BIOS da DDX 3.0 é enviado o comando de desligar o motor manualmente
    ld a, 64
    out [0xd4], a
    ; imprime e encerra
    call tty_print
    db "[FALHA]",0
    di
    halt
    jp $

; hl = string 1
; de = string 2
; b = tamanho
; compara duas strings
; ret cf= 1=igual | 0 = diferente
str_equal:
    scf
    push hl
    push de
    push bc
    push af
    .loop:
        ld a, [de]
        cp [hl]
        jr nz, .fail
        inc hl
        inc de
        djnz .loop
        jr .end
    .fail:
    pop af
    ccf
    push af
    .end:
    pop af
    pop bc
    pop de
    pop hl
    ret

tty_print_ok:
    call tty_print
    db "[OK]",13,10,0
    ret

tty_print:
    push ix
    push hl
    push af
    ld ix, 6
    add ix, sp
    ld l, [ix+0]
    ld h, [ix+1]
    .loop:
        ld a, [hl]
        inc hl
        cp 0
        jr z, .done
        call 0xa2
        jr .loop
    .done:
    ld [ix+0], l
    ld [ix+1], h
    pop af
    pop hl
    pop ix
    ret


; de = destino na memoria
; hl = bloco no disco
; le um bloco do disco para memoria
; ret cf = 1=lido | 0=falha
disk_read:
    push hl
    push de
    ld de, buf_disk
    call disk_read_raw
    pop de
    push af
    ld hl, buf_disk
    ld bc, buf_disk_size
    ldir
    pop af
    pop hl
    ret


; de = destino na memoria
; hl = bloco no disco
; le um bloco do disco para memoria
disk_read_raw:
    scf
    push hl
    push de
    push bc
    push af
    ex de, hl
    ld b, 3
    .try:
        call tty_print
        db ".",0
        push bc
        xor a
        ld a, [cfg_media_type]
        ld c, a
        ld a, [cfg_disk_id]
        ld b, 1
        push hl
        push de
        call 0x144
        pop de
        pop hl
        pop bc
        jr nc, .ok
            djnz .try
            call tty_print
            db "[ERROR]",0
            jr .try
    .ok:
    pop af
    pop bc
    pop de
    pop hl
    ret

