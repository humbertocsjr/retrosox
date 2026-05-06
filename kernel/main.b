
main()
{
    extrn proc_init, ipc_init, dev_init, fs_init, sysinit;

    proc_init();
    ipc_init();
    dev_init();
    fs_init();
    sysinit();

    asm("ei");

    for(;;)
    {
        asm("ei");
        asm("halt");
    }
}