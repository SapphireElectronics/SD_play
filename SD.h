#ifndef _SD_H_
#define _SD_H_

#include "common.h"




#define SD_BLOCK_SIZE		512

Boolean sdInit();
UInt8 sdSpiByte(UInt8 byte);

Boolean sdReadStart(UInt24 sec);
void sdNextSec();
void sdSecReadStop();



#endif
