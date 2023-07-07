#if !defined( STATEMACHINES_H )
#define STATEMACHINES_H 1

#include "hardware/pio.h"

void init_data_bus_programs();
io_wo_32 *get_data_bus_fifo();

void start_comms_programs(uint32_t addr, uint32_t byte_offset);
void end_comms_programs();
bool comms_poll_write(uint8_t *out);
bool comms_poll_read();

#endif // STATEMACHINES_H