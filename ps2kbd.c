#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "ps2kbd.h"
#include "uart.h"
#include <util/delay.h>


// Volatile, declared here because they're used in and out of the ISR
volatile uint8_t rcv_byte = 0;
volatile uint8_t rcv_bitcount = 0;
volatile uint8_t send_bitcount = 0;
volatile uint8_t scancode = 0;
volatile uint8_t strobe = 0 ;
volatile uint8_t ssp = 0; // 0 = Start/ 1 = stop/ 2 = parity
volatile uint8_t sr = 0; // 0 = 0 - Receive, 0 = 1 = Send
volatile uint8_t send_parity = 0; 
volatile uint8_t send_byte = 0;
volatile uint8_t parity_errors = 0; // Currently unused but will provide error info to host computer
volatile uint8_t framing_errors = 0;


int calc_parity(unsigned parity_x) 
{
	// Calculate Odd-Parity of byte needed to send PS/2 Packet
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
		EIMSK &= ~(1 << INT0); // Disable interrupt for CLK
		DDRD |= (1 << DDD2 | 1 << DDD3); // CLK/Data are now Outputs
		PORTD &= ~(1 << PD2); // Bring Clock low
		_delay_us(200);
		PORTD &= ~(1 << PD3); // Bring data Low
		PORTD |= (1 << PD2); // Release clock and set it as an input again, clear interrupt flags and re-enable the intterupts
		DDRD &= ~(1 << DDD2);
		EIFR |= (1 << INTF0);
		EIMSK |= (1 << INT0);
		sr = 1;
		while (sr == 1) {} // All the work for sending the data is handled inside the interrupt
		DDRD &= ~(1 << DDD2 | 1 << DDD3); // Clock and Data set back to input
		while (strobe == 0) {} // Wait for ACK packet before proceeding
		strobe = 0;
		send_tries--;
	}	while ((send_tries) && (scancode != 0xFA)); // If the response is not an ack, resend up to 3 times.

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
		if (send_bitcount >=0 && send_bitcount <=7) // Data Byte
		{
			if ((send_byte >> send_bitcount) & 1) {
				PORTD |= (1 << PD3);
			}
			else
			{
				PORTD &= ~(1 << PD3);
			}
		}
		else if (send_bitcount == 8) // Parity Bit
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
		else if (send_bitcount == 9) // Stop Bit
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
				rcv_byte |= (result << (rcv_bitcount - 1)); //Scancode Byte
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
	volatile uint8_t kb_register = 0; // 0 = keyup | 1 = shift | 2 = ctrl | 3 = alt | 4 = capslock | 5 = numlock | 6 = scroll lock
	volatile char ret_char = 0;
	DDRD &= ~(1 << DDD2 | 1 << DDD3); // PIND2 = PS/2 Clock, PIND3 = PS/2 Data both set as input
	EICRA |= (1 << ISC01);	// Interrupt on Falling Edge
	EIMSK |= (1 << INT0); // Enable Interrupt on PIND2 aka INT0
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
			if (kb_register & (1 << KB_KUP)) //This is a keyup event
			{
				switch(scancode)
				{
					case 0x12:
					case 0x59:
						kb_register &= ~(1 << KB_SHIFT);
						break;
					case 0x14:
						kb_register &= ~(1 << KB_CTRL);
						break;
					case 0x11:
						kb_register &= ~(1 << KB_ALT);
						break;
					default:
						break;
				}
				kb_register &= ~(1 << KB_KUP);

			}
			else 
			{
				switch(scancode)
				{
					case 0xF0: //Key up
						kb_register |= (1 << KB_KUP);
						break;
					case 0xE0: //Extended key sequence
						kb_register |= (1 << KB_EXT);
						break;
					case 0x12:
					case 0x59: // Shift
						kb_register |= (1 << KB_SHIFT);
						break;
					case 0x66: //backspace
						ret_char = 0x7F;
						break;
					case 0x5A: //enter
						ret_char = 0x0D;
						break;
					case 0x0D: //tab
						ret_char = 0x09;
						break;
					case 0x14: //ctrl
						kb_register |= (1 << KB_CTRL);
						break;
					case 0x11: //alt
						kb_register |= (1 << KB_ALT);
						break;
					case 0x76: //esc
						ret_char = 0x1B;
						break;
					case 0x58: //capslock
						kb_register ^= (1 << KB_CAPSLK);
						sendps2(0xed,0);
						sendps2((kb_register >> 4),0); // Set KBD Lights
						break;
					case 0x77: //numlock
						kb_register ^= (1 << KB_NUMLK);
						sendps2(0xed,0);
						sendps2((kb_register >> 4),0); // Set KBD Lights
						break;
					case 0x7E: //scrllock
						kb_register ^= (1 << KB_SCRLK);
						sendps2(0xed,0);
						sendps2((kb_register >> 4),0); // Set KBD Lights
						break;
					default: // Fall through for Alphanumeric Characters
						if (kb_register & (1 << KB_CTRL)) // ASCII Control Code 
						{
							ret_char = ps2_to_ascii_shifted[scancode];  
							if ((ret_char >=0x41) && (ret_char <= 0x5A)) //Make sure we don't read outside the valid range of codes
							{
								ret_char = ret_char - 0x40;
							}
							else 
							{
								ret_char = 0;
							}
						}
						else if ((kb_register & (1<< KB_SHIFT)) | (kb_register & (1 <<KB_CAPSLK))) {
							ret_char = ps2_to_ascii_shifted[scancode]; 
						}
						else
						{
							ret_char = ps2_to_ascii[scancode];
						}
						break;
				}				
				if (ret_char)
				{
					printf("%c", ret_char);
					ret_char = 0;
				}
			}
			strobe = 0;
		}
	}
}
