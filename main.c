#include "common.h"
#include "ufat/ufat.h"
#include "SD.h"


__CONFIG(FOSC_INTOSC & WDTE_SWDTEN & PWRTE_OFF & MCLRE_OFF & CP_OFF & CPD_OFF & BOREN_OFF & CLKOUTEN_OFF & IESO_OFF & FCMEN_OFF);
__CONFIG(WRT_OFF & PLLEN_OFF & STVREN_ON & BORV_19 & LVP_OFF);
__IDLOC(0000);

UInt8 eeRead(UInt8 addr){


	EECON1 = 0b00000000;	//read data
	EEADRL = addr;
	EECON1bits.RD = 1;		//do it
	return EEDATL;
}

void eeWrite(UInt8 addr, UInt8 data){

	EECON1= 0b00000100;	//write data
	EEADRL = addr;
	EEDATL = data;
	INTCONbits.GIE = 0;
	EECON2 = 0x55;
	EECON2 = 0xAA;
	EECON1bits.WR = 1;
	INTCONbits.GIE = 1;
	while(EECON1bits.WR);
}

static UInt16 rnd(void){

	UInt32 x;

	x = eeRead(0xFF);
	x = (x << 8) | eeRead(0xFE);
	x = (x << 8) | eeRead(0xFD);
	x = (x << 8) | eeRead(0xFC);
	
	x = x * 0xDEECE66D + 0x0B;

	eeWrite(0xFF, x >> 24);
	eeWrite(0xFE, x >> 16);
	eeWrite(0xFD, x >> 8);
	eeWrite(0xFC, x >> 0);

	return x >> 16;
}

void log(UInt8 val){

	static UInt8 addr = 0;

	eeWrite(addr++, val);

	while(!addr);
}

#define BUF_SZ				90
#define MAX_SECTOR_RANGES	50	//  = (EEPROM_SIZE - 4/*for random number generator*/) / 5
#define CP1_CON_VAL			0b00001100

static UInt8 gRead = 0;
static UInt8 gWrite = 1;
static UInt8 gBuffer[BUF_SZ];
static volatile UInt8 tmrReload;
static UInt8 byteStride;		//for non-8-bit-per-sample-mono


void secListRead(UInt8 which, UInt24* start, UInt16* len){

	UInt24 t24;
	UInt16 t16;
	UInt8 i;

	if(which >= MAX_SECTOR_RANGES){
		*start = 0;
		*len = 0;
	}

	which = which + (which << 2);	// which *= 5 :)
	
	t24 = 0;
	for(i = 0; i < 3; i++) t24 = (t24 << 8) | eeRead(which++);
	*start = t24;

	t16 = 0;
	for(i = 0; i < 2; i++) t16 = (t16 << 8) | eeRead(which++);
	*len = t16;
}

void secListWrite(UInt8 which, UInt24 start, UInt16 len){

	UInt8 i;

	if(which >= MAX_SECTOR_RANGES) return;

	which = which + (which << 2);	// which *= 5 :)
	
	for(i = 0; i < 3; i++, start >>= 8) eeWrite(which + (2 - i), start);
	for(i = 0; i < 2; i++, len >>= 8) eeWrite(which + (4 - i), len);
}

void fatal(UInt8 val){		//fatal error: beep error number a few times, then go to sleep

	UInt8 i, j, k;

	for(j = 0; j < 5; j++){
	
		for(k = 0; k < val; k++){
			__delay_ms(300);
			for(i = 0; i < 100; i++){
				RA5 = 1;
				__delay_ms(1);
				RA5 = 0;
				__delay_ms(1);
			}
		}

		__delay_ms(3000);
	}
	RA0 = 0;	//SD off

	while(1){
		#asm
			SLEEP
		#endasm
	}
}


void audioOn(void){

	UInt8 i;

	for(i = 0; i < BUF_SZ; i++) gBuffer[i] = 0x80;

	T2CON = 0b00000100;			//timer2 on
	PR2 = 31;	//should be 7, but that gives bad waveforms so we sacrifice some volume for better reproduction. 15 works beter, 31 best
	CCP1CON = CP1_CON_VAL;		//PWM on
	CCPR1H = 0;
	CCPR1L = 0;

	TRISAbits.TRISA5 = 0;	//pin is output

	TMR0 = 0;
	INTCONbits.TMR0IF = 0;
	INTCONbits.TMR0IE = 1;
}

void audioOff(void){

	INTCONbits.TMR0IE = 0;
	CCP1CON = 0;
	TRISAbits.TRISA5 = 0;	//pin is output
	RA5 = 0;
}


