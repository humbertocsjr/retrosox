section text

    _header:
    syscall_hard_reset:
    jp _start

    syscall_ucp16:
    jp math_ucp16

    syscall_udiv16:
    jp math_udiv16

    syscall_umul16:
    jp math_umul16

    syscall_get_current_process:
    jp proc_get_current_process

    times _header + 0x38 - $ db 0
    jp int_handler

    times _header + 0x80 - $ db 0
    global _start
    _start:
        ; para interrupcoes
        di

        ; define pilha do nucleo
        ld sp, kernel_stack_top

        ; inicializa memoria BSS
        ld a, 0
        ld bc, __bss_size__ - 1
        ld de, __bss_start__ + 1
        ld hl, __bss_start__
        ld [hl], a
        ldir

        ; inicializa processos
        call proc_init

        ; inicializa comunicacao inter processos
        call ipc_init

        ; inicializa gerenciador de dispositivos
        call dev_init

        ; inicializa sistema de arquivos
        call fs_init

        ; inicializa codigo da maquina 
        call sysinit

        ; ativa interrupcoes
        ei

        .idle:
            ei
            halt
            jp .idle
