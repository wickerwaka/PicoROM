#if !defined(ROM_H)
#define ROM_H 1

#include "hardware/pio.h"

void rom_init_programs();

void rom_service_start();
void rom_service_stop();

uint8_t *rom_get_buffer();

bool rom_check_oe();

#if defined(FEATURE_TCA)
void tca_set_pins(uint8_t pins);
void tca_set_pin(int pin, bool en);
#endif

#endif // ROM_H