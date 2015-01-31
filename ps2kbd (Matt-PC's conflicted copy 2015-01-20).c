#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "ps2kbd.h"
#include "uart.h"

static uint8_t rcv_byte = 0;
static uint8_t rcv_bitcount = 0;
volatile uint8_t result;

ISR (INT0_vect)
{
	if (PIND & (1 << PD3)) {
		result = 1;
	}
	else 
	{
		result = 0;
	}
	if (rcv_bitcount == 0)
	{
		rcv_byte = 0;
	}
	else if ((rcv_bitcount > 0) & (rcv_bitcount < 9)) {
				rcv_byte = (result | (1 << rcv_bitcount-1 ));
	}
	else if (rcv_bitcount == 10) 
	{
	// Frame received
	rcv_bitcount = 0;
	printcode();
	return;
	
	}
	rcv_bitcount++;
	return;
}

int printcode() {
printf("Scancode recvd: 0x%c\%c\r\n", hextoascii[((rcv_byte >> 4) & 0x0F)] ,hextoascii[(rcv_byte & 0x0F)]);
printf("Ascii %c \r\n", ps2_toascii[rcv_byte]);

}

int main (void) {
DDRD &= ~(1 << DDD2 | 1 << DDD3);
PORTD |= (1 << PORTD2 | 1 << PORTD3);
EICRA |= (1 << ISC01);
EIMSK |= (1 << INT0);

uart_init();
stdout = &uart_output;
stdin  = &uart_input;
printf("Startup Completed.\r\n");
sei();

while (1) {}
}