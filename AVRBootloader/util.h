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
 *  This is the header file for generic functions used throughout the bootloader
 *  program.
 */

#ifndef __BL_UTIL_H
#define __BL_UTIL_H

//#define F_CPU 1000000UL
//#define F_CPU 2000000UL
#define F_CPU 8000000UL

/* CAN_CS should define the pin used for the chip select on the
   MCP2515 CAN Controller.  This is different than the SPI_SS pin
   which must either be used for this purpose or set as an output
   and kept high.
*/

/* CAN Serial Gateway Chip Select */
#define CAN_CS PB4
/* Normal Chip Select for ATmega328P */
//#define CAN_CS PB2

#ifdef __AVR_ATmega328P__
#define SPI_SS   PB2
#define SPI_MOSI PB3
#define SPI_MISO PB4
#define SPI_SCK  PB5
#endif

#ifdef __AVR_ATmega2561__
#define SPI_SS   PB0
#define SPI_MOSI PB2
#define SPI_MISO PB3
#define SPI_SCK  PB1
#endif

#define SPI_DDR  DDRB
#define SPI_PORT PORTB
#define SPI_SS_LOW()  (SPI_PORT &= ~(1<<CAN_CS))
#define SPI_SS_HIGH() (SPI_PORT |= (1<<CAN_CS))
#define SPI_DELAY 24

// General Definitions
#define BIT(x) (1 << (x))
#define SETBITS(x,y) ((x) |= (y))
#define CLEARBITS(x,y) ((x) &= (~(y)))
#define SETBIT(x,y) SETBITS((x), (BIT((y))))
#define CLEARBIT(x,y) CLEARBITS((x), (BIT((y))))
#define BITSET(x,y) ((x) & (BIT(y)))
#define BITCLEAR(x,y) !BITSET((x), (y))
#define BITISSET(x,y) (((x) & (y)) == (y))
#define BITISCLEAR(x,y) (((x) & (y)) == 0)

/* cutil.c function */
void spi_write(uint8_t *write_buff, uint8_t *read_buff, uint8_t size);

/* util.S functions */
void start_app(void);
void reset(void);

#endif
