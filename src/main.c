/*
 * Title:			Agon firmware upgrade utility
 * Author:			Jeroen Venema
 * Created:			17/12/2022
 * Last Updated:	02/11/2023
 * 
 * Modinfo:
 * 17/12/2022:		Initial version
 * 05/04/2022:		Changed timer to 5sec at reset.
 *                  Sends cls just before reset
 * 07/06/2023:		Included faster crc32, by Leigh Brown
 * 14/10/2023:		VDP update code, MOS update rewritten for simplicity
 * 02/11/2023:		Batched mode, rewrite of UI
 */

#include <ez80.h>
#include <stdio.h>
#include <stdlib.h>
#include <ERRNO.H>
#include <ctype.h>
#include "mos-interface.h"
#include "flash.h"
#include "agontimer.h"
#include "crc32.h"
#include "filesize.h"
#include "./stdint.h"
#include <string.h>

#define UNLOCKMATCHLENGTH 9
#define EXIT_FILENOTFOUND	4
#define EXIT_INVALIDPARAMETER	19
#define DEFAULT_MOSFIRMWARE	"MOS.bin"

int errno; // needed by standard library
enum states{firmware,retry,systemreset};

char		mosfilename[256];
uint32_t	moscrc = 0;
uint24_t	filesize = 0;

// separate putch function that doesn't rely on a running MOS firmware
// UART0 initialization done by MOS firmware previously
// This utility doesn't run without MOS to load it anyway
int putch(int c)
{
	UINT8 lsr,temt;
	
	while((UART0_LSR & 0x40) == 0);
	UART0_THR = c;
	return c;
}

uint8_t mos_magicnumbers[] = {0xF3, 0xED, 0x7D, 0x5B, 0xC3};
#define MOS_MAGICLENGTH 5
bool containsMosHeader(uint8_t *filestart) {
	uint8_t n;
	bool match = true;

	for(n = 0; n < MOS_MAGICLENGTH; n++) if(mos_magicnumbers[n] != filestart[n]) match = false;
	return match;
}

void print_version(void) {
	printf("Agon legacy firmware update utility\n\r\n\r");
}

bool getResponse(void) {
	uint8_t response = 0;

	printf("Flash firmware (y/n)?");
	while((response != 'y') && (response != 'n')) response = tolower(getch());
	if(response == 'n') printf("\r\nUser abort\n\r\n\r");
	else printf("\r\n\r\n");
	return response == 'y';
}

bool update_mos(void) {
	uint32_t crcresult;
	uint24_t got;
	uint8_t file;
	char* ptr = (char*)BUFFER1;
	uint8_t value;
	uint24_t counter,pagemax, lastpagebytes;
	uint24_t addressto,addressfrom;
	int attempt;
	bool success = false;

	printf("Programming MOS firmware to ez80 flash...\r\n\r\n");
	// Actual work here	
	di();								// prohibit any access to the old MOS firmware

	attempt = 0;
	while((!success) && (attempt < 3)) {
		// start address in flash
		addressto = FLASHSTART;
		addressfrom = BUFFER1;
		// Write attempt#
		if(attempt > 0) printf("Retry attempt #%d\r\n", attempt);
		// Unprotect and erase flash
		printf("Erasing flash... ");
		enableFlashKeyRegister();	// unlock Flash Key Register, so we can write to the Flash Write/Erase protection registers
		FLASH_PROT = 0;				// disable protection on all 8x16KB blocks in the flash
		enableFlashKeyRegister();	// will need to unlock again after previous write to the flash protection register
		FLASH_FDIV = 0x5F;			// Ceiling(18Mhz * 5,1us) = 95, or 0x5F
		
		for(counter = 0; counter < FLASHPAGES; counter++)
		{
			FLASH_PAGE = counter;
			FLASH_PGCTL = 0x02;			// Page erase bit enable, start erase

			do
			{
				value = FLASH_PGCTL;
			}
			while(value & 0x02);// wait for completion of erase			
		}
		printf("\r\n");
				
		// determine number of pages to write
		pagemax = filesize/PAGESIZE;
		if(filesize%PAGESIZE) // last page has less than PAGESIZE bytes
		{
			pagemax += 1;
			lastpagebytes = filesize%PAGESIZE;			
		}
		else lastpagebytes = PAGESIZE; // normal last page
		
		// write out each page to flash
		for(counter = 0; counter < pagemax; counter++)
		{
			printf("\rWriting flash page %03d/%03d", counter+1, pagemax);
			
			if(counter == (pagemax - 1)) // last page to write - might need to write less than PAGESIZE
				fastmemcpy(addressto,addressfrom,lastpagebytes);				
				//printf("Fake copy to %lx, from %lx, %lx bytes\r\n",addressto, addressfrom, lastpagebytes);
			else 
				fastmemcpy(addressto,addressfrom,PAGESIZE);
				//printf("Fake copy to %lx, from %lx, %lx bytes\r\n",addressto, addressfrom, PAGESIZE);
		
			addressto += PAGESIZE;
			addressfrom += PAGESIZE;
		}
		lockFlashKeyRegister();	// lock the flash before WARM reset
		printf("\r\nChecking CRC... ");
		crc32_initialize();
		crc32(FLASHSTART, filesize);
		crcresult = crc32_finalize();
		if(crcresult == moscrc) {
			printf("OK\r\n");
			success = true;
		}
		else {
			printf("ERROR\r\n");
		}
		attempt++;
	}
	printf("\r\n");
	return success;
}

