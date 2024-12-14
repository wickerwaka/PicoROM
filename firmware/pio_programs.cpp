#include "pio_programs.h"

#include "data_bus.pio.h"
#include "comms.pio.h"
#include <hardware/pio.h>


PIOProgram prg_comms_read;
PIOProgram prg_comms_write;
PIOProgram prg_tca;
PIOProgram prg_data_bus;
PIOProgram prg_data_oe;
PIOProgram prg_data_pindir_hi;
PIOProgram prg_data_pindir_lo;
PIOProgram prg_data_report;

#define add_program(p, s, name, prg) \
    do \
    { \
        prg.pio_index = PIO_NUM(p); \
        prg.sm = s; \
        prg.config_func = name ## _program_get_default_config; \
        prg.offset = pio_add_program(p, &name ## _program); \
        if (prg.offset < 0) return false; \
    } while(false)


void pio_programs_reset()
{
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);

    prg_comms_read.reset();
    prg_comms_write.reset();
    prg_tca.reset();
    prg_data_bus.reset();
    prg_data_oe.reset();
    prg_data_pindir_hi.reset();
    prg_data_pindir_lo.reset();
}


bool pio_programs_init()
{
    pio_programs_reset();

    add_program(pio0, 0, pindir_fast, prg_data_pindir_lo);
    prg_data_pindir_hi = prg_data_pindir_lo;
    prg_data_pindir_hi.sm = 1;
    add_program(pio0, 2, output_enable_report, prg_data_report);
    add_program(pio0, 3, output, prg_data_bus);

    add_program(pio1, 0, output_enable_fast, prg_data_oe);
    add_program(pio1, 1, detect_read, prg_comms_read);
    add_program(pio1, 2, detect_write, prg_comms_write);
    add_program(pio1, 3, tca5405, prg_tca);

    return true;
}


