#if !defined(PIO_PROGRAMS_H)
#define PIO_PROGRAMS_H 1

#include "hardware/pio.h"

struct PIOProgram
{
    int8_t sm;
    int8_t offset;
    int8_t pio_index;
    pio_sm_config (*config_func)(uint);

    void reset()
    {
        sm = 0;
        offset = -1;
        pio_index = PIO_NUM(pio0);
        config_func = nullptr;
    }

    bool valid() const
    {
        return offset >= 0;
    }

    PIO pio()
    {
        return PIO_INSTANCE(pio_index);
    }

    pio_sm_config config() const
    {
        if (config_func)
            return config_func(offset);
        else
            return pio_sm_config();
    }
};

#define PRG_LOCAL(prg, p, s, o, c)                                                                                     \
    PIO p = prg.pio();                                                                                                 \
    int s = prg.sm;                                                                                                    \
    int o = prg.offset;                                                                                                \
    pio_sm_config c = prg.config();

extern PIOProgram prg_comms_detect;
extern PIOProgram prg_comms_clock;
extern PIOProgram prg_write_tca_bits;
extern PIOProgram prg_data_output;
extern PIOProgram prg_set_output_enable;
extern PIOProgram prg_set_pindir_hi;
extern PIOProgram prg_set_pindir_lo;
extern PIOProgram prg_report_data_access;

bool pio_programs_init();

#endif
