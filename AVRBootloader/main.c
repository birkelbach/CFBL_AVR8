/*  CANFix Bootloader - An Open Source CAN Fix Bootloader for ATMega328P 
 *  Copyright (c) 2011 Phil Birkelbach
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * This program is designed to be loaded into the bootloader section
 * of the AVR.  This is done by reassigning the .text section to
 * the starting address of the boot section with the linker.
 *
 * The .bootloader section is not used because it causes too many
 * problems with where the linker puts functions.  Reassigning the
 * .text section works much better.  It is therefore unwise to try
 * and combine this code with any application code.
 */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "mcp2515.h"
#include <util/delay_basic.h>
#include "bootloader.h"
#include "can.h"
#include "fix.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

static inline void init(void);

/* Global Variables */
uint8_t node_id;


#ifdef UART_DEBUG
/* This is a busy wait UART send function.
   It's mainly for debugging */
void
uart_write(char *write_buff, int size)
{
    int ptr=0;
    while(ptr < size) {
        while(!(UCSR0A & (1<<UDRE0)));
        UDR0 = write_buff[ptr];
        ptr++;
    }
}


/* Initialize the Serial port to 9600,8,N,1
   Used for debugging should be removed for
   production unless there is enough space
   to leave it for application use in the BL
   section of the program.
*/
void
init_serial(void)
{
    UCSR0B = (1<<RXEN0) | (1<<TXEN0);
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00); /* Set 1 Stop bit no parity */
#if F_CPU == 11059200UL
    /* 9600 BAUD @ 11.0592 MHz */
    UBRR0H = 0;
    UBRR0L = 71;
#elif F_CPU == 1000000UL
    UBRR0H = 0;
    UBRR0L = 12;
    UCSR0A = (1 << U2X1);
#elif F_CPU == 2000000UL
    UBRR0H = 0;
    UBRR0L = 24;
    UCSR0A = (1 << U2X1);
#elif F_CPU == 8000000UL
	UBRR0H = 0;
	UBRR0L = 103;
	UCSR0A = (1 << U2X1);
#else
  #error F_CPU needs to be properly defined
#endif
}
#endif /* UART_DEBUG */

/* Sets the port pins to the proper directions and initializes
   the registers for the SPI port */
void
init_spi()
{
    unsigned char x;

	/* We use the 8 bit timer 0 to make sure we have the right delay
	 * on our CS pin for the SPI to the CAN Controller */
	TCCR0B=0x01; /* Set Timer/Counter 0 to clk/1 */
    
    /* Set MOSI, SCK and SS output, all others input 
       PB2 Must either be an output or held high as an
       input for master SPI to work. */
    SPI_DDR |= (1<<SPI_MOSI)|(1<<SPI_SCK)|(1<<SPI_SS)|(1<<CAN_CS);
    SPI_PORT |= (1<<SPI_SS); /* Set the SS pin high to disable slave */
    SPI_PORT |= (1<<CAN_CS);
    /* Enable SPI, Master, set clock rate fck/16 */
    SPCR |= (1<<SPE)|(1<<MSTR);
    SPCR |= (1<<SPR0)|(0<<SPIE);
    /* This should clear any lingering interrupts? The book
     * says to do this. */
    x = SPSR;
    x = SPDR;
}

/* Calls the initialization routines */
static inline void
init(void)
{ 
    uint8_t cnf1=0x03, cnf2=0xb6, cnf3=0x04; /* Defaults to 125k */
	uint8_t can_speed = 0;

	init_spi();
 /* Set the CAN speed.  The values for 125k are the defaults so 0 is ignored
    and bad values also result in 125k */
    can_speed = eeprom_read_byte(EE_CAN_SPEED);
	if(can_speed==BITRATE_250) cnf1=0x01;      /* 250kbps */
	else if(can_speed==BITRATE_500) cnf1=0x00; /* 500kbps */
	else if(can_speed==BITRATE_1000) { cnf1=0x00; cnf2=0x92; cnf3=0x02; } /* 1Mbps */
    node_id = eeprom_read_byte(EE_NODE_ID);

 /* Initialize the MCP2515 */
	can_init(cnf1, cnf2, cnf3, 0x00);

#ifdef UART_DEBUG
	init_serial();
#endif
	TCCR1B=0x05; /* Set Timer/Counter 1 to clk/1024 */
 /* Move the Interrupt Vector table to the Bootloader section */
	MCUCR = (1<<IVCE);
	MCUCR = (1<<IVSEL);
	EICRA = 0x02; /* Set INT0 to falling edge */
}

/* This function stores the CRC value and the length in the
   last two words of the program flash. It does a 
   read/modify/write type operation on the whole last page. */
