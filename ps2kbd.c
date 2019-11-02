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
#include <avr/wdt.h>

// Volatile, declared here because they're used in and out of the ISR
volatile uint8_t rcv_byte = 0;
volatile uint8_t rcv_bitcount = 0;
volatile uint8_t send_bitcount = 0;
volatile uint8_t scancode = 0;
volatile uint8_t ssp = 0; // 0 = Start/ 1 = stop/ 2 = parity
volatile uint8_t send_parity = 0;
volatile uint8_t send_byte = 0;
volatile uint8_t parity_errors = 0; // Currently unused but will provide error info to host computer
volatile uint8_t framing_errors = 0;
volatile enum bufstate buffer = EMPTY;
volatile enum ps2state mode = KEY;
volatile enum rxtxstate sr = RX;

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

void sendps2(uint8_t data)
{
/*  Send a PS/2 Packet.
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
    PORTB &= ~(1 << PB5); // Set data Low
    PORTB &= ~(1 << PB6); // Set Clock low
    DDRB |= (1 << DDB6); // CLK low
    _delay_us(150);
    DDRB |= (1 << DDB5); // DATA low
    DDRB &= ~(1 << DDB6); // Release clock and set it as an input again, clear interrupt flags and re-enable the interrupts
    GIFR |= (1 << INTF0);
    GIMSK |= (1 << INT0);
    sr = TX;
    while (sr == TX) {} // All the work for sending the data is handled inside the interrupt
    DDRB &= ~(1 << DDB6 | 1 << DDB5); // Clock and Data set back to input
    buffer = EMPTY;
    while (buffer == EMPTY) {} // Wait for ACK packet before proceeding
    buffer = EMPTY;
    send_tries--;
  } while ((send_tries) && (scancode != 0xFA)); // If the response is not an ack, resend up to 3 times.
  _delay_us(150);
}

int getresponse(void) {
    mode = COMMAND;
    while (buffer == EMPTY) {}
    mode = KEY;
    buffer = EMPTY;
    return scancode;
}

void resetKbd(void) {
  sendps2(0xff); // reset kbd
  uint8_t resp = getresponse();
  if (resp != 0xAA) {
    while (1) {} // Trigger WDT Reset
  }
  sendps2(0xf0); // Set Codeset
  sendps2(0x02); // Codeset 2
}

void resetHost(void) {
    DDRB |= (1 << DDB4);
    PORTB &= ~(1 << PB4);
    for (;;) {} // Reset KBC using WDT
}

void parity_error(void)
{
  parity_errors++;
  sendps2(0xFE); // Inform the KBD of the Parity error and request a resend.
}

ISR (INT0_vect)
{
  if (sr == TX) { //Send bytes to device.
    if (send_bitcount >=0 && send_bitcount <=7) // Data Byte
    {
      if ((send_byte >> send_bitcount) & 1) {
        DDRB &= ~(1 << DDB5); // DATA High
      }
      else
      {
        DDRB |= (1 << DDB5); // DATA Low
      }
    }
    else if (send_bitcount == 8) // Parity Bit
    {   
      if (send_parity)
      {
        DDRB |= (1 << DDB5); // DATA Low
      }
      else
      {
        DDRB &= ~(1 << DDB5); // DATA High
      }
    }
    else if (send_bitcount == 9) // Stop Bit
    {
      DDRB &= ~(1 << DDB5); // DATA High
    }
    if (send_bitcount < 10)
    {
      send_bitcount++;
    }
    else
    {
      send_bitcount = 0;
      sr = RX;
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
    else if (rcv_bitcount == 10)
    {
      ssp |= (result << 1); // Stop Bit
      if ((ssp & 0x2) != 0x02) // Check start and stop bits.
      {
        framing_error(ssp);
      }
      else if (calc_parity(rcv_byte) == (ssp >> 2))
      {
        parity_error();
        buffer = EMPTY;
      }
      else
      {
        scancode = rcv_byte;
        buffer = FULL;
      }
      rcv_bitcount = 0;
      rcv_byte = 0;
      result = 0;
    }

  }

}

int main (void) {
  volatile uint8_t kb_register = 0;
  volatile uint8_t kb_leds = 0;
  volatile char ret_char = 0;

  MCUCR |= (1 << ISC01 | 1 << PUD); // Interrupt on Falling Edge, force disable pullups
  DDRB &= ~(1 << DDB6 | 1 << DDB5); // PINB6 = PS/2 Clock, PINB5 = PS/2 Data both set as input
  DDRB |= (1 << DDB3);
  DDRA |= (0xFF);
  GIMSK |= (1 << INT0); // Enable Interrupt on PINB2 aka INT0

  wdt_enable(WDTO_500MS);
  sei();
  resetKbd();
  
  while (1) {
    wdt_reset();
    if ((kb_register & (1 << KB_L_CTRL)) && (kb_register & (1 << KB_L_ALT)) && (kb_register & (1 << KB_R_ALT))) {
      for (;;) {
        resetHost();
      }
    }
    if ((buffer == FULL) && ((mode == KEY) || (mode == EXTKEY)))
    {
      GIMSK &= ~(1 << INT0); // Disable interrupt for CLK
      PORTB &= ~(1 << PB6); // Bring Clock low, inhibit keyboard until done processing event
      DDRB |= (1 << DDB6); // CLK now an outout
      if (mode == EXTKEY) {
        if (kb_register & (1 << KB_KUP)) //This is a keyup event
        {
          switch(scancode)
          {
            case 0x14:
              kb_register &= ~(1 << KB_R_CTRL);
              break;
            case 0x11:
              kb_register &= ~(1 << KB_R_ALT);
              break;
            default:
              break;
          }
          kb_register &= ~(1 << KB_KUP);
          mode = KEY;
        }
        else {
          switch(scancode)
          {
            case 0xF0: //Key up
              kb_register |= (1 << KB_KUP);
              mode = EXTKEY;
              break;
            case 0x14: //ctrl
              kb_register |= (1 << KB_R_CTRL);
              mode = KEY;
              break;
            case 0x11: //alt
              kb_register |= (1 << KB_R_ALT);
              mode = KEY;
              break;
            default:
              mode = KEY;
              break;
          }
        }
      }
      else if (mode == KEY)
      {
        if (kb_register & (1 << KB_KUP)) //This is a keyup event
        {
          switch(scancode)
          {
            case 0x12: // Left Shift
              kb_register &= ~(1 << KB_L_SHIFT);
              break;
            case 0x59: // Right Shift
              kb_register &= ~(1 << KB_R_SHIFT);
              break;
            case 0x14:
              kb_register &= ~(1 << KB_L_CTRL);
              break;
            case 0x11:
              kb_register &= ~(1 << KB_L_ALT);
              break;
            default:
              break;
          }
          kb_register &= ~(1 << KB_KUP);
          mode = KEY;
        }
        else {
          switch(scancode)
          {
            case 0xF0: //Key up
              kb_register |= (1 << KB_KUP);
              break;
            case 0xE0: //Extended key sequence
              mode = EXTKEY;
              break;
            case 0x12: // Left Shift
              kb_register |= (1 << KB_L_SHIFT);
              break;
            case 0x59: // Right Shift
              kb_register |= (1 << KB_R_SHIFT);
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
              kb_register |= (1 << KB_L_CTRL);
              break;
            case 0x11: //alt
              kb_register |= (1 << KB_L_ALT);
              break;
            case 0x76: //esc
              ret_char = 0x1B;
              break;
            case 0x58: //capslock
              kb_leds ^= (1 << KB_CAPSLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            case 0x77: //numlock
              kb_leds ^= (1 << KB_NUMLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            case 0x7E: //scrllock
              kb_leds ^= (1 << KB_SCRLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            default: // Fall through for Alphanumeric Characters
              if ((kb_register & (1 << KB_L_CTRL)) || (kb_register & (1 << KB_R_CTRL))) // ASCII Control Code
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
              else if ((kb_register & (1<< KB_L_SHIFT)) || (kb_register & (1<<KB_R_SHIFT))) {
                ret_char = ps2_to_ascii_shifted[scancode];
              }
              else if (kb_leds & (1 <<KB_CAPSLK)) {
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
      }
      buffer = EMPTY;
      DDRB &= ~(1 << DDB6);
      GIFR |= (1 << INTF0);
      GIMSK |= (1 << INT0);
    }
  }
}
