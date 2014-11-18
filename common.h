#ifndef _COMMON_H_
#define _COMMON_H_


#include <htc.h>
#define _XTAL_FREQ 33000000		//with osctune maxed out

typedef signed char Int8;
typedef unsigned char UInt8;
typedef unsigned char Boolean;
typedef unsigned long UInt32;
typedef unsigned short long UInt24;
typedef unsigned short UInt16;

#define inline

#define true	1
#define false	0
#define NULL	((void*)0)

void log(UInt8);



//RA2 = MISO
//RA1 = CLK
//RA4 = MOSI

//RA0 = card power

#endif