#if PGM_LENGTH_BITS == 16
void
store_crc(uint16_t crc, uint16_t length)
{    
    uint16_t n, i=0;
    /* Run through the page and store the information that is already there
       in the temporary buffer. */
    for(n=PGM_LAST_PAGE_START; n<(PGM_LAST_PAGE_START + PGM_PAGE_SIZE-4); n+=2) {
        i = pgm_read_word_near(n);
        boot_page_fill(n, i);
    }
    /* Add the length and crc to the buffer and write it out. */
    boot_page_fill(PGM_LENGTH, length);
    boot_page_fill(PGM_CRC, crc);
    boot_page_erase(PGM_LAST_PAGE_START);
    boot_spm_busy_wait(); 	
    boot_page_write(PGM_LAST_PAGE_START);
    boot_spm_busy_wait(); 
}
#elif PGM_LENGTH_BITS == 32
void
store_crc(uint16_t crc, uint32_t length)
{   
    uint16_t i=0;
    uint32_t n;

    /* Run through the page and store the information that is already there
       in the temporary buffer. */
    for(n=PGM_LAST_PAGE_START; n<(PGM_LAST_PAGE_START + PGM_PAGE_SIZE-6); n+=2) {
        i = pgm_read_word_far(n);
        boot_page_fill_safe(n, i);
    }
    /* Add the length and crc to the buffer and write it out. */
    boot_page_fill_safe(PGM_LENGTH_LSB, (uint16_t)(length & 0x0000FFFF));
    boot_page_fill_safe(PGM_LENGTH_MSB, (uint16_t)(length >> 16));
    boot_page_fill_safe(PGM_CRC, crc);
    boot_page_erase_safe(PGM_LAST_PAGE_START);
    boot_page_write_safe(PGM_LAST_PAGE_START);
}
#endif

/* This function polls the MCP2515 for a CAN frame that represents
   the given channel.  It checks Rx 1 first and then Rx 0.  Rx 1 first
   because it would be the older frame since we are using rollover */
static inline uint8_t
read_channel(uint8_t channel, struct CanFrame *frame)
{
    uint8_t result;
    uint16_t counter = 0;

    while(counter++ < 0x40FF) { /* roughly 1 second or so */
        result = can_poll_int();

        /* Read Frame from Buffer 1 */
        if(result & (1<<CAN_RX1IF)) {
            can_read(1, frame);
            /* Check that it's one of ours */
            if(frame->id == FIX_2WAY_CHANNEL + channel *2)
                return 0;
		}
		/* Read frame from buffer 0 */
        if(result & (1<<CAN_RX0IF)) {
            can_read(0, frame);
            /* Check that it's one of ours */
            if(frame->id == FIX_2WAY_CHANNEL + channel *2)
                return 0;
        }
    }
    return 2; /* Timeout */
}

/* This function handles the two way communication to firmware
   sending node.  It will never return.  If successful it checks
   the checksum of the new program as it's loaded and starts the
   new firmware.  If not successful it resets at BOOT_START and
   we do it all over again. */
void
load_firmware(uint8_t channel)
{
    struct CanFrame frame;
	int n;
    uint8_t result;
	uint16_t length = 0, offset = 0;
    uint32_t address = 0xFFFFFFFF;
	uint16_t crc;
	uint32_t temp;
	uint8_t to_count=0;
#ifdef UART_DEBUG
    char sout[5];

	uart_write("Load Firmware ", 14);
	itoa(channel, sout, 10);
    uart_write(sout, strlen(sout));
	uart_write("\n", 1);
#endif
    while(1) {
        result = read_channel(channel, &frame);
        if(address == 0xFFFFFFFF) { /* We're waiting for a command */
            /* We ignore failures while we are waiting for commands
               on the channel. */
            if(result == 0) {
                /* We may as well get the address here */
                address = *(uint32_t *)(&frame.data[1]);
                if(frame.data[0] == 0x01) { /* Fill Buffer */
				    length = frame.data[5] | frame.data[6]<<8;
#ifdef UART_DEBUG
                    uart_write("FB ", 3);
                    itoa(address, sout, 10);
                    uart_write(sout, strlen(sout));
					uart_write(" ", 1);
					itoa(length, sout, 10);
					uart_write(sout, strlen(sout));
					uart_write("\n", 1);
#endif
                } else if(frame.data[0] == 0x02) { /* Page Erase */
				    boot_page_erase_safe(address);
#ifdef UART_DEBUG
                    uart_write("EP ", 3);
					itoa(address, sout, 10);
                    uart_write(sout, strlen(sout));
					uart_write("\n", 1);
#endif
					address = 0xFFFFFFFF; /* So we don't try to read data */
                } else if(frame.data[0] == 0x03) { /* Page Write */
				    boot_page_write_safe(address);
#ifdef UART_DEBUG
                    uart_write("WP ", 3);
					itoa(address, sout, 10);
                    uart_write(sout, strlen(sout));
					uart_write("\n", 1);
#endif
					address = 0xFFFFFFFF; /* So we don't try to read data */
                } else if(frame.data[0] == 0x04) { /* Abort */
#ifdef UART_DEBUG
                    uart_write("A\n", 2);
#endif
					address = 0xFFFFFFFF; /* So we don't try to read data */
                } else if(frame.data[0] == 0x05) { /* Complete */
				    crc = *(uint16_t *)(&frame.data[1]);
                    temp = *(uint32_t *)(&frame.data[3]); /* Size */
                    frame.id++; /* Add one for the response channel */
					can_send(0, 3, frame); /* Send Response */
					store_crc(crc, temp);
					
#ifdef UART_DEBUG
					uart_write("C\n", 2);
#endif
                    reset();
                }
                frame.id++; /* Add one for the response channel */
                can_send(0, 3, frame); /* Send Response */
            } else if(result == 2) { /* Timeout */
			    to_count++;
				if(to_count > 30) return;
			}
        } else { /* We're waiting for buffer data. */
            if(result == 0) {
			    for(n=0; n<frame.length; n+=2) {
				    temp = *(uint16_t *)(&frame.data[n]);
					boot_page_fill_safe(address+offset+n, temp);
				}
				offset+=frame.length;
#ifdef UART_DEBUG
				uart_write(".", 1);
#endif
                /* The following is an ack for buffer load data
				   I don't know that we really need it. */
				frame.id++;
				frame.data[0] = offset;
				frame.data[1] = (offset & 0xFF00) >>8;
				frame.length = 2;
				can_send(0, 3, frame);
				if(offset >= length) {
				    address = 0xFFFFFFFF; /* To get out of here */
					offset = 0;
#ifdef UART_DEBUG
					uart_write("#\n", 2);
#endif
                }

			} else if(result == 2) { /* This is a timeout */
                address = 0xFFFFFFFF;
				offset = 0;
#ifdef UART_DEBUG
				uart_write("t\n", 2); /* TODO: Should send an abort */
#endif
			}
        }
    }
}

/* TESTING ONLY */
void
print_frame(struct CanFrame frame)
{
#ifdef UART_DEBUG
    char sout[5];
    int n;
	
	uart_write("CAN", 3);
    itoa(frame.id, sout, 16);
    uart_write(sout, strlen(sout));
    uart_write("D", 1);

    for(n=0; n<frame.length; n++) {
        itoa(frame.data[n], sout, 16);
        if(frame.data[n] <= 0x0F) {
            sout[1] = sout[0];
            sout[0] = '0';
        }
        uart_write(sout, 2);
    }
    uart_write("\n", 1);
#endif
}

/* This function checks each Rx buffer in the MCP2515 starting with
   buffer 1 since that will have the older frame in it.  If that has
   a node specific message in it then we return 1.  Else try Rx buffer
   0 and do the same.  If there are no node specific messages in either
   buffer then return 0 */
uint8_t
get_ns_frame(struct CanFrame *frame) {
	uint8_t result;
	
    result = can_poll_int();

 /* We're going to look in both buffers starting with 1 since that would
    be where the oldest of the two would be. */
    if(result & (1<<CAN_RX1IF)) {
        can_read(1, frame);
		if(frame->id >= FIX_NODE_SPECIFIC && frame->id < (FIX_NODE_SPECIFIC+256)) {
			return 1;
		}
	} else if(result & (1<<CAN_RX0IF)) {
		can_read(0, frame);
		if(frame->id >= FIX_NODE_SPECIFIC && frame->id < (FIX_NODE_SPECIFIC+256)) {
			return 1;
		}
	}
	return 0;
}

/* This is the function that we call periodically during the one
   second startup time to see if we have a bootloader request on
   the CAN Bus. */
uint8_t
bload_check(void) {
    struct CanFrame frame;
    uint8_t result, channel, send_node;
    
    result = get_ns_frame(&frame);
	if(result) { /* If this is true then a node specific message is in &frame */
		print_frame(frame); /* Debugging Stuff */
        if(frame.data[1] == node_id && frame.data[0] == FIX_FIRMWARE &&
           frame.data[2] == BL_VERIFY_LSB && frame.data[3] == BL_VERIFY_MSB) {
            /* Save the data from the frame that we need later. */  
            channel = frame.data[4];
            send_node = frame.id - 0x6E0;
            /* Build success frame */
            frame.id = FIX_NODE_SPECIFIC + node_id;
            frame.length = 3;
            frame.data[0] = FIX_FIRMWARE;
            frame.data[1] = send_node;
            frame.data[2] = 0x00;
            can_send(0, 3, frame);
			/* Jump to load firmware */
            load_firmware(channel); /* We should never come back from here */
        } 
    }
    return 0;
}

