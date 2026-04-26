obj_proc: equ 16
    .next: equ 0
    .stack_pointer: equ 2
    .slot_1: equ 4
    .slot_2: equ 5
    .page_1_0: equ 6
    .page_1_1: equ 7
    .page_2_0: equ 8
    .page_2_2: equ 9
    .stdin: equ 10
    .stdout: equ 12
    .stderr: equ 14
proc_list__max: equ 128

section bss

    proc_list: equ obj_proc * proc_list__max

section text

global proc_init
proc_init:
    ; define ponteiro do processo atual
    ld ix, proc_list
    ld [sysvar_proc_current], ix
    ; define ponteiro no processo atual para o proximo processo (inicialmente o mesmo)
    ld hl, proc_list
    ld [ix+obj_proc.next], l
    ld [ix+obj_proc.next+1], h

    ret

global proc_set_next_process
proc_set_next_process:
    ld ix, [sysvar_proc_current]
    ld l, [ix+obj_proc.next]
    ld h, [ix+obj_proc.next+1]
    ld [sysvar_proc_current], hl
    ret

global proc_save_state
proc_save_state:
    ; salva retorno dessa funcao
    pop de
    ; define ponteiro para processo atual
    ld ix, [sysvar_proc_current]
    ; salva o ponteiro da pilha atual
    ld hl, 0
    add hl, sp
    ld [ix+obj_proc.stack_pointer], l
    ld [ix+obj_proc.stack_pointer+1], h
    ; restaura retorno dessa funcao ainda na pilha antiga
    push de
    ret

global proc_restore_state
proc_restore_state:
    ; salva retorno dessa funcao
    pop de
    ; define ponteiro para processo atual
    ld ix, [sysvar_proc_current]
    ; restaura ponteiro da pilha do processo atual
    ld l, [ix+obj_proc.stack_pointer]
    ld h, [ix+obj_proc.stack_pointer+1]
    ld sp, hl
    ; restaura retorno dessa funcao na nova pilha
    push de
    ret

global proc_new
; hl = code start
; de = stack top
proc_new:
    ret