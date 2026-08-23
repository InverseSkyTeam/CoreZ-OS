// 单核模式APIC
#ifndef APIC_H
#define APIC_H

#include <stdint.h>

int apic_init(void);

void lapic_eoi(void);

#endif