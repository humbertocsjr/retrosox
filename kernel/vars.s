section bss

global sysvar_disk_0_slot
sysvar_disk_0_slot: resb 1

global sysvar_disk_1_slot
sysvar_disk_1_slot: resb 1

global sysvar_vdp_current
sysvar_vdp_current: resb 2

global sysvar_proc_current
sysvar_proc_current: resb 2

kernel_stack_base: resb 200
global kernel_stack_top
kernel_stack_top: