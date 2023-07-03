.cpu cortex-m0
.thumb

.global read_handler

.thumb_func
read_handler:
    // r0 = ram_base
    // r1 = mask
    // r2 = tx_fifo

    ldr r5, =0xd0000004

loop:
    ldr r3, [r5]
    and r3, r1
    ldrb r3, [r0, r3]
    //ldr r3, =0x00000001
    strb r3, [r2]
    b loop

