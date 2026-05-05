; Controlador de Teclado
; ======================
;
; Ações durante a interrupção:
; - Le teclas pressionadas
; - Registra na lista tecla pressionada
; - Decrementa todos os itens da lista
; - Os itens que alcancem zero, são removidos

modifier_shift: equ 0x10
modifier_graph: equ 0x20
modifier_ctrl: equ 0x40
count_mask: equ 0x0f

obj_keyb: equ 2
    .modifier_and_count: equ 0
    .key: equ 1 

keyb_list__max: equ 16


section bss

    keyb_stack_base: resb 200
    keyb_stack_top:
    keyb_list: resb keyb_list__max * obj_keyb

section text

    global keyb_init
    keyb_init:
        ; inicia o processo de gerenciamento do teclado
        ld hl, keyb_main
        ld de, keyb_stack_top
        call proc_new
        
        ; define dispositivo
        ld b, dev_id_keyb
        call dev_set_process
        ret

    keyb_main:
        .event_loop:
        halt
        jp .event_loop


    global keyb_int_handler
    keyb_int_handler:
        ret