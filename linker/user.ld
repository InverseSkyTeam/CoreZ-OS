ENTRY(_start)

PHDRS
{
    text  PT_LOAD FLAGS(5);
    data  PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x8048000;

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

    .data : {
        *(.data)
        *(.data.*)
        *(.tdata)
        *(.got)
        *(.got.*)
        *(.rela.dyn)
        *(.rela.got)
        *(.rel.dyn)
        *(.rel.got)
        *(.init_array*)
        *(.fini_array*)
    } :data

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
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
    }
}
