#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

volatile uint16_t ADC3_BUFFER[3];

#define ADC3_C
#include "adc3.h"
#undef ADC3_C
#include "mxconstants.h"

#define POT1_PORT   GPIOC
#define POT1_PIN    GPIO0
#define POT1_CH     10

#define POT2_PORT   GPIOC
#define POT2_PIN    GPIO1
#define POT2_CH     11

#define FB_PORT     GPIOA
#define FB_PIN      GPIO0
#define FB_CH       0

#define TIMADC      TIM3
#define RCC_TIMADC  RCC_TIM3


// Configure TIM3 to trigger ADC3 conversions
static void TIM_setup(void) {
    rcc_periph_clock_enable(RCC_TIMADC);
    timer_set_mode(TIMADC, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_continuous_mode(TIMADC);
    timer_set_period(TIMADC, 40000);
    timer_disable_oc_output(TIMADC, TIM_OC1 | TIM_OC2 | TIM_OC3 | TIM_OC4);
    timer_disable_preload(TIMADC);

    timer_set_master_mode(TIMADC, TIM_CR2_MMS_UPDATE); // TRGO output on update
}

static void TIM_enable(void) {
    timer_enable_counter(TIMADC);
}

static void DMA_setup(void) {
    rcc_periph_clock_enable(RCC_DMA2);
    //nvic_enable_irq(DMA2_STREAM1_IRQ);

    dma_stream_reset(DMA2, DMA_STREAM1);
    dma_set_priority(DMA2, DMA_STREAM1, DMA_SxCR_PL_LOW);

    dma_set_memory_size(DMA2, DMA_STREAM1, DMA_SxCR_MSIZE_16BIT);       // halfword increments
    dma_set_peripheral_size(DMA2, DMA_STREAM1, DMA_SxCR_PSIZE_16BIT);   // halfword peripheral size
    dma_enable_memory_increment_mode(DMA2, DMA_STREAM1);
    dma_enable_circular_mode(DMA2, DMA_STREAM1);
    dma_set_transfer_mode(DMA2, DMA_STREAM1, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
    dma_set_peripheral_address(DMA2, DMA_STREAM1, (uint32_t) &ADC_DR(ADC3));

    dma_set_memory_address(DMA2, DMA_STREAM1, (uint32_t) &ADC3_BUFFER);
    dma_set_number_of_data(DMA2, DMA_STREAM1, 3);

    //dma_enable_transfer_complete_interrupt(DMA2, DMA_STREAM1);
    dma_channel_select(DMA2, DMA_STREAM1, DMA_SxCR_CHSEL_2);
    dma_enable_stream(DMA2, DMA_STREAM1);
}

void ADC3_Setup(void) {
    // Setup the governor timer
    TIM_setup();

    // Configure GPIO pins for POT1, POT2 and voltage feedback
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_mode_setup(POT1_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO0);
    gpio_mode_setup(POT2_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO1);
    gpio_mode_setup(FB_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, FB_PIN);

    // Clocks
    rcc_periph_clock_enable(RCC_ADC3);
    adc_set_clk_prescale(ADC_CCR_ADCPRE_BY2);

    adc_off(ADC3);

    // Set trigger to TIM3_TRGO
    adc_enable_external_trigger_regular(ADC3, ADC_CR2_EXTSEL_TIM3_TRGO, ADC_CR2_EXTEN_RISING_EDGE);
    
    adc_set_right_aligned(ADC3);
    adc_set_resolution(ADC3, ADC_CR1_RES_12BIT);

    // Set scanning sequence
    uint8_t channels[] = {FB_CH, POT2_CH, POT1_CH};
    adc_set_single_conversion_mode(ADC3);
    adc_disable_discontinuous_mode_regular(ADC3);
    adc_enable_scan_mode(ADC3);
    adc_set_regular_sequence(ADC3, 3, channels);
    adc_set_sample_time_on_all_channels(ADC3, ADC_SMPR_SMP_28CYC);

    // Enable DMA
    adc_enable_dma(ADC3);
    adc_set_dma_continue(ADC3); // enables continuous circular mode
    DMA_setup();
}

void ADC3_Start(void) {
    adc_power_on(ADC3); 
    TIM_enable();
}