#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SECTOR_SIZE 512
#define TOTAL_SECTORS 32
#define RESERVED_SECTORS 1
#define FAT_COUNT 1
#define ROOT_ENTRIES 16
#define SECTORS_PER_CLUSTER 1
#define FAT_SECTORS 1
#define ROOT_DIR_SECTORS 1
#define DATA_START (RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS + ROOT_DIR_SECTORS)

static uint8_t image[TOTAL_SECTORS * SECTOR_SIZE];

static void write_boot_sector(void)
{
    uint8_t *b = image;
    memset(b, 0, SECTOR_SIZE);
    b[0] = 0xEB;
    b[1] = 0x3C;
    b[2] = 0x90;
    memcpy(b + 3, "PROOS   ", 8);
    b[11] = 0x00;
    b[12] = 0x02; /* 512 bytes per sector */
    b[13] = SECTORS_PER_CLUSTER;
    b[14] = RESERVED_SECTORS & 0xFF;
    b[15] = RESERVED_SECTORS >> 8;
    b[16] = FAT_COUNT;
    b[17] = ROOT_ENTRIES & 0xFF;
    b[18] = ROOT_ENTRIES >> 8;
    b[19] = TOTAL_SECTORS & 0xFF;
    b[20] = TOTAL_SECTORS >> 8;
    b[21] = 0xF8;
    b[22] = FAT_SECTORS & 0xFF;
    b[23] = FAT_SECTORS >> 8;
    b[24] = 0x01;
    b[26] = 0x01;
    b[28] = 0x00;
    b[29] = 0x00;
    b[36] = 'F';
    b[37] = 'A';
    b[38] = 'T';
    b[39] = '1';
    b[40] = '6';
    b[41] = ' ';
    b[42] = ' ';
    b[43] = ' ';
    b[44] = ' ';
    b[510] = 0x55;
    b[511] = 0xAA;
}

static void write_fat(void)
{
    uint8_t *fat = image + RESERVED_SECTORS * SECTOR_SIZE;
    memset(fat, 0, FAT_SECTORS * SECTOR_SIZE);
    fat[0] = 0xF8;
    fat[1] = 0xFF;
    fat[2] = 0xFF;
    fat[3] = 0xFF;
}

static void write_root_directory(void)
{
    uint8_t *root = image + (RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS) * SECTOR_SIZE;
    memset(root, 0, ROOT_DIR_SECTORS * SECTOR_SIZE);

    const char name[11] = {'R','E','A','D','M','E',' ',' ','T','X','T'};
    memcpy(root, name, 11);
    root[11] = 0x20;
    root[26] = 0x02;
    root[27] = 0x00;
    const char content[] = "Hello from proOS FAT16!\n";
    uint32_t size = (uint32_t)strlen(content);
    root[28] = size & 0xFF;
    root[29] = (size >> 8) & 0xFF;
    root[30] = (size >> 16) & 0xFF;
    root[31] = (size >> 24) & 0xFF;
}

static void write_file_data(void)
{
    uint8_t *data = image + DATA_START * SECTOR_SIZE;
    memset(data, 0, (TOTAL_SECTORS - DATA_START) * SECTOR_SIZE);
    const char content[] = "Hello from proOS FAT16!\n";
    memcpy(data, content, strlen(content));
}

static void build_image(void)
{
    memset(image, 0, sizeof(image));
    write_boot_sector();
    write_fat();
    write_root_directory();
    write_file_data();
}

#ifndef FAT16_IMAGE_STANDALONE
size_t fat16_image_generate(uint8_t *buffer, size_t max_len)
{
    if (!buffer || max_len < sizeof(image))
        return 0;
    build_image();
    memcpy(buffer, image, sizeof(image));
    return sizeof(image);
}
#else
int main(int argc, char **argv)
{
    build_image();
    const char *path = (argc > 1) ? argv[1] : NULL;

    if (path)
    {
        FILE *f = fopen(path, "wb");
        if (!f)
            return 1;
        fwrite(image, 1, sizeof(image), f);
        fclose(f);
    }
    else
    {
        fwrite(image, 1, sizeof(image), stdout);
    }
    return 0;
}
#endif
