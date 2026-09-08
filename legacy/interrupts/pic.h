#ifndef PIC_H
#define PIC_H

#include "types.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define ICW1_ICW4    0x01
#define ICW1_SINGLE  0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL   0x08
#define ICW1_INIT    0x10

#define ICW4_8086    0x01
#define ICW4_AUTO    0x02
#define ICW4_BUF_SLAVE 0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM    0x10

#define PIC_MASTER_OFFSET 0x20
#define PIC_SLAVE_OFFSET  0x28

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_send_eoi(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

#endif
