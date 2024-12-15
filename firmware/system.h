#if !defined( SYSTEM_H )
#define SYSTEM_H 1

#include <stdint.h>

static constexpr uint32_t N_DATA_PINS = 8;
static constexpr uint32_t BASE_DATA_PIN = 22;
static constexpr uint32_t DATA_PIN_MASK = (( 1 << N_DATA_PINS ) - 1) << BASE_DATA_PIN;

static constexpr uint32_t N_OE_PINS = 2;
static constexpr uint32_t BASE_OE_PIN = 20;
static constexpr uint32_t OE_PIN_MASK = (( 1 << N_OE_PINS ) - 1) << BASE_OE_PIN;

#if defined(FEATURE_CLOCK)
static constexpr uint32_t CLOCK_PIN = 17;
static constexpr uint32_t N_ADDR_PINS = 17;
#else
static constexpr uint32_t N_ADDR_PINS = 17;
#endif

static constexpr uint32_t BASE_ADDR_PIN = 0;
static constexpr uint32_t ADDR_PIN_MASK = (( 1 << N_ADDR_PINS ) - 1) << BASE_ADDR_PIN;


static constexpr uint32_t BUF_OE_PIN = 19;
static constexpr uint32_t BUF_OE_PIN_MASK = 1 << BUF_OE_PIN;

static constexpr uint32_t TCA_EXPANDER_PIN = 18;
static constexpr uint32_t TCA_EXPANDER_PIN_MASK = 1 << TCA_EXPANDER_PIN;

static constexpr uint32_t TCA_LINK_PIN = 1;
static constexpr uint32_t TCA_READ_PIN = 2;
static constexpr uint32_t TCA_RESET_VALUE_PIN = 3;
static constexpr uint32_t TCA_RESET_PIN = 4;

static constexpr uint32_t ROM_SIZE = 0x40000;
static constexpr uint32_t ADDR_MASK = (( 1 << N_ADDR_PINS ) - 1);
static constexpr uint32_t FLASH_SIZE = 2 * 1024 * 1024;

static constexpr uint32_t STATUS_PIO_INIT = 0x00000001;

#endif // SYSTEM_H