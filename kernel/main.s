_header:
syscall_hard_reset:
jp _start

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
    ld bc, __bss_size__
    ld de, __bss_start__
    call mem_fill_byte

    ; inicializa processos
    call proc_init

    ; inicializa codigo da maquina 
    call sysinit

    ; ativa interrupcoes
    ei

    nop
    nop
    halt
    jp $
