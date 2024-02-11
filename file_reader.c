//
// Created by Krystian on 25.11.23.
//

#include "file_reader.h"
#include "tested_declarations.h"
#include "rdebug.h"

////////////////////////////////////////////////////////////////////////DISK

struct disk_t *disk_open_from_file(const char *volume_file_name) {
	if (volume_file_name == NULL) {
		errno = EFAULT;
		return NULL;
	}
	FILE *file = fopen(volume_file_name, "rb");
	if (file == NULL) {
		errno = ENOENT;
		return NULL;
	}
	struct disk_t *disk = (struct disk_t *) calloc(1, sizeof(struct disk_t));
	if (disk == NULL) {
		errno = ENOMEM;
		fclose(file);
		return NULL;
	}
	disk->pFile = file;
	fseek(file, 0, SEEK_END);
	disk->numberOfSectors = ftell(file) / SECTOR_SIZE;
	if (disk->numberOfSectors > MAX_NUM_OF_SECTORS_IN_FAT16) {
		free(disk);
		fclose(file);
		errno = EINVAL;
		return NULL;
	}
	return disk;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {
	if (pdisk == NULL || buffer == NULL || sectors_to_read < 0) {
		errno = EFAULT;
		return -1;
	}
	if (first_sector < 0 || (uint32_t) (first_sector + sectors_to_read) > pdisk->numberOfSectors) {
		errno = ERANGE;
		return -1;
	}
	fseek(pdisk->pFile, first_sector * SECTOR_SIZE, SEEK_SET);
	fread(buffer, SECTOR_SIZE, sectors_to_read, pdisk->pFile);
	return 0;
}

int disk_close(struct disk_t *pdisk) {
	if (pdisk == NULL) {
		errno = EFAULT;
		return -1;
	}
	fclose(pdisk->pFile);
	free(pdisk);
	return 0;
}
///////////////////////////////////////////////////////////////////////////VOLUME

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {
	if (pdisk == NULL || (int32_t) first_sector < 0) {
		errno = EFAULT;
		return NULL;
	}

	struct volume_t *volume = (struct volume_t *) calloc(1, sizeof(struct volume_t));
	if (volume == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	disk_read(pdisk, (int32_t) first_sector, &volume->bootSector, 1);

	if (volume->bootSector.SignatureValue != SIGNATURE_VALUE) {
		free(volume);
		errno = EINVAL;
		return NULL;
	}

	if (volume->bootSector.MaxNumOfFiles * sizeof(struct SFN_t) % volume->bootSector.BytesPerSector != 0) {
		free(volume);
		errno = EINVAL;
		return NULL;
	}

	volume->disk = pdisk;

	volume->FAT1 = (void *) calloc(volume->bootSector.FatSize, volume->bootSector.BytesPerSector);
	if (volume->FAT1 == NULL) {
		free(volume);
		errno = ENOMEM;
		return NULL;
	}

	volume->FAT2 = (void *) calloc(volume->bootSector.FatSize, volume->bootSector.BytesPerSector);
	if (volume->FAT2 == NULL) {
		free(volume->FAT1);
		free(volume);
		errno = ENOMEM;
		return NULL;
	}

	disk_read(pdisk, (int32_t) first_sector + volume->bootSector.SizeReservedArea, volume->FAT1, volume->bootSector.FatSize);

	disk_read(pdisk, (int32_t) first_sector + volume->bootSector.SizeReservedArea + volume->bootSector.FatSize, volume->FAT2,
	          volume->bootSector.FatSize);

	if (memcmp(volume->FAT1, volume->FAT2, volume->bootSector.FatSize * SECTOR_SIZE) != 0) {
		free(volume->FAT1);
		free(volume->FAT2);
		free(volume->rootDirectory);
		free(volume);
		errno = EINVAL;
		return NULL;
	}

	volume->rootDirectory = (void *) malloc(volume->bootSector.MaxNumOfFiles * sizeof(struct dir_entry_t));
	if (volume->rootDirectory == NULL) {
		free(volume->FAT1);
		free(volume->FAT2);
		free(volume);
		errno = ENOMEM;
		return NULL;
	}

	disk_read(pdisk, volume->bootSector.SizeReservedArea + volume->bootSector.FatSize * 2,
	          volume->rootDirectory, (int) sizeof(struct SFN_t) * volume->bootSector.MaxNumOfFiles / volume->bootSector.BytesPerSector);

	return volume;
}

int fat_close(struct volume_t *pvolume) {
	if (pvolume == NULL) {
		errno = EFAULT;
		return -1;
	}
	free(pvolume->FAT1);
	free(pvolume->FAT2);
	free(pvolume->rootDirectory);
	free(pvolume);
	return 0;
}

////////////////////////////////////////////////////////////////////////////FILE_READER

void fixFileName(const char *fileName, char fixedFileName[FILE_NAME_LENGTH]) {
	*(fixedFileName + NOT_DIR_FILE_LENGTH) = '\0';
	if (strchr(fileName, '.') == NULL) {
		strcpy(fixedFileName, fileName);
		for (size_t i = strlen(fileName); i < FILE_NAME_LENGTH; ++i) {
			*(fixedFileName + i) = ' ';
		}
		return;
	}
	int dotOffset = 0;
	for (int i = 0; i < END_OF_FULL_FILE_NAME; i++) {
		if (*(fileName + i) == '.') {
			dotOffset = i;
			*(fixedFileName + i) = ' ';
			i++;
			for (int j = DOT_OFFSET; j < END_OF_FULL_FILE_NAME; j++) {
				if (*(fileName + i) == '\0') {
					*(fixedFileName + j) = ' ';
					break;
				}
				*(fixedFileName + j) = *(fileName + i);
				i++;
			}
			break;
		}
		*(fixedFileName + i) = *(fileName + i);
	}
	for (int j = dotOffset; j < DOT_OFFSET; ++j) {
		*(fixedFileName + j) = ' ';
	}
}

bool checkIfFileExist(struct SFN_t *file, char *changedFileName) {
	if (strncmp(file->filename, changedFileName, FILE_NAME_LENGTH) == 0) {
		if ((file->fileAttribute & (1 << IS_DIRECTORY)) == DIR_ATTR_VALUE) {
			errno = EISDIR;
			return false;
		}
		return true;
	}
	return false;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
	if (pvolume == NULL || file_name == NULL) {
		errno = EFAULT;
		return NULL;
	}
	struct SFN_t *rootDirectory = pvolume->rootDirectory;

	struct file_t *file = malloc(sizeof(struct file_t));
	if (file == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	bool found = false;
	char changedFileName[12] = "";
	fixFileName(file_name, changedFileName);
	for (unsigned i = 0; i < pvolume->bootSector.MaxNumOfFiles; i++) {
		if (checkIfFileExist(rootDirectory, changedFileName)) {
			file->file_info = *rootDirectory;
			found = true;
			break;
		}
		rootDirectory++;
	}
	if (found == false) {
		errno = ENOENT;
		free(file);
		return NULL;
	}
	file->offset = 0;
	file->volume = pvolume;

	file->chain = get_chain_fat16(pvolume->FAT1, pvolume->bootSector.FatSize * pvolume->bootSector.BytesPerSector,
	                              file->file_info.firstClusterNumberLowBits);
	if (file->chain == NULL) {
		free(file);
		errno = ENOMEM;
		return NULL;
	}

	file->chain->clusterOffset = 0;
	size_t clusterSize = file->volume->bootSector.BytesPerSector * file->volume->bootSector.SectorPerCluster;
	file->chain->size = file->file_info.fileSize / (clusterSize);

	file->chain->clusterBuffer = calloc(1, (clusterSize) * sizeof(char));
	if (file->chain->clusterBuffer == NULL) {
		free(file);
		errno = ENOMEM;
		return NULL;
	}

	return file;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
	if (ptr == NULL || stream == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (stream->offset >= stream->file_info.fileSize) {
		if (stream->offset > stream->file_info.fileSize) {
			errno = ENXIO;
			return -1;
		}
		return 0;
	}

	char *buffer = ptr;
	size_t bytesRead = 0;
	size_t expectedBytes = size * nmemb;
	size_t clusterSize = stream->volume->bootSector.BytesPerSector * stream->volume->bootSector.SectorPerCluster;
	size_t sectorSize = stream->volume->bootSector.BytesPerSector;
	size_t sectorsToRead = clusterSize / sectorSize;

	int clusterStartPosition = (int) (stream->volume->bootSector.SizeReservedArea + stream->volume->bootSector.FatSize * stream->volume->bootSector.NumFATs +
	                                  (sizeof(struct SFN_t) * stream->volume->bootSector.MaxNumOfFiles) / sectorSize);

	while (stream->offset <= stream->file_info.fileSize) {
		if (stream->offset >= stream->file_info.fileSize || bytesRead >= size * nmemb) {
			break;
		}

		int clusterNumber = (int) (stream->offset / clusterSize);
		int sectorToRead = (int) (clusterStartPosition + (stream->chain->clusters[clusterNumber] - FIRST_CLUSTER_OFFSET) * sectorsToRead);
		if (disk_read(stream->volume->disk, sectorToRead, stream->chain->clusterBuffer, (int) sectorsToRead) == -1) {
			errno = ERANGE;
			return -1;
		}

		stream->chain->clusterOffset = stream->offset % clusterSize;
		if (stream->chain->clusterOffset == clusterSize) {
			stream->chain->clusterOffset = 0;
		}

		while ((stream->chain->clusterOffset < clusterSize) && (bytesRead < expectedBytes) && (stream->offset < stream->file_info.fileSize)) {
			buffer[bytesRead] = stream->chain->clusterBuffer[stream->chain->clusterOffset];
			stream->chain->clusterOffset++;
			stream->offset++;
			bytesRead++;
		}
	}
	return bytesRead / size;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {
	if (stream == NULL) {
		errno = EFAULT;
		return -1;
	}
	switch (whence) {
		case SEEK_SET:
			if (offset < 0 || offset > (int32_t) stream->file_info.fileSize) {
				errno = ENXIO;
				return -1;
			}
			stream->offset = offset;
			break;
		case SEEK_CUR:
			if ((int32_t) stream->offset + offset < 0 || stream->offset + offset > stream->file_info.fileSize) {
				errno = ENXIO;
				return -1;
			}
			stream->offset += offset;
			break;
		case SEEK_END:
			if (offset > 0 || offset < -(int32_t) (stream->file_info.fileSize)) {
				errno = ENXIO;
				return -1;
			}
			stream->offset = stream->file_info.fileSize + offset;
			break;
		default:
			errno = EINVAL;
			return -1;
	}
	return 0;
}

int file_close(struct file_t *stream) {
	if (stream == NULL) {
		errno = EFAULT;
		return -1;
	}
	free(stream->chain->clusters);
	free(stream->chain->clusterBuffer);
	free(stream->chain);
	free(stream);
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////DIRECTORY_READER

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
	if (pvolume == NULL || dir_path == NULL) {
		errno = EFAULT;
		return NULL;
	}

	if (strcmp("\\", dir_path) != 0) {
		errno = ENOENT;
		if (*dir_path != '\\') {
			errno = ENOTDIR;
		}
		return NULL;
	}

	struct dir_t *directory = calloc(1, sizeof(struct dir_t));
	if (directory == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	directory->data = pvolume->rootDirectory;
	directory->size = pvolume->bootSector.MaxNumOfFiles;
	directory->offset = 0;
	directory->readDirs = 0;
	return directory;
}

bool checkForExtension(const char *dirName) {
	if (*(dirName + DOT_OFFSET) == ' ') {
		return false;
	}
	return true;
}

void fixDirName(char *dirName, char fixedDirName[13], int isDirectory) {
	if (dirName == NULL || fixedDirName == NULL) {
		printf("dirName or fixedDirName is NULL\n");
		return;
	}
	switch (isDirectory) {
		case IS_NOT_DIR:
			if (checkForExtension(dirName) == false) {
				for (int i = 0 ;*dirName != ' ' && *dirName != '\0' && i < NOT_DIR_FILE_LENGTH;i++) {
					*fixedDirName = *dirName;
					dirName++;
					fixedDirName++;
				}
				*fixedDirName = '\0';
				return;
			}
			if (strchr(dirName, ' ') == NULL) {
				strcpy(fixedDirName, dirName);
				memmove(fixedDirName + DOT_OFFSET, dirName + FILE_LENGTH_TO_DOT, EXTENSION_LENGTH_WITH_DOT);
				*(fixedDirName + DOT_OFFSET) = '.';
				*(fixedDirName + END_OF_FULL_FILE_NAME) = '\0';
				return;
			}
			for (int i = 0 ;*dirName != ' ' &&  i < NOT_DIR_FILE_LENGTH;i++) {
				*fixedDirName = *dirName;
				dirName++;
				fixedDirName++;
			}
			*fixedDirName = '.';
			fixedDirName++;
			dirName++;
			while (*dirName == ' ') {
				dirName++;
			}
			for (int i = 0 ;*dirName != ' ' &&  i < EXTENSION_LENGTH;i++) {
				*fixedDirName = *dirName;
				dirName++;
				fixedDirName++;
			}
			*(fixedDirName) = '\0';
			break;
		case DIR_ATTR_VALUE:
			while (*dirName != ' ' && *dirName != '\0') {
				*fixedDirName = *dirName;
				dirName++;
				fixedDirName++;
			}
			*fixedDirName = '\0';
			break;
		default:
			printf("Bad atrribute input\n");
			break;
	}
}

void addOffsetAndChangeDirAttr(struct dir_t *pdir) {
	if (pdir == NULL) {
		return;
	}
	pdir->offset++;
	if (pdir->offset == pdir->size && pdir->readDirs == 0) {
		pdir->offset = 0;
		pdir->readDirs = 1;
	}
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
	if (pdir == NULL || pentry == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (pdir->offset == pdir->size) {
		errno = ENXIO;
		return -1;
	}
	struct SFN_t *directory = pdir->data;
	bool found = false;
	while (pdir->offset < pdir->size) {

		if ((directory[pdir->offset].filename[0] == LAST_ENTRY) ||
		    (directory[pdir->offset].filename[0] == FILE_DELETED) ||
		    (directory[pdir->offset].fileAttribute & (1 << IS_VOLUME_LABEL)) == VOLUME_LABEL_ATTR_VALUE ||      //is volume label
		    (directory[pdir->offset].fileSize != 0 && pdir->readDirs == 0) ||                                  //is File
		    (directory[pdir->offset].fileSize == 0 && pdir->readDirs == 1)) {                                 //is Directory
			addOffsetAndChangeDirAttr(pdir);
			continue;
		}
		found = true;
		addOffsetAndChangeDirAttr(pdir);
		break;
	}

	if (found == false) {
		errno = EIO;
		return 1;
	}

	directory += pdir->offset - 1;

	char fixedDirName[13] = "";
	fixDirName(directory->filename, fixedDirName, directory->fileAttribute & (1 << IS_DIRECTORY));
	strcpy(pentry->name, fixedDirName);
	pentry->size = directory->fileSize;

	(directory->fileAttribute & (1 << READ_ONLY)) ? pentry->is_readonly = 1 : (pentry->is_readonly = 0);
	(directory->fileAttribute & (1 << IS_HIDDEN)) ? pentry->is_hidden = 1 : (pentry->is_hidden = 0);
	(directory->fileAttribute & (1 << IS_SYSTEM)) ? pentry->is_system = 1 : (pentry->is_system = 0);
	//missing is_volume_labelS
	(directory->fileAttribute & (1 << IS_DIRECTORY)) ? pentry->is_directory = 1 : (pentry->is_directory = 0);
	(directory->fileAttribute & (1 << IS_ARCHIVED)) ? pentry->is_archived = 1 : (pentry->is_archived = 0);
	return 0;
}

int dir_close(struct dir_t *pdir) {
	if (pdir == NULL) {
		errno = EFAULT;
		return -1;
	}
	free(pdir);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////CLUSTERS_CHAIN

struct clusters_chain_t *get_chain_fat16(const void *const buffer, size_t size, uint16_t first_cluster) {
	if (size == 0 || buffer == NULL || first_cluster == 0) {
		return NULL;
	}
	const uint16_t *buffer_bytes = buffer;
	struct clusters_chain_t *chain = malloc(sizeof(struct clusters_chain_t));
	if (chain == NULL) {
		return NULL;
	}
	chain->clusters = malloc(sizeof(uint16_t));
	if (chain->clusters == NULL) {
		free(chain);
		return NULL;
	}
	chain->size = 1;
	chain->clusters[0] = first_cluster;
	uint16_t current_cluster = *(buffer_bytes + first_cluster);
	unsigned int first_fat_sector = (first_cluster * 2) / size;
	while (current_cluster < 0xFFF8) {
		uint8_t *FAT_table = malloc(sizeof(uint8_t) * size);
		if (FAT_table == NULL) {
			free(chain->clusters);
			free(chain);
			return NULL;
		}
		uint16_t *tmp = realloc(chain->clusters, sizeof(uint16_t) * (chain->size + 2));
		if (tmp == NULL) {
			free(chain->clusters);
			free(chain);
			return NULL;
		}
		chain->clusters = tmp;
		unsigned int fat_offset = current_cluster * 2;
		unsigned int fat_sector = first_fat_sector + (fat_offset / size);
		unsigned int ent_offset = fat_offset % size;
		memcpy(FAT_table, buffer_bytes + fat_sector, size);
		chain->clusters[chain->size] = current_cluster;
		unsigned short table_value = *(unsigned short *) (&FAT_table[ent_offset]);
		current_cluster = table_value;
		chain->size++;
		free(FAT_table);
	}
	return chain;
}


