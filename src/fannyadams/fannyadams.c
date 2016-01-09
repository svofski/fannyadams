#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>

#include "usrat.h"
#include "xprintf.h"
#include "adc3.h"
#include "power.h"
#include "systick.h"
#include "event.h"
#include "gpio.h"
#include "i2s.h"

#include "usbcmp.h"

volatile uint32_t lr = 0x80007fff;
void test() {
    uint16_t left = lr >> 16;
    uint16_t right = lr & 0177777;
    int32_t il = (int32_t) ((uint32_t)left << 16);
    int32_t ir = (int32_t) ((uint32_t)right << 16);
    xprintf("il=%d(%x) ir=%d(%x)\r\n", il, il, ir, ir);
}

int main(void)
{
    Clock_Setup();
    Debug_UART_Setup();

    xprintf("LES SHADOKS POMPAIENT\n\r");

    test();

    Event_Setup();
    ADC3_Setup();
    ADC3_Start();

    Power_Setup();
    GPIO_Setup();

    I2S_Setup();
    I2S_InitBuffer();

    USBCMP_Setup();

    // Turn on power if any of the plugs is inserted
    Clock_Debounce(DEBOUNCE_POWER, 150);
    Clock_Debounce(DEBOUNCE_DCDC, 300);

    while (1) {
        USBCMP_Poll();
        if (xavail()) {
            int c = xgetchar();
            xprintf("c=%02x ", c);
            switch (c) {
                case 'q':   Power_SetFactor(Power_GetFactor() - 1); break;
                case 'w':   Power_SetFactor(Power_GetFactor() + 1); break;
                case 'e':   Power_Stop(); break;
                case 'r':   Power_Start(); break;
                case 'a':   Power_SetPeriod(Power_GetPeriod() - 10); break;
                case 's':   Power_SetPeriod(Power_GetPeriod() + 10); break;
            }
            xprintf("ADC3: %d %d %d  Period: %d Factor: %d \n\r", ADC3_BUFFER[0], ADC3_BUFFER[1], ADC3_BUFFER[2],
                    Power_GetPeriod(), Power_GetFactor());
            xprintf("GPIOB=%04x %d %d\n\r", GPIOB_IDR, (GPIOB_IDR & GPIO8) != 0, (GPIOB_IDR & GPIO9) == 0);
        }
        for (;Event_QueueLength() > 0;) {
            Event *ev = Event_Get();
            switch (ev->EventId) {
                case EVENT_OUTPUT_PLUG:
                    //xprintf("Output plug state: @%d %s\n", ev->Timestamp, ev->Data ? "PLUGGED" : "UNPLUGGED");
                    //Event_Post_Delayed(100, (Event){EventId: POWER_SWITCH, Data: ev->Data});
                    Clock_Debounce(DEBOUNCE_POWER, 500);
                    break;
                case EVENT_DEBOUNCE: 
                    switch (ev->Data) {
                        case DEBOUNCE_POWER:
                            if (GPIO_IsOutputPlugged()) {
                                xprintf("Jack plugged, power on\r\n");
                                Power_Start();
                                Clock_Debounce(DEBOUNCE_I2S, 250);
                            } else {
                                xprintf("Jacks unplugged, I2S Shutdown, Power off\r\n");
                                I2S_Shutdown();
                                Power_Stop();
                            }
                            break;
                        case DEBOUNCE_DCDC:
                            {
                            int raw = ADC3_BUFFER[0];
                            int scaled = raw * 78;
                            //xprintf("DC-DC: voltage=%d.%d %d.%d\r\n", raw/1241, (raw % 1241)/124,
                            //    scaled/12410, (scaled % 12410)/1240);
                            Clock_Debounce(DEBOUNCE_DCDC, 1000);
                            }
                            break;
                        case DEBOUNCE_I2S:
                            xprintf("I2S Start\r\n");
                            I2S_Setup();
                            //I2S_Start();
                            break;
                    }
                    break;
                default:
                    xprintf("Unknown event %d:%d@%d\n\r", ev->EventId, ev->Data, ev->Timestamp);
                    break;
            }
        }
    }

    return 0;
}
