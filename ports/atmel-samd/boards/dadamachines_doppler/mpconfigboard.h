#define MICROPY_HW_BOARD_NAME "DADA MACHINES - DOPPLER"
#define MICROPY_HW_MCU_NAME "samd51g19a"

#define CIRCUITPY_MCU_FAMILY samd51

#define DOPPLER_VER 1 // "ver V1.0"
// #define DOPPLER_VER 2 // "rev B"

#if DOPPLER_VER == 1
// This is for "V1.0"
#define MICROPY_HW_LED_STATUS (&pin_PA23)
#else
// This is for Rev B
#define MICROPY_HW_LED_STATUS (&pin_PA22)
#endif

// #define MICROPY_HW_APA102_MOSI (&pin_PB03)
// #define MICROPY_HW_APA102_SCK (&pin_PB02)

// #define CIRCUITPY_BITBANG_APA102

// These are pins not to reset.
// QSPI Data pins
#define MICROPY_PORT_A (PORT_PA08 | PORT_PA09 | PORT_PA10 | PORT_PA11)
// DotStar pins, QSPI CS, and QSPI SCK
#define MICROPY_PORT_B (PORT_PB02 | PORT_PB03 | PORT_PB10 | PORT_PB11)
#define MICROPY_PORT_C (0)
#define MICROPY_PORT_D (0)

#define AUTORESET_DELAY_MS 500

// If you change this, then make sure to update the linker scripts as well to
// make sure you don't overwrite code
#define CIRCUITPY_INTERNAL_NVM_SIZE 8192

#define BOARD_FLASH_SIZE (FLASH_SIZE - 0x4000 - CIRCUITPY_INTERNAL_NVM_SIZE)

#define DEFAULT_I2C_BUS_SCL (&pin_PA13)
#define DEFAULT_I2C_BUS_SDA (&pin_PA12)

#if DOPPLER_VER == 1
// this seemed to work for the "V1" board...
#define DEFAULT_SPI_BUS_SCK (&pin_PB09)
#define DEFAULT_SPI_BUS_MOSI (&pin_PB08)
#define DEFAULT_SPI_BUS_MISO (&pin_PB11)
#else
// not sure if this is what my board has...?
#define DEFAULT_SPI_BUS_SCK (&pin_PA01)
#define DEFAULT_SPI_BUS_MOSI (&pin_PA00)
#define DEFAULT_SPI_BUS_MISO (&pin_PB23)
#endif

// error w/ v1.0 board compiling w/ this... not sure why...
#if DOPPLER_VER != 1
#define DEFAULT_UART_BUS_RX (&pin_PA16)
#define DEFAULT_UART_BUS_TX (&pin_PA17)
#endif

// USB is always used internally so skip the pin objects for it.
#define IGNORE_PIN_PA24 1
#define IGNORE_PIN_PA25 1
