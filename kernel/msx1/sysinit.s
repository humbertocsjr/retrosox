global sysinit
sysinit:
    ; Inicializa variaveis globais baseado nas originais da bios

    ; copia slot do controlador de disco 0
    ld a, [0xfb22]
    ld [sysvar_disk_0_slot], a

    ; copia slot do controlador de disco 1
    ld a, [0xfb24]
    ld [sysvar_disk_1_slot], a

    call vdp_init
    
    ret