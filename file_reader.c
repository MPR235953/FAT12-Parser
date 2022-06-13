#include "file_reader.h"
#include <string.h>

enum error_t err;
int sector_size_global = 512;

struct disk_t* disk_open_from_file(const char* volume_file_name){
    if(!volume_file_name){
        err = EFAULT;
        return NULL;
    }
    struct disk_t *disk = (struct disk_t *)malloc(sizeof(struct disk_t));
    if(!disk){
        err = ENOMEM;
        return NULL;
    }
    disk->fp = fopen(volume_file_name, "rb");
    if(!disk->fp){
        free(disk);
        err = ENOENT;
        return NULL;
    }
    return disk;
}

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if(!pdisk || !buffer){
        err = EFAULT;
        return -1;
    }
    fseek(pdisk->fp, first_sector * sector_size_global, SEEK_SET);
    int read = (int)fread(buffer, sector_size_global, sectors_to_read, pdisk->fp);
    if(read != sectors_to_read){
        err = ERANGE;
        return -1;
    }
    return read;
}

int disk_close(struct disk_t* pdisk){
    if(!pdisk){
        err = EFAULT;
        return -1;
    }
    fclose(pdisk->fp);
    free(pdisk);
    return 0;
}

//#####################################################################################################################

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector){
    if(!pdisk){
        err = EFAULT;
        return NULL;
    }
    struct fat_super_t *super = (struct fat_super_t *)calloc(1, sizeof(struct fat_super_t));
    struct volume_t *volume = (struct volume_t *)calloc(1, sizeof(struct volume_t));
    if(!super || !volume){
        free(super);
        free(volume);
        err = ENOMEM;
        return NULL;
    }

    volume->super_sector = super;
    volume->disk_handle = pdisk;

    int check = disk_read(pdisk, first_sector, super, 1);
    if(check < 0 || super->magic != 0xAA55 || super->ext_bpb_signature != 0x28 && super->ext_bpb_signature != 0x29 ||
       !(super->logical_sectors16 == 0 ^ super->logical_sectors32 == 0) || super->logical_sectors16 == 0 && super->logical_sectors32 <= 65535 ||
       super->reserved_sectors <= 0 || super->fat_count != 1 && super->fat_count != 2 || super->sectors_per_cluster <= 0 || super->sectors_per_cluster > 128 ||
       super->root_dir_capacity * sizeof(struct root_entry) % super->bytes_per_sector != 0){
        free(super);
        free(volume);
        err = EINVAL;
        return NULL;
    }

    sector_size_global = volume->super_sector->bytes_per_sector;


    volume->fat1_start = VOLUME_START + super->reserved_sectors;
    volume->fat2_start = volume->fat1_start + super->sectors_per_fat;
    volume->root_start = volume->fat2_start + super->sectors_per_fat;
    volume->sectors_per_root = super->root_dir_capacity * sizeof(struct root_entry) / super->bytes_per_sector;
    volume->data_start = volume->root_start + volume->sectors_per_root;
    //volume->cluster_start = volume->data_start + (n_cluster - 2) * super->sectors_per_cluster;
    if(super->logical_sectors16 != 0)
        volume->available_clusters = (super->logical_sectors16 - super->reserved_sectors - 2 * super->sectors_per_fat - volume->sectors_per_root) / super->sectors_per_cluster;
    else
        volume->available_clusters = (super->logical_sectors32 - super->reserved_sectors - 2 * super->sectors_per_fat - volume->sectors_per_root) / super->sectors_per_cluster;
    volume->available_sectors = volume->available_clusters * super->sectors_per_cluster;
    volume->available_bytes = volume->available_clusters * super->sectors_per_cluster * super->bytes_per_sector;
    volume->fat_entry_count = volume->available_clusters + 2;


    uint64_t fat_size_in_bytes = super->sectors_per_fat * super->bytes_per_sector;
    uint8_t *fat_1 = (uint8_t *)calloc(fat_size_in_bytes, sizeof(uint8_t));
    uint8_t *fat_2 = (uint8_t *)calloc(fat_size_in_bytes, sizeof(uint8_t));
    if(!fat_1 || !fat_2){
        free(fat_2);
        free(fat_1);
        free(super);
        free(volume);
        err = EINVAL;
        return NULL;
    }

    int res1 = disk_read(volume->disk_handle, volume->fat1_start, fat_1, super->sectors_per_fat);
    int res2 = disk_read(volume->disk_handle, volume->fat2_start, fat_2, super->sectors_per_fat);
    if(res1 == -1 || res2 == -1 || memcmp(fat_1, fat_2, fat_size_in_bytes) != 0){
        free(fat_2);
        free(fat_1);
        free(super);
        free(volume);
        err = EINVAL;
        return NULL;
    }

    volume->fat = fat_1;
    free(fat_2);

    return volume;
}