void sleep(UInt32 ms){		//sleeps unconditionally and approximately this many milliseconds...

#define HIGHEST_WDT_BIT		18

	Int8 i;
	UInt8 oldClkCfg, oldTris;
	const UInt32 v_initializer = 1UL << HIGHEST_WDT_BIT;	//bc this compiler sucks
	UInt32 v = v_initializer;

	oldClkCfg = OSCCON;
	OSCCON = 0b00010011;	//switch to slower clock
	RA5 = 0;
	oldTris = TRISA;
	TRISA = 0b11011110;	//all in except RA5 and RA0
	RA0 = 0;	

	for(i = HIGHEST_WDT_BIT; i >= 0; i--, v >>= 1){

		while(ms >= v){
			#asm
				CLRWDT
			#endasm
			WDTCON = (i << 1) | 1;
			ms -= v;
			#asm
				SLEEP
			#endasm
		}
	}
	SWDTEN = 0;

	TRISA = oldTris;
	OSCCON = oldClkCfg;	//switch to 31KHz clock
}


bool ufatExtRead(u32 sector, u16 offset, u8 len, u8* buf){

	static u32 curSec = 0xFFFFFFFFUL;
	static u16 curPos = 0;

	if(sector != curSec || offset < curPos){

		if(curSec != 0xFFFFFFFFUL){

			while(curPos++ != 512) sdSpiByte(0xFF);	//fast forward to sector end
			sdSecReadStop();
			curSec = 0xFFFFFFFFUL;
		}
		if(!sdReadStart(curSec = sector)) return false;
		curPos = 0;
	}

	while(curPos != offset){	//skip to where we're needed
		curPos++;
		sdSpiByte(0xFF);
	}

	curPos += len;
	while(len--) *buf++ = sdSpiByte(0xFF);
	
	if(curPos == 512){
		sdSecReadStop();
		curSec = 0xFFFFFFFFUL;
	}

	return true;
}

void ufatExtReadTerminate(void){

	//finish reading whatever sector we're reading so that we can issue commands to the SD card
	ufatExtRead(0, 512, 0, NULL);
}

static UInt8 byte(){

	return sdSpiByte(0xFF);
}

static Boolean spiCmp(const char* with, UInt8 len){	//return true for match, false  otherwise

	while(len--) if(byte() != *with++) return false;

	return true;
}

static UInt8 hdrProcess(){	//read at most 127 bytes and use them, return number read. return zero if file is invalid

	static const UInt8 riff[4] = {'R', 'I', 'F', 'F'};
	static const UInt8 wave[4] = {'W', 'A', 'V', 'E'};
	UInt8 csz, i = 0, j;
	UInt32 t32;


	//check for RIFF header
	if(!spiCmp(riff, 4)) return 0;	//no RIFF header -> not MS RIFF media format -> fail

	//skip file size
	for(j = 0; j < 4; j++) byte();

	//check for WAVE format header
	if(!spiCmp(wave, 4)) return 0;	//no WAVE type identifier header -> not WAVE format -> fail
	i += 12;

	
	while(i < 127){	//look for "fmt "header in the first 127 bytes

		UInt8 hdr[4];

		for(j = 0; j < 4; j++) hdr[j] = byte();										//read chunk type
		csz = byte();																//read chunk size
		if(byte() || byte() || byte()) return 0;									//chunk over 256 bytes? -> too big to skip
		i += 8;

		if(hdr[0] == 'f' && hdr[1] == 'm' && hdr[2] == 't' && hdr[3] == ' '){		//our lucky day -> it's the format chnk

			byteStride = 1;

			if(byte() != 1 || byte() != 0) return 0;	 							//not PCM format -> fail
			i += 2;

			j = byte();
			if(byte() != 0) return 0;												//over 256 channels is not supported!
			if(!j) return 0;														//zero channels also not so good
			byteStride *= j;														//we only play the first channel
			i += 2;
		
			t32 = 0;
			for(j = 0; j < 4; j++) t32 = (t32 >> 8) | (((UInt32)byte()) << 24);	//read sample rate
			i += 4;

#define DIV	(4 * t32)

			tmrReload = 69;

			t32 = (_XTAL_FREQ + (DIV / 2)) / DIV;
			//sz is now the delay we need. we need to split it equally between OPTION reg and tmrReload
			j = 0;
			while(t32 > 256){

				j++;
				t32 >>= 1;
			}
			if(j >= 8) return 0;	//too slow...

			OPTION_REG = (OPTION_REG & 0xF0) | (j ? (j - 1) : 8);
			tmrReload = 256 - t32;			

			for(j = 0; j < 6; j++) sdSpiByte(0xFF);									//skip byte rate and clock align
			i += 6;

			if(sdSpiByte(0xFF) != 8 || sdSpiByte(0xFF) != 0) return 0;				//8 bits per sample only please
			i += 2;
		
			byteStride--;
			return i;
		}
		else{																		//skip this chunk and go on

			if(csz > 0x80) return 0;												//too big a chunk to skip -> give up
			i += csz;
			while(csz--) byte();
		}
	}

	return i;
}

