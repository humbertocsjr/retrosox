
section text

    global _start
    _start:
        ; salva ponteiro de retorno
        pop ix

        ; limpa BSS
        exx
        ld a, 0
        ld bc, __bss_size__ - 1
        ld de, __bss_start__ + 1
        ld hl, __bss_start__
        ld [hl], a
        ldir
        exx

        ; define pilha e restaura na pilha ponteiro de retorno
        ld sp, stack_top
        push ix

        ; chama funcao principal
        call main

        ret

section bss

stack_base: resb 512
stack_top:

; ============
; - SYSCALLS -
; ============

global syscall_hard_reset
syscall_hard_reset: equ 0x0000

global syscall_ucp16
; hl = valor 1
; de = valor 2
; compara dois valores
syscall_ucp16: equ 0x0003

global syscall_udiv16
; hl = valor 1
; de = valor 2
; divide dois valores
; ret: hl = retorno
; ret: de = modulo
syscall_udiv16: equ 0x0006

global syscall_umul16
; hl = valor 1
; de = valor 2
; multiplica dois valores
; ret: hl = retorno
syscall_umul16: equ 0x0009

global syscall_get_current_process
; le o processo atual
; ret hl = processo atual
syscall_get_current_process: equ 0x000c

; ===========
; - SYSVARS -
; ===========

global sysvar_disk_0_slot
sysvar_disk_0_slot: equ 0xc200 ; 1 byte

global sysvar_disk_1_slot
sysvar_disk_1_slot: equ 0xc201 ; 1 byte

global sysvar_vdp_current
sysvar_vdp_current: equ 0xc202 ; 2 bytes

global sysvar_proc_current
sysvar_proc_current: equ 0xc204 ; 2 bytes