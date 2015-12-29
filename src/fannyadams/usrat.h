#pragma once

#define DEBUG_USARTN 1

void Debug_UART_Setup(void);
int xputchar(int c);
int xavail(void);
int xgetchar(void);
void xflush(void);
