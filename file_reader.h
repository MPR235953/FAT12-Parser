#ifndef ZAD2_3_FILE_READER_H
#define ZAD2_3_FILE_READER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define ALIGN(x,al) (((x) + ((al) - 1)) & ~((al) - 1))

#define VOLUME_START 0

enum error_t{
    EFAULT = 14,
    ENOENT = 2,
    ENOMEM = 12,
    ERANGE = 34,
    EINVAL = 22,
    EISDIR = 21,
    ENXIO = 6,
    ENOTDIR = 20,
    EIO = 5
};

extern enum error_t err;

struct fat_super_t{
    uint8_t jump_code[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_dir_capacity;
    uint16_t logical_sectors16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t chs_sectors_pre_track;
    uint16_t chs_tracks_per_cylinder;
    uint32_t hidden_sectors;
    uint32_t logical_sectors32;
    uint8_t media_id;
    uint8_t chs_head;
    uint8_t ext_bpb_signature;
    uint32_t serial_number;
    char volume_label[11];
    char fsid[8];
    uint8_t boot_code[448];
    uint16_t magic;
}__attribute__((packed));

struct root_entry{
    uint8_t file_name[11];
    uint8_t file_attribute;
    uint8_t reserved;
    uint8_t file_creation_time;
    uint16_t creation_time; //hours,minutes,seconds
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t high_order;    //part 1 of first cluster
    uint16_t modified_time; //hours,minutes,seconds
    uint16_t modified_date;
    uint16_t low_order;     //part 2 of first cluster
    uint32_t file_size;
}__attribute__((packed));

struct  dir_entry_t {
    char name[13];
    uint32_t size;
    uint8_t is_archived: 1;
    uint8_t is_readonly: 1;
    uint8_t is_system: 1;
    uint8_t is_hidden: 1;
    uint8_t is_directory: 1;
}__attribute__((packed));




struct disk_t{
    FILE *fp;
}__attribute__((packed));


struct volume_t{
    struct disk_t *disk_handle;
    struct fat_super_t *super_sector;
    uint8_t *fat;
    uint64_t fat1_start;                //volume_start + reserved_sectors
    uint64_t fat2_start;                //fat1_start + sectors_per_fat
    uint64_t root_start;                //fat1_start + 2 * sectors_per_fat
    uint64_t sectors_per_root;          //root_dir_capacity * sizeof(dir_entry) / bytes_per_sector
    uint64_t data_start;                //root_start + sectors_per_root
    uint64_t cluster_start;             //data_start + (n_cluster - 2) * sectors_per_cluster
    uint64_t available_clusters;        //(logical_sectors16/32 - reserved_sectors - 2 * sectors_per_fat - sectors_per_root) / sectors_per_cluster
    uint64_t available_sectors;         //available_clusters * sectors_per_cluster
    uint64_t available_bytes;           //available_clusters * sectors_per_cluster * bytes_per_sector
    uint64_t fat_entry_count;           //available_clusters + 2

}__attribute__((packed));


struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};

struct file_t{
    struct clusters_chain_t *clusters_chain;
    uint16_t current_position;
    uint16_t moved;
    struct root_entry *entry;
    struct volume_t *volume;
};


struct dir_t{
    struct volume_t *volume;
    uint8_t *root_dir_data;
    int entry_number;
};



struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);


struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);


struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster);
struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);


struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

#endif //ZAD2_3_FILE_READER_H
