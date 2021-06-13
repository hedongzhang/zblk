//
// Created by h_d_zhang@163.com on 2021/3/28.
//

#ifndef ZBLK_ZBLK_INIT_H
#define ZBLK_ZBLK_INIT_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/delay.h>

#define SECTOR_SHIFT 9
#define GBYTE_BYTE_SHIFT 30
#define GBYTE_SECTOR_SHIFT GBYTE_BYTE_SHIFT - SECTOR_SHIFT

enum zblk_stat {
    ZBLK_STAT_CREATING,
    ZBLK_STAT_RUNNING,
    ZBLK_STAT_SUSPEND,
    ZBLK_STAT_REMOVING
};

enum zblk_type {
    ZBLK_TYPE_MEMBLK = 0,
    ZBLK_TYPE_LINEAR,
    ZBLK_TYPE_STRIPED,
    ZBLK_TYPE_MIRROR
};

struct zblk_sharding {
    int index;
    sector_t size;
    //end sector of zblk + 1
    sector_t end;

    char path[64];
    struct block_device *bdev;

    struct list_head list;
};

struct zblk {
    int minor;
    sector_t size;

    struct request_queue *q;
    struct gendisk *disk;

    enum zblk_stat stat;
    enum zblk_type type;

    struct timer_list timer;

    struct list_head list;

    int sharding_count;
    struct list_head sharding;
};

int zblk_open(struct block_device *bdev, fmode_t mode);
void zblk_release(struct gendisk *disk, fmode_t mode);

//void zblk_info();
void zblk_info(void);


#define ZBLK_COUNT 2
#define SHARDING_COUNT 2
static const char *zblk_bdev[ZBLK_COUNT][SHARDING_COUNT] =
{
    {
        "/dev/sda1",
        "/dev/sda2",
    },
    {
        "/dev/sda3",
        "/dev/sda4",
    }
};

#endif //ZBLK_ZBLK_INIT_H
