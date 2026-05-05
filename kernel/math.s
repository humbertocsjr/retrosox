section text

    global math_ucp16
    ; hl = valor 1
    ; de = valor 2
    ; compara dois valores
    math_ucp16:
        or a
        sbc hl, de
        add hl, de
        ret

    global math_udiv16
    ; hl = valor 1
    ; de = valor 2
    ; divide dois valores
    ; ret: hl = retorno
    ; ret: de = modulo
    ; algoritimo baseado no livro: Programing the Z80 - 3rd Edition - Rodney Zaks
    math_udiv16:
        push af
        push bc
        ld b, 16
        ld c, l
        ld a, h
        ld hl, 0
        .divisao_repeticao:
            rl c
            rla 
            adc hl, hl
            sbc hl, de
            jr nc, .divisao_ignora
                add hl, de
            .divisao_ignora:
            ccf
            djnz .divisao_repeticao
        rl c
        rla
        ld e, c
        ld d, a
        ex de, hl
        pop bc
        pop af
        ret

    global math_umul16
    ; hl = valor 1
    ; de = valor 2
    ; multiplica dois valores
    ; ret: hl = retorno
    ; algoritimo baseado no livro: Programing the Z80 - 3rd Edition - Rodney Zaks
    math_umul16:
        push af
        push bc
        ld b, 16
        ld a, l
        ld c, h
        ld hl, 0
        .multiplica_repeticao:
            srl c
            rra
            jr nc, .multiplica_ignora
                add hl, de
            .multiplica_ignora:
            ex de, hl
            add hl, hl
            ex de, hl
            djnz .multiplica_repeticao
        ld de, 0
        pop bc
        pop af
        ret
