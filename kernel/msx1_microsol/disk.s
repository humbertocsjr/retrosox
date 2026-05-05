


section bss

    disk_stack_base: resb 200
    disk_stack_top:

    disk_buffer: equ 0xc000
    disk_port_status: equ 0xd0
    disk_port_command: equ 0xd0
    disk_port_sector: equ 0xd2
    disk_port_data: equ 0xd3
    disk_port_drive: equ 0xd4

    disk_motor_enabled: resb 1
    disk_motor_counter: resb 1

section text

    global disk_init
    disk_init:
        ; inicia o processo do controlador de disco
        ld hl, disk_main
        ld de, disk_stack_top
        call proc_new
        
        ; define dispositivo
        ld b, dev_id_disk
        call dev_set_process
        ret

    disk_main:
        ; desliga motor
        ld a, 64
        out [0xd4], a

        ; marca motor como desligado
        ld a, 0
        ld [disk_motor_enabled], a
        ld [disk_motor_counter], a

        .event_loop:
            halt
            jp .event_loop


    global disk_int_handler
    disk_int_handler:
        ld a, [disk_motor_enabled]
        cp 0
        ret z
        ld a, [disk_motor_counter]
        dec a
        ld [disk_motor_counter], a
        cp 0
        ret nz
        ; desliga motor
        ld a, 64
        out [0xd4], a
        ; marca motor como desligado
        ld a, 0
        ld [disk_motor_enabled], a
        ret

    disk_wait:
        push bc
        push af
        ld bc, 4224
        .wait:
            dec bc
            ld a, c
            or b
            jp nz, .wait
        pop af
        pop bc
        ret

    disk_wait_ready:
        push af
        .loop:
            in a, [0xd0]
            rrca
            jr c, .loop
        pop af
        ret

    disk_send_cmd:
        call disk_wait_ready
        out [0xd0], a
        ; aguarda um tempo curto
        push hl
        nop
        pop hl
        ret

    
    ; hl = endereco
    ; calcula endereco no disco
    ; ret: bc = cilindro
    ; ret: h = cabeca
    ; ret: l = setor
    disk_calc_cha:
        ; salva os registradores
        push af
        push de
        ; divide o endereco pelos setores
        ld de, 9
        call math_udiv16
        xor d ; zera o valor do cabeçote
        ; do resultado da divisão verifica a parte do cabeçote (bit 0)
        bit 0, l
        jr z, .head_is_zero
            inc d ; se o cabeçote for 1 define o valor do cabeça
        .head_is_zero:
        ; divide o resultado pelo numero de cabeças
        srl h ; efetua um shift right para dividir por 2 (cabeças), sobrando o cilindro
        rr l
        ld b, h ; define BC = cilindro
        ld c, l
        ld h, d ; h = cabeca
        ld l, e ; l = setor
        inc l ; incrementa o setor por ser valor base 1
        ; restaura os registradores
        pop de
        pop af
        ret

    ; hl = endereco
    ; de = destino na RAM
    ; le um bloco do disco
    ; ret: cf = 1=sucesso
    disk_read_block:
        ; salva os registradores
        push hl
        push de
        push bc
        push af
        ; calcula o endereço em Cilindros/Cabeças/Setores
        call disk_calc_cha
        ; c = cilindro
        ; h = cabeca
        ; l = setor
        call disk_wait_ready ; aguarda o disco esta pronto para receber comandos
        ld a, 0 ; informa que o disco ainda nao esta em atividade (uso da interrupcao)
        ld [disk_motor_enabled], a
        ld a, h ; calcula a seleçao da unidade/cabeça
        rlca
        rlca
        rlca
        rlca
        and 0x10
        or 0x21
        out [disk_port_drive], a
        ld a, 0xd0 ; 
        out [disk_port_command], a
        call disk_wait
        call disk_wait_ready
        ld a, c
        out [disk_port_data], a
        ld a, 0x10
        call disk_send_cmd
        call disk_wait_ready
        ; le o setor
        ld a, l
        out [disk_port_sector], a
        ex de, hl
        ld de, 512
        ld c, disk_port_data
        ld a, 0x80
        call disk_send_cmd
        push hl
        di
        .read_disk:
            in a, [disk_port_status]
            rra ; verifica o bit 0 -> se deve continuar a leitura
            jr nc, .read_disk_end
            rra ; verifica o bit 1 -. se deve continuar esperando para ler o proximo byte
            jr nc, .read_disk
            ini ; le um byte e armazena no ponteiro HL (incrementa o ponteiro)
            dec de ; incrementa o contador de bytes lidos
            ld a, e
            or d
            jr nz, .read_disk
        .read_disk_end:
        ld a, 1 ; intorma para a interrupcao que o motor esta ativo
        ld [disk_motor_enabled], a
        ld a, 255 ; informa para a interrupcao a contagem de tempo para desativar o motor
        ld [disk_motor_counter], a
        ld a, e
        or d
        jr z, .read_ok
            ei
            pop bc
            ld a, b
            pop bc
            pop de
            pop hl
            scf
            ccf
            ret
        .read_ok:
            pop hl
            ei
            pop bc
            ld a, b
            pop bc
            pop de
            pop hl
            scf
            ret