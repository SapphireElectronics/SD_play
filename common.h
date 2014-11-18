#ifndef _COMMON_H_
#define _COMMON_H_


//#include <htc.h>
#define _XTAL_FREQ 33000000		//with osctune maxed out

typedef int8 Int8;
typedef uns8 UInt8;
typedef uns8 Boolean;
typedef uns32 UInt32;
typedef uns24 UInt24;
typedef uns16 UInt16;

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