static void play(){

	Boolean ret, start = true;
	UInt24 firstSec;
	UInt16 numSec;
	UInt24 sec;
	UInt8 secListIdx = 0;
	UInt8 i, t = 0;

	audioOn();

	while(1){

		secListRead(secListIdx++, &firstSec, &numSec);
		if(!firstSec && !numSec) break;

		ret = sdReadStart(firstSec);
		if(!ret) fatal(2);
	
		if(start){	//first part of file is header -> get some useful info out of it

			i = hdrProcess();
			if(i == 0){
				sdSecReadStop();
				break;				//file invalid -> fail
			}
			start = false;
		} else i= 0;

		for(sec = 0; sec < numSec; sec++){
			UInt8 j;

			if(sec){
				i = 0;
				sdNextSec();
			}

			for(j =0; j < 4; j++){		//faster than 16 bit counter to 512

				if(j) i = 0;
				while(i != 128){
	
					if(t){
						t--;
						sdSpiByte(0xFF);
						i++;
					}
					else if(gWrite != gRead){

						t = sdSpiByte(0xFF);
	
						gBuffer[gWrite++] = t >> 2;
						if(gWrite == BUF_SZ) gWrite = 0;
						i++;
						t = byteStride;
					}
				}
			}
		}
		sdSecReadStop();
	}

	audioOff();
}

UInt16 measureBattery(void){	//result in millivolts

	UInt24 t = 0;
	UInt8 i;

	FVRCON	= 0b11000001;	//1.024V Vref on
	ADCON0	= 0b01111101;	//configure ADC to measure 0.6 v reference
	ADCON1	= 0b11110000;	//Convert using Vdd as ref
	__delay_ms(1);
	for(i = 0; i < 10; i++){
		__delay_us(160);
		ADCON0bits.GO_nDONE = 1;
		while(ADCON0bits.GO_nDONE);	//wait
		t += ADRES;
	}
	FVRCON = 0b00000000;	//Vref off
	ADCON0 &=~ 0b00000001;	//ADC off

	/*
		10*t = 1.024*10*1023/Vcc
		Vcc = 10.24*1023/10*t
		Vcc*1000=10240*1023/10*t
		Vcc*1000=10475520/t
	*/

	t = (10475520 + (t >> 1)) / t;

	return t;
}

void main(void){

	Boolean ret;
	char name[11];
	UInt8 flags;
	UInt16 n, i = 0, id, numFiles = 0;
	UInt32 sec, sz;
	
	OSCCON				= 0b11110000;	//32 MHz clock
	OSCTUNE				= 0x1F;			//33 MHz clok really
	INTCON				= 0b10000000;	//ints enabled, all sources masked
	APFCON				= 0b01000001;	//MOSI on RA4, CCP1 on RA5
	TRISA				= 0b11001100;	//out on 0, 1, 4, 5, in on 2, DNK on 3
	ANSELA				= 0;			//no analog inputs
	OPTION_REG			= 0b10000000;	//no weak pull-ups, timer0 at OSC/4/2 = osc/ 8

	SWDTEN				= 0;			//WDT off for now	

	__delay_ms(64);						//make sure EEPROM is ready for writes by the time we finish this
	__delay_ms(500);

	n = measureBattery();
	if(n > 3600) fatal(10);	//too high voltage -> unsafe to power on the sd card
	if(n < 2500) fatal(11);	//too low voltage -> SD card might be unstable
	
	RA0 = 1;		//card power on
	ret = sdInit();
	if(!ret) fatal(1);

	ufatInit();
	ret = ufatMount();
	if(!ret) fatal(6);

	while(ufatGetNthFile(i, name, &sz, &flags, &id)){
		i++;
		if(flags & (UFAT_FLAG_DIR | UFAT_FLAG_HIDDEN)) continue;				//skip dirs and hidden files
		if(name[8] !='W' || name[9] != 'A' || name[10] != 'V') continue;	//skip non-WAV files
		numFiles++;
	}
	
	if(!numFiles) fatal(3);	//no files

	while(1){
		
		UInt8 j;

		n = rnd() % numFiles;	//pick a random file
	
		i = 0;
		while(ufatGetNthFile(i, name, &sz, &flags, &id)){
			i++;
			if(flags & (UFAT_FLAG_DIR | UFAT_FLAG_HIDDEN)) continue;				//skip dirs and hidden files
			if(name[8] !='W' || name[9] != 'A' || name[10] != 'V') continue;	//skip non-WAV files
			if(!n--) goto found;
		}
	
		fatal(9);
	found:
	
		if(!ufatOpen(id)) fatal(7);

		j = 0;
		while(ufatGetNextSectorRange(&sec, &sz)){
			while(sz){
				UInt16 sv = sz > 65535 ? 65535 : sz;
				secListWrite(j++, sec, sv);
				sec += sv;
				sz -= sv;
			}
		}
		secListWrite(j, 0, 0);
		ufatExtReadTerminate();
		
		play();
		RA0 = 0;		//card off

		sleep(30000);

		RA0 = 1;		//card power on
		ret = sdInit();
		if(!ret) fatal(1);
	}

	while(1);
}

void interrupt isr(void){

//	if(INTCONbits.TMR0IF){		//not needed
		UInt8 v, v1;

		TMR0 = tmrReload;

		v = gBuffer[gRead++];

		v1 = (CP1_CON_VAL & 0xCF) | ((v & 3) << 4);
		v = v >> 2;
		CCPR1L = v;
		CCP1CON = v1;

		if(gRead == BUF_SZ) gRead = 0;
		
		INTCONbits.TMR0IF = 0;
//	}
}