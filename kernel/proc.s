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

    proc_list: resb obj_proc * proc_list__max
    proc_taskswitch_enabled: resb 1

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

    ; ativa troca de tarefas
    ld a, 1
    ld [proc_taskswitch_enabled], a

    ret

global proc_set_next_process
proc_set_next_process:
    ; se troca de tarefas estiver desativado encerra
    ld a, [proc_taskswitch_enabled]
    cp 0
    ret z

    ; troca a tarefa atual pela do proximo processo
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
; hl = code start (inicio do codigo executavel)
; de = stack top (topo da pilha)
; cria um novo processo
; ret cf = 1=ok | 0=estouro da lista de processos
; ret hl = processo
; ret de = novo topo da pilha
proc_new:
    push ix
    push iy
    push bc
    ; salva estado atual da troca de tarefas
    ld a, [proc_taskswitch_enabled]
    ld c, a
    push bc
    push de
    push hl


    ; desativa troca de tarefas
    ld a, 0
    ld [proc_taskswitch_enabled], a

    ; inicializa ponteiro para inicio da lista de processos
    ld ix, proc_list

    ld b, proc_list__max
    dec b
    .search:
        ; vai para proxima tarefa
        ld de, obj_proc
        add ix, de

        ; procura por uma tarefa vazia
        ld a, [ix+obj_proc.next]
        or [ix+obj_proc.next + 1]
        
        ; se encontra vai para criacao da tarefa
        jr z, .found

        ; senao procura na proxima
        djnz .search
    
    ; se exauriu a busca desiste
    xor a
    pop hl
    ld hl, 0
    push hl
    jp .end

    .found:
    ; se encontrou um processo vazio, cria

    ; define ponteiro auxiliar para processo do nucleo
    ld iy, proc_list 

    ; copia proximo do processo do nucleo para novo processo
    ld e, [iy + obj_proc.next]
    ld d, [iy + obj_proc.next + 1]
    ld [ix + obj_proc.next], e
    ld [ix + obj_proc.next + 1], d

    ; copia ponteiro do novo processo para o proximo processo do processo do nucleo
    push ix
    pop de
    ld [iy + obj_proc.next], e
    ld [iy + obj_proc.next + 1], d

    ; salva ponteiros no processo novo
    pop de ; ponteiro do inicio do codigo executavel
    pop iy ; ponteiro para pilha
    ld [iy +- 1], d  ; reti
    ld [iy +- 2], e
    ld a, 0
    ld [iy +- 3], a  ; af
    ld [iy +- 4], a
    ld [iy +- 5], a  ; ix
    ld [iy +- 6], a
    ld [iy +- 7], a  ; iy
    ld [iy +- 8], a
    ld [iy +- 9], a  ; hl
    ld [iy +- 10], a
    ld [iy +- 11], a ; bc
    ld [iy +- 12], a
    ld [iy +- 13], a ; de
    ld [iy +- 14], a
    ld [iy +- 15], a ; af'
    ld [iy +- 16], a
    ld [iy +- 17], a ; hl'
    ld [iy +- 18], a
    ld [iy +- 19], a ; bc'
    ld [iy +- 20], a
    ld [iy +- 21], a ; de'
    ld [iy +- 22], a
    ld bc, 22
    push iy
    pop hl
    xor a
    sbc hl, bc
    
    ; copia novo topo da pilha para o ponteiro
    ld [ix + obj_proc.stack_pointer], l
    ld [ix + obj_proc.stack_pointer + 1], h

    ; restaura para retorno o topo da pilha
    push hl

    ; substitui ponteiro do hl salvo na pilha, pelo processo novo
    push ix

    ; prepara estado para retorno
    xor a
    scf

    .end:
    pop hl
    pop de
    pop bc
    push af
    ; restaura troca de tarefas
    ld a, c
    ld [proc_taskswitch_enabled], a
    pop af
    pop bc
    pop iy
    pop ix

    ret