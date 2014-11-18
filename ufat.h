#ifndef _UFAT_H_
#define _UFAT_H_

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned char bool;

#ifndef true
	#define true 1
#endif
#ifndef false
	#define false 0
#endif
#ifndef NULL
	#define NULL ((void*)0)
#endif

#define UFAT_DISK_SECTOR_SZ		512

#define UFAT_FLAG_READONLY		1
#define UFAT_FLAG_HIDDEN		2
#define UFAT_FLAG_SYSTEM		4
#define UFAT_FLAG_DIR			16
#define UFAT_FLAG_ARCHIVE		32

//externally required function(s)
bool ufatExtRead(u32 sector, u16 offset, u8 len, u8* buf);

void ufatInit(void);													//init fs driver
bool ufatMount(void);													//try mounting a volume
bool ufatGetNthFile(u16 n, char* name, u32* sz, u8* flags, u16* id);	//in root directory only, false for no more
bool ufatOpen(u16 id);													//in root directory only
bool ufatGetNextSectorRange(u32* first, u32* len);						//for currently opened file, false for "no more"

#endif