bool filesExist(void) {
	uint8_t file;
	bool filesexist = true;

	file = mos_fopen(mosfilename, fa_read);
	if(!file) {
		printf("Error opening MOS firmware \"%s\"\n\r",mosfilename);
		filesexist = false;
	}
	mos_fclose(file);

	return filesexist;
}

bool validFirmware(void) {
	uint8_t file;
	bool validfirmware = true;

	if(!containsMosHeader((uint8_t *)BUFFER1)) {
		printf("\"%s\" does not contain valid MOS ez80 startup code\r\n", mosfilename);
		validfirmware = false;
	}
	if(filesize > FLASHSIZE) {
		printf("\"%s\" too large for 128KB embedded flash\r\n", mosfilename);
		validfirmware = false;
	}
	return validfirmware;
}

void showCRC32(void) {
	printf("MOS CRC 0x%04lX\r\n", moscrc);
	printf("\r\n");
}

void calculateCRC32(void) {
	uint8_t file;
	uint24_t got,size;
	char* ptr;

	moscrc = 0;

	printf("\r\nCalculating CRC...\r\n");

	ptr = (char*)BUFFER1;
	crc32_initialize();
	crc32(ptr, filesize);	
	moscrc = crc32_finalize();
	printf("\r\n\r\n");
}

uint24_t readMemory(const char *filename) {
	uint8_t file;
	char* ptr = (char*)BUFFER1;
	uint24_t size = 0;

	printf("Reading \"MOS.bin\" to memory");
	file = mos_fopen(mosfilename, fa_read);
	while(!mos_feof(file))
	{
		*ptr = mos_fgetc(file);
		ptr++;
		size++;
		if(size%2048 == 0) printf(".");
	}
	mos_fclose(file);
	return size;
}

int main(int argc, char * argv[]) {	
	sysvar_t *sysvars;
	int n;
	uint16_t tmp;
	sysvars = getsysvars();

	strcpy(mosfilename, DEFAULT_MOSFIRMWARE);

	// All checks
	if(!filesExist()) return EXIT_FILENOTFOUND;
	if(!validFirmware()) {
		return EXIT_INVALIDPARAMETER;
	}

	putch(12);
	print_version();

	filesize = readMemory(mosfilename);
	if(filesize == 0) {
		printf("\r\nError reading from SD card\r\n");
		return 0;
	}

	calculateCRC32();

	showCRC32();
	if(!getResponse()) return 0;

	if(update_mos()) {
		printf("Done\r\n\r\n");
		printf("Please don't forget to update the VDP\r\n");
		printf("It is OK to reset or shut down the system now ");
		while(1); // No live MOS to return to
	}
	return 0;
}

