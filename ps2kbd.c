/*
PS2KBC, a PS2 Controler implemented on the Atmel ATTINY861.
Copyright (C) 2015 Matt Harlum

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <avr/io.h>
#include <avr/interrupt.h>
#include "ps2kbd.h"
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
volatile uint8_t fsm_state = 0;
volatile uint8_t host_cmd = 0;


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
  framing_errors++;
  GIMSK &= ~(1 << INT0);
  _delay_ms(8);
  GIFR |= (1 << INTF0); // Clear Interrupt flag
  GIMSK |= (1 << INT0);
}

void inhibit_kbd()
{
      GIMSK &= ~(1 << INT0); // Disable interrupt for CLK
      DDRB |= (1 << DDB6); // CLK now an outout
      PORTB &= ~(1 << PB6); // Bring Clock low, inhibit keyboard until done processing event
}

void disinhibit_kbd()
{
      PORTB |= (1 << PB6); // Release clock and set it as an input again, clear interrupt flags and re-enable the interupts
      DDRB &= ~(1 << DDB6);
      GIFR |= (1 << INTF0);
      GIMSK |= (1 << INT0);
}

void sendps2(uint8_t data, uint8_t responseneeded)
{
/*  Complicated shit to send a PS/2 Packet.
  Begin the request by making both inputs outputs, drag clock low for at least 100us then take data low and release clock.
  the device will soon after start clocking in the data so make clk an input again and pay attention to the interrupt.
  The device will clock in 1 start bit, 8 data bits, 1 parity bit then 1 stop bit. It will then ack by taking data low on the 12th clk (though this is currently ignored) and then it will respond with an 0xFA ACK */
  uint8_t send_tries = 3;
  scancode = 0;
  do
  {
    send_byte = data;
    send_parity = calc_parity(send_byte);
    GIMSK &= ~(1 << INT0); // Disable interrupt for CLK
    DDRB |= (1 << DDB6 | 1 << DDB5); // CLK/Data are now Outputs
    PORTB &= ~(1 << PB6); // Bring Clock low
    _delay_us(150);
    PORTB &= ~(1 << PB5); // Bring data Low
    PORTB |= (1 << PB6); // Release clock and set it as an input again, clear interrupt flags and re-enable the intterupts
    DDRB &= ~(1 << DDB6);
    GIFR |= (1 << INTF0);
    GIMSK |= (1 << INT0);
    sr = 1;
    while (sr == 1) {} // All the work for sending the data is handled inside the interrupt
    DDRB &= ~(1 << DDB6 | 1 << DDB5); // Clock and Data set back to input
    while (strobe == 0) {} // Wait for ACK packet before proceeding
    strobe = 0;
    send_tries--;
  }   while ((send_tries) && (scancode != 0xFA)); // If the response is not an ack, resend up to 3 times.

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
}

ISR (PCINT_vect)
{
  if PINB & (1 << PB4) // Pin went high?
  {
    if fsm_state == 0 {
      inhibit_kbd();
       // Handshake by sending 0xAA to the host to notify we are about to wait for a command
      PORTA = 0x4F;
      PORTB |= 1 << PB3;
      _delay_us(10);
      PORTB &= ~(1 << PB3);
      DDRA = 0x00; // PORTA is now an input   
      fsm_state++;
    }
    else if fsm_state == 1 {
      host_cmd = PORTA;
      PORTB |= 1 << PB3;
      _delay_us(10);
      PORTB &= ~(1 << PB3);
      fsm_state = 0;
      sendps2(0xED,0);
      sendps2(host_cmd,0);
      DDRA = 0xFF;
      disinhibit_kbd()
    }
/*      if host_cmd == 1 {
        fsm_state++
      } else {
        fsm_state = 3
      }
    }
    else if fsm_state =< 3 {
      host_cmd = PORTA;
      PORTB |= 1 << PB3;
      _delay_us(10);
      PORTB &= ~(1 << PB3);
      sendps2(host_cmd,0);
      fsm_state++
    }
    else {
      fsm_state = 0;
      DDRA = 0xFF;
      disinhibit_kbd;
    }*/
  }
}

ISR (INT0_vect)
{
  if (sr == 1) { //Send bytes to device.
    if (send_bitcount >=0 && send_bitcount <=7) // Data Byte
    {
      if ((send_byte >> send_bitcount) & 1) {
        PORTB |= (1 << PB5);
      }
      else
      {
        PORTB &= ~(1 << PB5);
      }
    }
    else if (send_bitcount == 8) // Parity Bit
    {   
      if (send_parity)
      {
        PORTB &= ~(1 << PB5);
      }
      else
      {
        PORTB |= (1 << PB5);
      }
    }
    else if (send_bitcount == 9) // Stop Bit
    {
      PORTB |= (1 << PB5);
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

    if (PINB & (1 << PB5))
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
  DDRB &= ~(1 << DDB6 | 1 << DDB5 | 1 << DDB4); // PINB6 = PS/2 Clock, PINB5 = PS/2 Data both set as input, PINB4 = Host Handshake
  DDRB |= (1 << DDB3);
  DDRA |= (0xFF);
  MCUCR |= (1 << ISC01 | 1 << PUD); // Interrupt on Falling Edge, force disable pullups
  PCMSK1 |= ( << PCINT12); // Add PB4 to Pin-change interrupt 1 mask
  GIMSK |= (1 << INT0 | 1 << PCINE1); // Enable Interrupt on PINB2 aka INT0 or Pin change interrupt 1

  sei();
  sendps2(0xff,1); // reset kbd
  sendps2(0xf0,0); // Set Codeset
  sendps2(0x02,0); // Codeset 2
  

  while (1) {
    if (strobe)
    {
      inhibit_kbd();
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
                ret_char ^= 0x40;
              }
              else 
              {
                ret_char = 0;
              }
            }
            else if (kb_register & (1<< KB_SHIFT)) {
              ret_char = ps2_to_ascii_shifted[scancode];
            }
            else if (kb_register & (1 <<KB_CAPSLK)) {
              ret_char = ps2_to_ascii[scancode];
              if ((ret_char >= 0x61) && (ret_char <= 0x7A))
              {
                ret_char ^= 0x20;
              }
            }
            else
            {
              ret_char = ps2_to_ascii[scancode];
            }
            break;
        }
        if (ret_char)
        {
          PORTA = ret_char;
          PORTB |= 1 << PB3;
          _delay_us(10);
          PORTB &= ~(1 << PB3);
          ret_char = 0;
          PORTA = 0;
        }
      }
      strobe = 0;
      disinhibit_kbd();
    }
  }
}
