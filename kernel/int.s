section text

    global int_handler
    int_handler:
        push af
        push ix
        push iy
        push hl
        push bc
        push de
        ex af, af'
        exx
        push af
        push hl
        push bc
        push de

        call vdp_int_handler
        call keyb_int_handler
        call disk_int_handler

        call proc_save_state
        call proc_set_next_process
        call proc_restore_state
        
        pop de
        pop bc
        pop hl
        pop af
        ex af, af'
        exx
        pop de
        pop bc
        pop hl
        pop iy
        pop ix
        pop af
        ei
        ret