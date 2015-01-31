#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "ps2kbd.h"
#include "uart.h"
#include <util/delay.h>

volatile uint8_t rcv_byte = 0;
volatile uint8_t rcv_bitcount = 0;
volatile uint8_t send_bitcount = 0;
volatile uint8_t scancode = 0;
volatile uint8_t strobe = 0 ;
volatile uint8_t ssp = 0; // 0 = Start/ 1 = stop/ 2 = parity
volatile uint8_t sr = 0; // 0 = Receive, 1 = Send
volatile uint8_t send_parity;
volatile uint8_t send_byte;
volatile uint8_t parity_errors = 0;
volatile uint8_t framing_errors = 0;


int calc_parity(unsigned parity_x) 
{
	unsigned parity_y;
	parity_y = parity_x ^ (parity_x >> 1);
    parity_y = parity_y ^ (parity_y >> 2);
    parity_y = parity_y ^ (parity_y >> 4);
    return parity_y & 1;
}

void framing_error(uint8_t num)
{
	// Deal with PS/2 Protocol Framing errors. delay for the rest of the packet and clear interrupts generated during the delay.						
	printf("!F");
	framing_errors++;
	EIMSK &= ~(1 << INT0);
	_delay_ms(8);
	EIFR |= (1 << INTF0);	 // Clear Interrupt flag
	EIMSK |= (1 << INT0);
}

void sendps2(uint8_t data, uint8_t responseneeded)
{
/*	Complicated shit to send a PS/2 Packet.
	Begin the request by making both inputs outputs, drag clock low for at least 100us then take data low and release clock.
	the device will soon after start clocking in the data so make clk an input again and pay attention to the interrupt.
	The device will clock in 1 start bit, 8 data bits, 1 parity bit then 1 stop bit. It will then ack by taking data low on the 12th clk (though this is currently ignored) and then it will respond with an 0xFA ACK */
	uint8_t send_tries = 3;
	scancode = 0;
	do
	{
		send_byte = data;
		send_parity = calc_parity(send_byte);
		EIMSK &= ~(1 << INT0);
		DDRD |= (1 << DDD2 | 1 << DDD3);
		PORTD &= ~(1 << PD2);
		_delay_us(200);
		PORTD &= ~(1 << PD3);
		PORTD |= (1 << PD2);
		DDRD &= ~(1 << DDD2);
		EIFR |= (1 << INTF0);
		EIMSK |= (1 << INT0);
		sr = 1;
		while (sr == 1) {} // All the work for sending the data is handled inside the interrupt
		DDRD &= ~(1 << DDD2 | 1 << DDD3);
		while (strobe == 0) {} // Wait for ACK packet before proceeding
		strobe = 0;
		send_tries--;
	}	while ((send_tries) && (scancode != 0xFA));

	if (responseneeded) // Are we expecting a response besides ACK?
	{
		while (strobe == 0) {}
		strobe = 0;	
	}
}

void parity_error(void)
{	
	parity_errors++;
	sendps2(0xFE,0); // Inform the KBD of the Parity error and request a resend.
	printf("!P");
}

ISR (INT0_vect)
{
if (sr == 1) { //Send bytes to device.
	if (send_bitcount >=0 && send_bitcount <=7)
	{
		if ((send_byte >> send_bitcount) & 1) {
			PORTD |= (1 << PD3);
		}
		else
		{
			PORTD &= ~(1 << PD3);
		}
	}
	else if (send_bitcount == 8)
	{	
		if (send_parity)
		{
			PORTD &= ~(1 << PD3);
		}
		else
		{
			PORTD |= (1 << PD3);
		}
	}
	else if (send_bitcount == 9)
	{
		PORTD |= (1 << PD3);
	}
	if (send_bitcount < 10)
	{
		send_bitcount++;
	}
	else
	{
		send_bitcount = 0;
		sr = 0;
	}
}

else { // Receive from device
uint8_t result = 0;

	if (PIND & (1 << PD3)) 
	{
		result = 1;
	}
	else {
		result = 0;
	}
if (rcv_bitcount <=9) 
{
	if (rcv_bitcount >=1 && rcv_bitcount <= 8) 
	{
		rcv_byte |= (result << (rcv_bitcount - 1));
	}
	else if (rcv_bitcount == 0)
	{
		ssp = result; // Start Bit
	}
	else if (rcv_bitcount == 9)
	{
		ssp |= (result << 2); // Parity Bit
	}
	rcv_bitcount++;
}
	else if (rcv_bitcount >= 10) 
	{
		ssp |= (result << 1); // Stop Bit
		if ((ssp & 0x2) != 0x02) // Check start and stop bits.
		{
			framing_error(ssp);
		} 
		else if (calc_parity(rcv_byte) == (ssp >> 2))
		{
			parity_error();
			strobe = 0;
		}
		else 
		{
			scancode = rcv_byte;
			strobe = 1;
		}
		rcv_bitcount = 0;
		rcv_byte = 0;
		result = 0;
	} 

}

}


