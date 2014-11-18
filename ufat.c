#include "ufat.h"

#define UFAT_FLAG_VOLUME_LABEL	8
#define UFAT_FLAG_DEVICE		64
#define UFAT_FLAG_RESERVED		128
#define UFAT_FLAG_LFN			(UFAT_FLAG_VOLUME_LABEL | UFAT_FLAG_READONLY | FAT_FLAG_HIDDEN | UFAT_FLAG_SYSTEM)

#define EOC_16						0xfff8
#define EOC_12						0xff8
#define CLUS_INVALID				0xffff

//fat16 only, very very limited

static u32 diskOffset = 0;		//to beginning of fs
static u8 secPerClus;
static u16 rootDirEntries;
static u16 sectorsPerFat;
static u16 fatSec;		//where fat begin
static u16 rootSec;		//where root directory begins
static u16 dataSec;		//where data begins
static u16 curClus = CLUS_INVALID;


static bool ufatParsePartitionTable(void){

	char record[16];
	u16 offset;

	if(diskOffset) return false;	//partitions inside partitions do not exist, probbay no fat FS on this disk - bail out

	for(offset = 0x1BE; offset < 0x1FE; offset += 16){

		if(!ufatExtRead(0, offset, 16, record)) return false;
		if(record[4] != 1 && record[4] != 4 && record[4] != 6 && record[4] != 0x0B && record[4] != 0x0C && record[4] != 0x0E) continue;	//not FAT parition

		//we now have a contender - try to mount it
		diskOffset = record[11];
		diskOffset = (diskOffset << 8) | record[10];
		diskOffset = (diskOffset << 8) | record[9];
		diskOffset = (diskOffset << 8) | record[8];
		if(ufatMount()) return true;
	}
	//if we got here, we failed - give up and cry
	return false;
}

static u16 ufatGetU16(const char* v, u8 idx){

	v += idx;
	return (((u16)v[1]) << 8) | ((u16)v[0]);
}

static u32 ufatGetU32(const char* v, u8 idx){

	v += idx;
	return (((u32)v[3]) << 24) | (((u32)v[2]) << 16) | (((u32)v[1]) << 8) | ((u32)v[0]);
}

void ufatInit(void){

	diskOffset = 0;
}

bool ufatMount(void){

	char buf[13];

	if(!ufatExtRead(diskOffset, 0x36, 4, buf)) return false;
	if(buf[0] !='F' || buf[1] !='A' || buf[2] != 'T' || buf[3] != '1'){	//may be a partition table

		return ufatParsePartitionTable();
	}

	if(!ufatExtRead(diskOffset, 0x0B, 13, buf)) return false;
	if(ufatGetU16(buf, 0x0B - 0x0B) != 512) return false;		//only 512 bytes/sector FSs supported
	secPerClus = buf[0x0D - 0x0B];
	fatSec = ufatGetU16(buf, 0x0E - 0x0B);	//"reserved sectors" = sectors before first fat
	rootDirEntries = ufatGetU16(buf, 0x11 - 0x0B);
	sectorsPerFat = ufatGetU16(buf, 0x16 - 0x0B);
	
	rootSec = fatSec + sectorsPerFat * (u16)(buf[0x10 - 0x0B]);
	dataSec = rootSec + (((u32)rootDirEntries) * 32 + UFAT_DISK_SECTOR_SZ - 1) / UFAT_DISK_SECTOR_SZ;

	return true;
}

bool ufatGetNthFile(u16 n, char* name, u32* sz, u8* flags, u16* id){

	u16 i;
	u32 sec = diskOffset + rootSec;
	u16 offset = 0;
	u8 buf[4];

	for(i = 0; i < rootDirEntries; i++){

		if(!ufatExtRead(sec, offset, 1, buf)) return false;
		if(buf[0] == 0) break;	//no more entries
		if(buf[0] != 0xE5 && buf[0] != 0x2E){		//we process only non-deleted, non "." and ".." entries

			if(!n--){		//we found it

				if(name){

					name[0] = (buf[0] == 0x05) ? 0xE5 : buf[0];
					if(!ufatExtRead(sec, offset + 1, 10, name + 1)) return false;
				}

				if(flags){

					if(!ufatExtRead(sec, offset + 0x0B, 1, flags)) return false;
				}

				if(id){

					if(!ufatExtRead(sec, offset + 0x1A, 2, buf)) return false;
					*id = ufatGetU16(buf, 0);
				}

				if(sz){

					if(!ufatExtRead(sec, offset + 0x1C, 4, buf)) return false;
					*sz = ufatGetU32(buf, 0);
				}

				return true;
			}
		}
		offset += 32;
		if(offset == UFAT_DISK_SECTOR_SZ){
			offset = 0;
			sec++;
		}
	}

	//we fail
	return false;
}

bool ufatOpen(u16 id){

	curClus = id;
	return true;
}

u16 ufatGetNextClus(u16 clus){

	char buf[2];
	u32 sec = diskOffset + fatSec;
	u16 offset;

	sec += clus / (UFAT_DISK_SECTOR_SZ / 2);
	offset = (clus % (UFAT_DISK_SECTOR_SZ / 2)) * 2;

	if(!ufatExtRead(sec, offset, 2, buf)) return CLUS_INVALID;

	clus = ufatGetU16(buf, 0);
	if(clus >= EOC_16) return CLUS_INVALID;

	return clus;
}
bool ufatGetNextSectorRange(u32* first, u32* len){

	u16 next = curClus, prev;
	u32 t;


	if(curClus == CLUS_INVALID) return false;

	do{

		prev = next;
		next = ufatGetNextClus(prev);
	}while(next == prev + 1 && next != CLUS_INVALID);

	//prev is now the last cluster in this chain that is in sequence with previous ones
	//next is now the next cluster (not in sequence - fragment)

	t = prev + 1 - curClus;
	t *= secPerClus;
	*len = t;

	t = (curClus - 2);
	t *= secPerClus;
	t += dataSec;
	t += diskOffset;
	*first = t;

	curClus = next;

	return true;
}