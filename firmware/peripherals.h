#if !defined(PERIPHERALS_H)
#define PERIPHERALS_H 1

#include <stddef.h>
#include <stdint.h>

enum class ResetLevel : uint8_t
{
    Low,
    High,
    Z
};

void reset_set(ResetLevel level);
ResetLevel reset_get();
void reset_to_string(ResetLevel level, char *s, size_t sz);
bool reset_from_string(const char *s, ResetLevel *level);

void trigger_identify_led();

void peripherals_init();

#endif
