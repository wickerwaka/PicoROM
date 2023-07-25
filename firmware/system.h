#if !defined( SYSTEM_H )
#define SYSTEM_H 1

#include <stdint.h>

static constexpr uint32_t N_DATA_PINS = 8;
static constexpr uint32_t BASE_DATA_PIN = 22;

static constexpr uint32_t N_OE_PINS = 2;
static constexpr uint32_t BASE_OE_PIN = 20;

#if ACTIVITY_LED==1
static constexpr uint32_t N_ADDR_PINS = 18;
static constexpr uint32_t BASE_ADDR_PIN = 0;
static constexpr uint32_t N_BUF_OE_PINS = 1;
static constexpr uint32_t BASE_BUF_OE_PIN = 19;
static constexpr uint32_t ACTIVITY_LED_PIN = 18;
#elif OUTPUT_BUFFER==1
static constexpr uint32_t N_ADDR_PINS = 19;
static constexpr uint32_t BASE_ADDR_PIN = 0;
static constexpr uint32_t N_BUF_OE_PINS = 1;
static constexpr uint32_t BASE_BUF_OE_PIN = 19;
#else
static constexpr uint32_t N_ADDR_PINS = 20;
static constexpr uint32_t BASE_ADDR_PIN = 0;
static constexpr uint32_t N_BUF_OE_PINS = 0;
static constexpr uint32_t BASE_BUF_OE_PIN = 0;
#endif


static constexpr uint32_t ROM_SIZE = 0x40000;
static constexpr uint32_t ADDR_MASK = ROM_SIZE - 1;
static constexpr uint32_t FLASH_SIZE = 2 * 1024 * 1024;

#endif // SYSTEM_H