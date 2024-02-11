//
// Created by Krystian on 25.11.23.
//

#ifndef PROJECT1_FILE_READER_H
#define PROJECT1_FILE_READER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

#define SECTOR_SIZE 512
#define FIRST_CLUSTER_OFFSET 2
#define MAX_NUM_OF_SECTORS_IN_FAT16 65535
#define SIGNATURE_VALUE 0xAA55
#define NOT_DIR_FILE_LENGTH 10
#define FILE_NAME_LENGTH 11
#define EXTENSION_LENGTH 3
#define EXTENSION_LENGTH_WITH_DOT 4
#define FILE_LENGTH_TO_DOT 7
#define FILE_DELETED (char) 0xe5
#define LAST_ENTRY (char) 0x00
#define DIR_ATTR_VALUE 16
#define VOLUME_LABEL_ATTR_VALUE 8
#define IS_NOT_DIR 0
#define END_OF_DIR_NAME 10
#define DOT_OFFSET 8
#define END_OF_FULL_FILE_NAME 12
#define READ_ONLY 0
#define IS_HIDDEN 1
#define IS_SYSTEM 2
#define IS_VOLUME_LABEL 3
#define IS_DIRECTORY 4
#define IS_ARCHIVED 5

typedef struct fatBootSector {
	unsigned char jmpBoot[3];               //0-2	Assembly code instructions to jump to boot code (mandatory in bootable partition)
	unsigned char OEMName[8];               //3-10	OEM name in ASCII
	uint16_t BytesPerSector;                //11-12	Bytes per sector (512, 1024, 2048, or 4096)
	unsigned char SectorPerCluster;         //13	Sectors per cluster (Must be a power of 2 and cluster size must be <=32 KB)
	uint16_t SizeReservedArea;              //14-15	Size of reserved area, in sectors
	unsigned char NumFATs;                  //16	Number of FATs (usually 2)
	uint16_t MaxNumOfFiles;                 //17-18	Maximum number of files in the root directory (FAT12/16; 0 for FAT32)
	uint16_t NumOfSectors1;                 //19-20	Number of sectors in the file system; if 2 B is not large enough, set to 0 and use 4
	// B value in bytes 32-35 below
	unsigned char MediaType;                //21    Media type (0xf0=removable disk, 0xf8=fixed disk)
	uint16_t FatSize;                       //22-23	Size of each FAT, in sectors, for FAT12/16; 0 for FAT32
	uint16_t SectorsPerTrack;               //24-25	Sectors per track in storage device
	uint16_t NumOfHeads;                    //26-27	Number of heads in storage device
	uint32_t NumOfSectorsStartPartition;    //28-31	Number of sectors before the start partition
	uint32_t NumOfSectors2;                 //32-35	Number of sectors in the file system; this field will be 0 if the 2B field above
	// (bytes 19-20) is non-zero
	unsigned char BIOSINT13h;               //36	BIOS INT 13h (low level disk services) drive number
	unsigned char NOTused;                  //37	Not used
	unsigned char ExtendedBootSignature;    //38	Extended boot signature to validate next three fields (0x29)
	uint32_t VolumeSerialNumber;            //39-42	Volume serial number
	char VolumeLabel[11];                   //43-53	Volume label, in ASCII
	char FileSystemTypeLevel[8];            //54-61	File system type level, in ASCII. (Generally "FAT", "FAT12", or "FAT16")
	char NOTused2[448];                     //62-509	Not used
	uint16_t SignatureValue;                //510-511	Signature value (0xaa55)
}__attribute__((packed)) fatBootSector;


struct disk_t {
	FILE *pFile;
	uint32_t numberOfSectors;
};

struct disk_t *disk_open_from_file(const char *volume_file_name);

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read);

int disk_close(struct disk_t *pdisk);

struct volume_t {
	struct disk_t *disk;
	struct fatBootSector bootSector;
	void *FAT1;
	void *FAT2;
	void *rootDirectory;
};

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector);

int fat_close(struct volume_t *pvolume);

struct clusters_chain_t {
	uint16_t *clusters;
	char *clusterBuffer;
	size_t clusterOffset;
	size_t size;
};

struct clusters_chain_t *get_chain_fat16(const void *const buffer, size_t size, uint16_t first_cluster);

struct date_t {
	uint16_t year: 7;
	uint16_t month: 4;
	uint16_t day: 5;
};
struct time_t {
	uint16_t hours: 5;
	uint16_t minutes: 6;
	uint16_t seconds: 5;
};
struct SFN_t {
	char filename[11];
	unsigned char fileAttribute;
	unsigned char reservedNT;
	unsigned char fileCreationTime;
	struct time_t creationTime;
	struct date_t creationDate;
	uint16_t lastAccessDate;
	uint16_t firstClusterNumberHighBits;
	struct time_t lastModificationTime;
	struct date_t lastModificationDate;
	uint16_t firstClusterNumberLowBits;
	uint32_t fileSize;
}__attribute__((__packed__));

struct file_t {
	struct SFN_t file_info;
	struct clusters_chain_t *chain;
	struct volume_t *volume;
	size_t offset;
};

struct file_t *file_open(struct volume_t *pvolume, const char *file_name);

int file_close(struct file_t *stream);

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);

int32_t file_seek(struct file_t *stream, int32_t offset, int whence);

void fixFileName(const char *fileName, char fixedFileName[FILE_NAME_LENGTH]);

bool checkIfFileExist(struct SFN_t *file, char *changedFileName);

struct dir_t {
	void *data;
	int size;
	int offset;
	short readDirs;
};

struct dir_entry_t {
	char name[13];
	size_t size;
	int is_archived;
	int is_readonly;
	int is_system;
	int is_hidden;
	int is_directory;
};

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path);

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry);

int dir_close(struct dir_t *pdir);

bool checkForExtension(const char *dirName);

void fixDirName(char *dirName, char fixedDirName[13], int isDirectory);

void addOffsetAndChangeDirAttr(struct dir_t *pdir);


#endif //PROJECT1_FILE_READER_H