int fat_close(struct volume_t* pvolume){
    if(!pvolume){
        err = EFAULT;
        return -1;
    }
    free(pvolume->super_sector);
    free(pvolume->fat);
    free(pvolume);
    return 0;
}

//#####################################################################################################################

struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster){
    if(!buffer || size == 0 || first_cluster == 0) return NULL;
    struct clusters_chain_t *chain = (struct clusters_chain_t *)calloc(1, sizeof(struct clusters_chain_t));
    if(!chain) return NULL;
    //chain->clusters = NULL;

    uint8_t part1, part2;
    uint16_t jump = first_cluster;
    int cluster_count = 0;
    do{
        uint16_t *tmp = (uint16_t *)realloc(chain->clusters, sizeof(uint16_t) * (cluster_count + 1));
        if(!tmp || jump > size){
            if(chain->clusters != NULL)
                free(chain->clusters);
            free(chain);
            return NULL;
        }
        chain->clusters = tmp;

        *(chain->clusters + cluster_count) = jump;
        part1 = *((uint8_t *)buffer + (uint16_t)(jump + jump / 2));     //multiply jump by 1.5
        part2 = *((uint8_t *)buffer + (uint16_t)(jump + jump / 2) + 1);
        if(jump % 2 == 0) {
            part2 = part2 & 15;     // lub part2 = (part2 << 4) >> 4;
            jump = part2 << 8;
            jump |= part1;
        }
        else {
            part1 >>= 4;
            jump = part2 << 4;
            jump |= part1;
        }
        cluster_count++;

    }while(jump < 0xFF8);

    chain->size = cluster_count;
    return chain;
}



struct file_t* file_open(struct volume_t* pvolume, const char* file_name){
    if(!pvolume){
        err = EFAULT;
        return NULL;
    }
    if(!file_name){
        err = ENOENT;
        return NULL;
    }

    struct file_t *file = (struct file_t *)calloc(1, sizeof(struct file_t));
    uint8_t *whole_root = (uint8_t *)calloc(pvolume->sectors_per_root * pvolume->super_sector->bytes_per_sector, sizeof(uint8_t));
    struct root_entry *entry = (struct root_entry *)calloc(1, sizeof(struct root_entry));
    if(!file || !whole_root || !entry){
        free(entry);
        free(whole_root);
        free(file);
        err = ENOMEM;
        return NULL;
    }

    int res = disk_read(pvolume->disk_handle, pvolume->root_start, whole_root, pvolume->sectors_per_root);
    if(res == -1){
        free(entry);
        free(whole_root);
        free(file);
        err = EINVAL;
        return NULL;
    }

    char name_from_root[13];
    int j = 0, not_found = 0;
    do {
        memset(name_from_root, '\0', 13);
        memcpy(entry, ((struct root_entry *)whole_root + j), sizeof(struct root_entry));
        //entry = ((struct root_entry *)whole_root + j);
        if(*entry->file_name == 0xe5){
            j++;
            continue;
        }
        else if(*entry->file_name == 0x00){
            not_found = 1;
            break;
        }
        int i;
        for (i = 0; i < 8 && *(entry->file_name + i) != ' '; i++)
            *(name_from_root + i) = *(entry->file_name + i);
        if (!((entry->file_attribute >> 3) & 1) && !((entry->file_attribute >> 4) & 1)) {
            if(memcmp(entry->file_name + 8, "   ", 3) != 0)
                *(name_from_root + i) = '.';
            i++;
            //memcpy(name_from_root + i + 1, entry->file_name + 8, 3 * sizeof(char));
            for(int k = 8; k < 11 && *(entry->file_name + k) != ' '; k++, i++)
                *(name_from_root + i) = *(entry->file_name + k);
        }
        j++;
    }while(strcmp(file_name, name_from_root) != 0 && j < (int)pvolume->super_sector->root_dir_capacity);

