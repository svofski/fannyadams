#include <inttypes.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>

#include "usrat.h"


#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)

#define BUFFER_SIZE   32

volatile uint8_t TxBuffer[BUFFER_SIZE];
volatile uint8_t RxBuffer[BUFFER_SIZE];

volatile uint8_t tx_head = 0, tx_tail = 0;
volatile uint8_t rx_head = 0, rx_tail = 0;

#define DEBUG_USARTN 1

#if (DEBUG_USARTN == 1)
#define USART_DEBUG USART1
#define USART_DEBUG_IRQn USART1_IRQn
#elif (DEBUG_USARTN == 3)
#define USART_DEBUG USART3
#define USART_DEBUG_IRQn USART3_IRQn
#endif

static int usart_Configured = 0;

volatile int USARTD = 0;

void Debug_UART_Setup(void) 
{
    tx_head = rx_head = 0;
    tx_tail = rx_tail = 0;

    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_USART1);

    /* Enable the USART1 interrupt. */
    nvic_enable_irq(NVIC_USART1_IRQ);

    /* Setup GPIO pins for USART1 transmit. */
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6);
    /* Setup GPIO pins for USART1 receive. */
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7);

    gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_25MHZ, GPIO6); // TX

    /* Setup USART1 TX and RX pin as alternate function. */
    gpio_set_af(GPIOB, GPIO_AF7, GPIO6);
    gpio_set_af(GPIOB, GPIO_AF7, GPIO7);

    /* Setup USART1 parameters. */
    usart_set_baudrate(USART1, 115200);
    usart_set_databits(USART1, 8);
    usart_set_stopbits(USART1, USART_STOPBITS_1);
    usart_set_mode(USART1, USART_MODE_TX_RX);
    usart_set_parity(USART1, USART_PARITY_NONE);
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

    /* Enable USART1 Receive interrupt. */
    usart_enable_rx_interrupt(USART1);

    /* Finally enable the USART. */
    usart_enable(USART1);

    usart_Configured = 1;
}

static inline uint8_t tx_count(void)
{
    USARTD = 10;

    return (tx_head >= tx_tail) ? tx_head - tx_tail : tx_head + BUFFER_SIZE - tx_tail;
}

static inline uint8_t rx_count(void)
{
    USARTD = 33;
    return (rx_head >= rx_tail) ? rx_head - rx_tail : rx_head + BUFFER_SIZE - rx_tail;
}

void xflush(void) {
    while(tx_count() != 0);
}

int xavail(void) {
    return rx_count();
}

int xgetchar(void)
{
    if (xavail()) {
        if (++rx_tail == BUFFER_SIZE) {
            rx_tail = 0;
        }
        return RxBuffer[rx_tail];
    } else {
        return -1;
    }
}

static void emit(void)
{
    do {
        if (tx_head != tx_tail) {
            // if there's a send in progress, exit to let it finish 
            if ((USART_SR(USART_DEBUG) & USART_SR_TXE) == 0) {
                break;
            }

            if (++tx_tail == BUFFER_SIZE) {
                tx_tail = 0;
            }
            usart_send(USART_DEBUG, TxBuffer[tx_tail]);
            usart_enable_tx_interrupt(USART_DEBUG);
        } else {            
            usart_disable_tx_interrupt(USART_DEBUG);
        }
    } while(0);
}

int xputchar(int ch)
{   
    if (!usart_Configured) return -1;

    if (ch == '\n') {
        xputchar('\r');
    }

    // wait until buffer has room
    uint8_t count;
    while ((count = tx_count()) == BUFFER_SIZE - 1);

    // disable USART IRQ
    usart_disable_tx_interrupt(USART_DEBUG);

    // put character in buffer
    if (++tx_head == BUFFER_SIZE) {
        tx_head = 0;
    }
    TxBuffer[tx_head] = (uint8_t) ch;

    // enable USART IRQ
    usart_enable_tx_interrupt(USART_DEBUG);

    // if the buffer was empty, initiate transfer
    if (count == 0) {
        // force trigger USART IRQ, interrupt is already enabled
        USART_SR(USART1) |= USART_SR_TXE;
    }
    
    return ch;
}

#if (DEBUG_USARTN == 1)
void usart1_isr(void)
#elif (DEBUG_USARTN == 3)
void usart3_isr(void)
#endif
{
    if ((USART_SR(USART_DEBUG) & USART_SR_RXNE) != 0) {
        // this is done automatically by reading USART->DR
        //USART_ClearITPendingBit(USART_DEBUG, USART_IT_RXNE); == USART_DEBUG->SR &= ~USART_IT_RXNE;
        if (++rx_head == BUFFER_SIZE) {
            rx_head = 0;
        }
        RxBuffer[rx_head] = usart_recv(USART_DEBUG);
     }

    // if transmit register empty
    if ((USART_SR(USART_DEBUG) & USART_SR_TXE) != 0) {
        emit();
    }
}
