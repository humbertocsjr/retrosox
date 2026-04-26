
global _start
_start:
    call _main
    ret

; SYSCALLS

global syscall_hard_reset
syscall_hard_reset: equ 0x0000

; SYSVARS

global sysvar_disk_0_slot
sysvar_disk_0_slot: equ 0xc200 ; 1 byte

global sysvar_disk_1_slot
sysvar_disk_1_slot: equ 0xc201 ; 1 byte

global sysvar_vdp_current
sysvar_vdp_current: equ 0xc202 ; 2 bytes

global sysvar_proc_current
sysvar_proc_current: equ 0xc204 ; 2 bytes