    if(not_found){
        free(entry);
        free(whole_root);
        free(file);
        err = ENOENT;
        return NULL;
    }
    else if (((entry->file_attribute >> 3) & 1) || ((entry->file_attribute >> 4) & 1)){
        free(entry);
        free(whole_root);
        free(file);
        err = EISDIR;
        return NULL;
    }

    file->entry = entry;
    uint32_t first_cluster = (entry->high_order << 16) | entry->low_order;

    file->clusters_chain =  get_chain_fat12(pvolume->fat, pvolume->super_sector->sectors_per_fat * pvolume->super_sector->bytes_per_sector, first_cluster);
    if(file->clusters_chain == NULL){
        free(entry);
        free(whole_root);
        free(file);
        err = EFAULT;
        return NULL;
    }

    file->current_position = *file->clusters_chain->clusters;
    file->moved = 0;
    file->volume = pvolume;
    free(whole_root);
    return file;
}

int file_close(struct file_t* stream){
    if(!stream){
        err = EFAULT;
        return -1;
    }
    free(stream->clusters_chain->clusters);
    free(stream->clusters_chain);
    free(stream->entry);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream){
    if(!ptr || size == 0 || nmemb == 0 || !stream){
        err = EFAULT;
        return -1;
    }
    if(stream->moved == stream->entry->file_size) return 0;     //jesli osiagnieto koniec pliku

    size_t bytes_to_read = size * nmemb;
    size_t aligned_bytes_to_read = ALIGN(bytes_to_read + stream->moved, stream->volume->super_sector->bytes_per_sector * stream->volume->super_sector->sectors_per_cluster);        //wyrownanie do rozmiaru klastra w bajtach
    if(bytes_to_read > stream->entry->file_size){       //jesli liczba bajtow do wczytania przewieksza rozmiar pliku to wczytaj tyle bajtow ile ma plik
        aligned_bytes_to_read = ALIGN(stream->entry->file_size + stream->moved, stream->volume->super_sector->bytes_per_sector * stream->volume->super_sector->sectors_per_cluster);
        bytes_to_read = stream->entry->file_size;
    }
    uint8_t *buffer = (uint8_t *)calloc(aligned_bytes_to_read, sizeof(uint8_t));        //alokacja bufora na klastry
    if(!buffer){
        err = ENOMEM;
        return -1;
    }
    int res;    //wczytywanie kolejnych klastrow do bufora
    int clusters_to_read = (int)aligned_bytes_to_read / (stream->volume->super_sector->bytes_per_sector * stream->volume->super_sector->sectors_per_cluster);
    for(int i = 0; i < clusters_to_read; i++) {
        res = disk_read(stream->volume->disk_handle, stream->volume->data_start + (*(stream->clusters_chain->clusters + i) - 2) * stream->volume->super_sector->sectors_per_cluster, buffer + i * stream->volume->super_sector->bytes_per_sector * stream->volume->super_sector->sectors_per_cluster, stream->volume->super_sector->sectors_per_cluster);
        if (res == -1) {
            free(buffer);
            err = ERANGE;
            return -1;
        }
    }

    if(stream->moved + bytes_to_read > stream->entry->file_size){       //jesli po wczytaniu zadanej wartosci przesuniecie pliku wyjdzie poza jego rozmiar
        memcpy(ptr, buffer + stream->moved, stream->entry->file_size - stream->moved);
        free(buffer);
        stream->moved += stream->entry->file_size - stream->moved;
        return (stream->entry->file_size - stream->moved) / size;
    }
    else{
        memcpy(ptr, buffer + stream->moved, bytes_to_read);
        free(buffer);
        stream->moved += bytes_to_read;
        return bytes_to_read / size;
    }
}

