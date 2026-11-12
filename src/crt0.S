// Module header for a code module the game's loader (rtld) loads beside main: the word at +4 points to the MOD0
// header, which tells rtld where the dynamic section and the zeroed memory are. rtld keeps its own record of the
// module at the start of that zeroed memory. Our code starts when rtld calls the DT_INIT function (src/main.cpp).
    .hidden __mod0
    .hidden __bss_start__
    .hidden __bss_end__
    .hidden __module_object
    .hidden _DYNAMIC
    .section .text.crt0, "ax"
    .global __module_start
    .hidden __module_start
__module_start:
    ret
    .word __mod0 - __module_start
    // rtld reads the word at +8 as the offset of an optional module record (Nintendo's modules point it past MOD0);
    // zero means "none". Alignment padding here would be NOP instructions, which rtld takes as a wild offset.
    .word 0
    .word 0

    .section .rodata.mod0, "a"
    .align 4
__mod0:
    .ascii "MOD0"
    .word _DYNAMIC - __mod0
    .word __bss_start__ - __mod0
    .word __bss_end__ - __mod0
    .word 0                         // eh_frame_hdr start (none)
    .word 0                         // eh_frame_hdr end
    .word __module_object - __mod0  // rtld's record of this module
