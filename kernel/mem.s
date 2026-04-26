
global mem_fill_byte
; preenche um trecho da memoria
; de = destino
; a = caractere
; bc = tamanho
mem_fill_byte:
    push af
    push bc
    push de
    push hl
    ; verifica se o contador eh zero
    ld l, a
    ld a, b
    or c
    ; se for encerra
    jr z, .end
    ; senao grava na posicao destino o caractere
    ld a, l
    ld [de], a
    ; copia o ponteiro para a origem
    ld l, e
    ld h, d
    ; incrementa ponteiro de destino
    inc de
    ; decrementa contador pois ja gravou o primeiro
    dec bc
    ; verifica se o contador eh zero
    ld a, b
    or c
    ; se for encerra
    jr z, .end
    ; senao copia
    ldir
    .end:
    pop hl
    pop de
    pop bc
    pop af
    ret