int32_t file_seek(struct file_t* stream, int32_t offset, int whence){
    if(!stream) {
        err = EFAULT;
        return -1;
    }

    switch(whence){
        case SEEK_SET:{
            if(offset < 0 || stream->moved + offset > (int32_t)stream->entry->file_size){
                err = ENXIO;
                return -1;
            }
            stream->moved = offset;
            break;
        }
        case SEEK_CUR:{
            if(stream->moved + offset < 0 || stream->moved + offset > (int32_t)stream->entry->file_size){
                err = ENXIO;
                return -1;
            }
            stream->moved += offset;
            break;
        }
        case SEEK_END:{
            if(offset > 0 || (int)stream->entry->file_size + offset < 0){
                err = ENXIO;
                return -1;
            }
            stream->moved = stream->entry->file_size + offset;
            break;
        }
        default:{
            err = EINVAL;
            return -1;
        }
    }
    return stream->moved;
}

//#####################################################################################################################

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path){
    if(!pvolume){
        err = EFAULT;
        return NULL;
    }
    if(!dir_path){
        err = ENOENT;
        return NULL;
    }
    if(strcmp(dir_path, "\\") != 0){
        err = ENOTDIR;
        return NULL;
    }

    struct dir_t *root_dir = (struct dir_t *)calloc(1, sizeof(struct dir_t));
    if(!root_dir){
        err = ENOMEM;
        return NULL;
    }

    root_dir->entry_number = 0;
    root_dir->volume = pvolume;
    root_dir->root_dir_data = (uint8_t *)calloc(pvolume->sectors_per_root * pvolume->super_sector->bytes_per_sector, sizeof(uint8_t));
    if(root_dir->root_dir_data == NULL){
        free(root_dir);
        err = ENOMEM;
        return NULL;
    }

    int res = disk_read(root_dir->volume->disk_handle, root_dir->volume->root_start, root_dir->root_dir_data, root_dir->volume->sectors_per_root);
    if(res == -1){
        free(root_dir->root_dir_data);
        free(root_dir);
        err = EIO;
        return NULL;
    }

    return root_dir;
}

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry){
    if(!pdir && !pentry){
        err = EFAULT;
        return -1;
    }

    if(pdir->entry_number >= pdir->volume->super_sector->root_dir_capacity){
        err = ENXIO;
        return -1;
    }


    struct root_entry en;
    memcpy(&en, (struct root_entry *)pdir->root_dir_data + pdir->entry_number, sizeof(struct root_entry));
    while(*(en.file_name) == 0xe5){
        pdir->entry_number++;
        memcpy(&en, (struct root_entry *)pdir->root_dir_data + pdir->entry_number, sizeof(struct root_entry));
    }

    if(*(en.file_name) == 0x00) return 1;

    pdir->entry_number++;

    memset(pentry, 0, sizeof(struct dir_entry_t));
    pentry->size = en.file_size;

    if(en.file_attribute & 0x20) pentry->is_archived = 1;
    if(en.file_attribute & 0x01) pentry->is_readonly = 1;
    if(en.file_attribute & 0x04) pentry->is_system = 1;
    if(en.file_attribute & 0x02) pentry->is_hidden = 1;
    if(en.file_attribute & 0x10) pentry->is_directory = 1;

    int i;
    for(i = 0; i < 8 && *(en.file_name + i) != ' '; i++)
        *(pentry->name + i) = *(en.file_name + i);
    if (!((en.file_attribute >> 3) & 1) && !((en.file_attribute >> 4) & 1)) {
        if (memcmp(en.file_name + 8, "   ", 3) != 0)
            *(pentry->name + i) = '.';
        i++;
        for (int k = 8; k < 11 && *(en.file_name + k) != ' '; k++, i++)
            *(pentry->name + i) = *(en.file_name + k);
    }

    return 0;
}

int dir_close(struct dir_t* pdir){
    if(!pdir) return -1;
    free(pdir->root_dir_data);
    free(pdir);
    return 0;
}
