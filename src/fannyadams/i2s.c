#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/spi.h>

#include "i2s.h"
#include "systick.h"

static int32_t buffer1[AUDIO_BUFFER_SIZE];
static int32_t buffer2[AUDIO_BUFFER_SIZE];
static uint32_t pingpong = 0;

static dma_callback callback;

uint32_t I2S_DMA_TICK;

volatile uint32_t i2s_enabled;

static void dma_setup(void) {
    rcc_periph_clock_enable(RCC_DMA1);

    dma_stream_reset(DMA1, DMA_STREAM4);
    dma_set_priority(DMA1, DMA_STREAM4, DMA_SxCR_PL_LOW);

    dma_set_memory_size(DMA1, DMA_STREAM4, DMA_SxCR_MSIZE_16BIT);       // halfword increments
    dma_set_peripheral_size(DMA1, DMA_STREAM4, DMA_SxCR_PSIZE_16BIT);   // halfword peripheral size
    dma_enable_memory_increment_mode(DMA1, DMA_STREAM4);
    dma_enable_circular_mode(DMA1, DMA_STREAM4);
    dma_enable_double_buffer_mode(DMA1, DMA_STREAM4);
    dma_set_transfer_mode(DMA1, DMA_STREAM4, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_peripheral_address(DMA1, DMA_STREAM4, (uint32_t) &SPI_DR(SPI2));

    dma_set_memory_address(DMA1, DMA_STREAM4, (uint32_t) &buffer1);
    dma_set_memory_address_1(DMA1, DMA_STREAM4, (uint32_t) &buffer2);
    dma_set_number_of_data(DMA1, DMA_STREAM4, AUDIO_BUFFER_SIZE*2);

    // interruptnik
    nvic_enable_irq(NVIC_DMA1_STREAM4_IRQ);
    dma_enable_transfer_complete_interrupt(DMA1, DMA_STREAM4);
    //dma_enable_half_transfer_interrupt(DMA1, DMA_STREAM4);

    dma_channel_select(DMA1, DMA_STREAM4, DMA_SxCR_CHSEL_0);
    dma_enable_stream(DMA1, DMA_STREAM4);
}

void dma1_stream4_isr(void) {
    dma_clear_interrupt_flags(DMA1, DMA_STREAM4, DMA_TCIF | DMA_HTIF);
    I2S_DMA_TICK++;// = Clock_Get();
    pingpong = (pingpong + 1) & 1;
    if (callback) {
        (*callback)();
    }
}

int32_t* I2S_GetBuffer() {
    return pingpong ? buffer1 : buffer2;
}

void I2S_SetCallback(dma_callback cb) {
    callback = cb;
}

void I2S_InitBuffer() {
    int32_t n = 0x10000000;
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i+=2) {
        buffer1[i] = +n; //0x800000 + n;
        buffer1[i+1] = -n; //0x800000 - n;
        n += 0x01000000;// 0x7fffffffL/AUDIO_BUFFER_SIZE/2;
    }
}

void I2S_Setup(void) {
    rcc_periph_clock_enable(RCC_SPI2);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);

    /**I2S2 GPIO Configuration    
    PB10     ------> I2S2_CK
    PB12     ------> I2S2_WS
    PB15     ------> I2S2_SD
    PC6     ------> I2S2_MCK 
    */

    // DAC reset pin: B14
    gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO14);
    gpio_set_af(GPIOB, GPIO_AF0, GPIO14);
    gpio_clear(GPIOB, GPIO14);


    // GPIO B10..15 AF5: I2S2_CK | I2S2_WS | I2S2_SD
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO12 | GPIO15);
    gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO12 | GPIO15);

    // GPIO C6 AF5: I2S2_MCK
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6);
    gpio_set_af(GPIOC, GPIO_AF5, GPIO6);

    // setup as 24-bit Philips I2S

    // SPI_CR2  TEXIE       Transmit Buffer Empty interrupt enable
    //          TXDMAEN     Transmit buffer DMA enable
    SPI_CR2(SPI2) = SPI_CR2_TXEIE
        | SPI_CR2_TXDMAEN;
    SPI_I2SCFGR(SPI2) = 
      //  CKPOL = 0
          SPI_I2SCFGR_I2SMOD                                                // I2S mode is selected
        | (SPI_I2SCFGR_I2SCFG_MASTER_TRANSMIT << SPI_I2SCFGR_I2SCFG_LSB)    // I2S Master Transmit
        | (SPI_I2SCFGR_I2SSTD_I2S_PHILIPS << SPI_I2SCFGR_I2SSTD_LSB)       // Philips I2S
        | (SPI_I2SCFGR_DATLEN_24BIT << SPI_I2SCFGR_DATLEN_LSB)              // 24-bits
        | SPI_I2SCFGR_CHLEN;                                                // Long chlen (32-bit)


    // Target Fs
    // I2S bitrate = number of bits per channel × number of channels × sampling audio frequency
    //               32 * 2 * 48000 = 3 072 000
    // Fhse = 25, PLLM = 25, Fvco = 1 MHz
    // Fi2s = 1 * 258 / 3 = 86 MHz
    // Fs = 86e6/(64*(4*(1+2*3)) = 47991.071429 :-> DIV = 3, ODD = 1
    // MCK = Fs * 256 = 12285714.286 Hz
    // CK (bit clock) = 32 * 2 * 47991 = 3071424 Hz
    // LRCLK = 47991 Hz
    int N = 258;
    int R = 3;
    int DIV = 3;

    RCC_PLLI2SCFGR =
          (N << RCC_PLLI2SCFGR_PLLI2SN_SHIFT)
        | (R << RCC_PLLI2SCFGR_PLLI2SR_SHIFT);

    RCC_CFGR &= ~RCC_CFGR_I2SSRC;               // PLL2IS clock is I2S clock source

    RCC_CR |= RCC_CR_PLLI2SON;                  // Enable PLLI2S

    /* Wait till PLLI2S is ready */
    while((RCC_CR & RCC_CR_PLLI2SRDY) == 0);    // for for PLL to become stable

    gpio_set(GPIOB, GPIO14);                    // pull DAC reset pin high

    SPI_I2SPR(SPI2) = 
          SPI_I2SPR_MCKOE                       // MCLK output enable
        | SPI_I2SPR_ODD                         //  ODD factor = 0
        | DIV;

    dma_setup();
}

void I2S_Start() {
    SPI_I2SCFGR(SPI2) |= SPI_I2SCFGR_I2SE;      // Enable I2S2   
    i2s_enabled = 1;
}

void I2S_Pause() {
    i2s_enabled = 0;
    SPI_I2SCFGR(SPI2) &= ~SPI_I2SCFGR_I2SE;      // Enable I2S2       
    dma_setup();
}

void I2S_Shutdown(void) {
    gpio_clear(GPIOB, GPIO14);
 
    dma_disable_stream(DMA1, DMA_STREAM4);

    SPI_I2SCFGR(SPI2) &= ~SPI_I2SCFGR_I2SE;     // Disable I2S2
    rcc_periph_clock_disable(RCC_SPI2);

    // Reset GPIO pins to inputs
    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO13 | GPIO12 | GPIO15);
    gpio_set_af(GPIOB, GPIO_AF0, GPIO13 | GPIO12 | GPIO15);
    gpio_mode_setup(GPIOC, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO6);
    gpio_set_af(GPIOC, GPIO_AF0, GPIO6);
}
