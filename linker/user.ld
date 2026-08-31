ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);
    data    PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC;
}

SECTIONS
{
    . = 0;

    .text : {
        *(.text.unlikely)
        *(.text.startup)
        *(.text)
        *(.text.*)
        *(.init)
        *(.fini)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .rela.dyn : { *(.rela.dyn) } :data
    .rela.got : { *(.rela.got) } :data
    .relr.dyn : { *(.relr.dyn) } :data
    .rel.dyn : { *(.rel.dyn) } :data
    .rel.got : { *(.rel.got) } :data

    .data : {
        *(.data)
        *(.data.*)
        *(.tdata)
        *(.got)
        *(.got.*)
        *(.init_array*)
        *(.fini_array*)
    } :data

    .dynamic : { *(.dynamic) } :data :dynamic

    .bss : {
        *(.bss)
        *(.bss.*)
        *(.tbss)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.gnu.version*)
    }
}