int main (void) {
volatile uint8_t kbd_curr_cmd = 0; // 0 = keyup | 1 = shift | 2 = ctrl | 3 = alt | 4 = capslock | 5 = numlock | 6 = scroll lock
DDRD &= ~(1 << DDD2 | 1 << DDD3);
EICRA |= (1 << ISC01);
EIMSK |= (1 << INT0);
uart_init();
stdout = &uart_output;
stdin  = &uart_input;
printf("Startup Completed. \r\n");

sei();
sendps2(0xff,1); // reset kbd
printf("Keyboard Self-test completed: 0x%x\r\n", scancode);
sendps2(0xf0,0); // Set Codeset 
sendps2(0x02,0); // Codeset 2

	while (1) {
		if (strobe)
		{
/*			if (scancode == 0x52 && kbd_curr_cmd == 0)
			{
				kbd_lights ^= 1 << 2;
				sendps2(0xed,0);
				sendps2(kbd_lights,0);
			}
			else if (scancode != 0xf0)
			{
				printf("Scancode: %x %x\r\n", scancode, kbd_curr_cmd);
			}
			if (scancode == 0xf0) {
				kbd_curr_cmd = 1; //key_up
			}
			else 
			{
				kbd_curr_cmd = 0; //key_down
			}
			strobe = 0;
*/


			if (kbd_curr_cmd & (1 << KB_KUP)) //This is a keyup event
			{
				switch(scancode)
				{
					case 0x12 | 0x59:
						kbd_curr_cmd &= ~(1 << KB_SHIFT);
						break;
					case 0x14:
						kbd_curr_cmd &= ~(1 << KB_CTRL);
						break;
					case 0x11:
						kbd_curr_cmd &= ~(1 << KB_ALT);
						break;
					default:
						break;
				}
				kbd_curr_cmd &= ~(1 << KB_KUP);

			}
			else 
			{
				switch(scancode)
				{
					case 0xF0: //Key up
						kbd_curr_cmd |= (1 << KB_KUP);
						break;
					case 0xE0: //Extended key
						kbd_curr_cmd |= (1 << KB_EXT);
						break;
					case 0x12 | 0x59: // Shift
						kbd_curr_cmd |= (1 << KB_SHIFT);
						break;
					case 0x66: //backspace
						break;
					case 0x5A: //enter
						break;
					case 0x0D: //tab
						break;
					case 0x14: //ctrl
						kbd_curr_cmd |= (1 << KB_CTRL);
						break;
					case 0x11: //alt
						kbd_curr_cmd |= (1 << KB_ALT);
						break;
					case 0x76: //esc
						break;
					case 0x58: //capslock
						kbd_curr_cmd ^= (1 << KB_CAPSLK);
						sendps2(0xed,0);
						sendps2((kbd_curr_cmd >> 4),0); // Set KBD Lights
						break;
					case 0x77: //numlock
						kbd_curr_cmd ^= (1 << KB_NUMLK);
						sendps2(0xed,0);
						sendps2((kbd_curr_cmd >> 4),0); // Set KBD Lights
						break;
					case 0x7E: //scrllock
						kbd_curr_cmd ^= (1 << KB_SCRLK);
						sendps2(0xed,0);
						sendps2((kbd_curr_cmd >> 4),0); // Set KBD Lights
						break;
					default:
						printf("char %c \r\n", ps2_to_ascii[scancode]);s
						break;
				}				
			}
			strobe = 0;
		}
	}
}
