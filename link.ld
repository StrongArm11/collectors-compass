OUTPUT_ARCH(aarch64)
ENTRY(__module_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dyn PT_DYNAMIC;
}
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.crt0)) *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { KEEP(*(.rodata.mod0)) *(.rodata .rodata.*) } :rodata
    .dynsym : { *(.dynsym) } :rodata
    .dynstr : { *(.dynstr) } :rodata
    .hash : { *(.hash) } :rodata
    .gnu.hash : { *(.gnu.hash) } :rodata
    .rela.dyn : { *(.rela.dyn) *(.rela.*) } :rodata
    .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .got : { *(.got) *(.got.plt) } :data
    .dynamic : { *(.dynamic) } :data :dyn
    . = ALIGN(16);
    HIDDEN(__bss_start__ = .);
    .bss : { HIDDEN(__module_object = .); . += 0x400; *(.bss .bss.*) *(COMMON) . = ALIGN(16); } :data
    HIDDEN(__bss_end__ = .);
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note.GNU-stack) *(.interp) *(.plt) *(.iplt) }
}