/* This calculates a CRC16 for the program memory starting at 
   address 0x0000 and going up to count-1 */
#if PGM_LENGTH_BITS == 16
uint16_t
pgmcrc(uint16_t count) {
    uint16_t carry;
    uint16_t crc = 0xffff;
	uint16_t addr = 0; 

    while(addr != count) {
        int i = 8;

        bload_check();

        crc ^= pgm_read_byte_near(addr++);
        while(i--)
        {
            carry = crc & 0x0001;
            crc >>= 1;
            if (carry) crc ^= 0xA001;
        }
    }
    return crc;
}
#elif PGM_LENGTH_BITS == 32
uint16_t
pgmcrc(uint32_t count) {
	uint16_t crctable[256];
	uint8_t temp;
	uint16_t crc = 0xffff;
	uint32_t addr = 0;
	uint8_t i;
	
	/* Since we could be dealing with really large programs we are going
	   to pre-calculate the CRC table and use it instead of all the bit shifting */
	for(int n=0; n<256; n++) {
		i = 8;
		crctable[n] = n;
		while(i--) {
			if(crctable[n] & 0x01)
			    crctable[n] = (crctable[n] >> 1) ^ 0xA001;
			else
			    crctable[n] = crctable[n] >> 1;
		}
	}
		
	while(addr != count) {
		if(addr % 64 == 0)
		    bload_check();

		temp = pgm_read_byte_far(addr++) ^ crc;
		crc >>=8;
		crc ^= crctable[temp];

	}
	return crc;
}
#endif

/* Main Program Routine */
int
main(void)
{
	struct CanFrame frame;
    uint16_t pgm_crc, cmp_crc, timer=0;
	uint8_t crcgood=0;
	
#if PGM_LENGTH_BITS == 16
    uint16_t count;
#elif PGM_LENGTH_BITS == 32
    uint32_t count;
#endif
#ifdef UART_DEBUG
    char sout[8];    
#endif

	init();
#ifdef UART_DEBUG
	uart_write("\nStart Node ", 12);
	itoa(node_id,sout, 16);
	uart_write(sout, strlen(sout));
	uart_write("\n", 1);
#endif

#if PGM_LENGTH_BITS == 16
    /* Find the firmware size and checksum */
	count   = pgm_read_word_near(PGM_LENGTH);
    cmp_crc = pgm_read_word_near(PGM_CRC);
#elif PGM_LENGTH_BITS == 32
	count   = pgm_read_dword_far(PGM_LENGTH);
	cmp_crc = pgm_read_word_far(PGM_CRC);
#endif
    if(count >= PGM_LAST_PAGE_START + PGM_PAGE_SIZE) count = PGM_LAST_PAGE_START + PGM_PAGE_SIZE; /* bounds check */
	/* Retrieve the Program Checksum */
	pgm_crc = pgmcrc(count);
	/* If it matches then set the good flag */
	if(pgm_crc == cmp_crc) {
	    crcgood = 1;
    }
#ifdef UART_DEBUG
    itoa(pgm_crc,sout,16);
	uart_write("Checksum ", 9);
	uart_write(sout,strlen(sout));
	itoa(cmp_crc,sout,16);
	uart_write(" ?= ", 4);
	uart_write(sout,strlen(sout));
	uart_write("\n",1);
#endif
	/* This timer expires at roughly one second after startup */
	while(TCNT1 <= 0x2B00) /* Run this for about a second */
        bload_check();
    TCNT1 = 0x0000;
#ifdef UART_DEBUG
	uart_write("TIMEOUT\n",8);
#endif
	if(crcgood) {
	   start_app(); /* When we go here we ain't never comin' back */
    }
	
    /* If CRC is no good we sit here and look for a firmware update command
       forever. */
#ifdef UART_DEBUG
	uart_write("Program Fail\n",13);
#endif
    PORTB |= (1<<PB0);
    while(1) { 
		if(timer == 0) {
			/* Send a node alarm message indicating a firmware failure */
			frame.id = node_id;
			frame.length = 4;
			frame.data[0] = 0x00; /* Alarm type LSB */
			frame.data[1] = 0x00; /* Alarm type MSB */
			frame.data[2] = (uint8_t)(pgm_crc & 0x00FF); /* Send current checksum */
			frame.data[3] = (uint8_t)(pgm_crc >> 8);
			can_send(0, 3, frame);
		}
		timer++;
        bload_check();
	}	
}

