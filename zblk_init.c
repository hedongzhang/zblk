//
// Created by h_d_zhang@163.com on 2021/3/28.
//

#include "zblk_init.h"
#include "memblk.h"
#include "linear.h"

//base
static int major = 1000;
module_param(major, int, S_IRUGO);
MODULE_PARM_DESC(major, " device major of zblk");

static int bs = 512;
module_param(bs, int, S_IRUGO);
MODULE_PARM_DESC(bs, " block size of every zblk");

static int type = 0;
module_param(type, int, S_IRUGO);
MODULE_PARM_DESC(type, " block type: memblk(0), linear(1), striped(2), mirror(3)");

static int size = 100;
module_param(size, int, S_IRUGO);
MODULE_PARM_DESC(size, " size(unit:GB) of every memblk zblk");

//memblk
static int memblk_num = 2;
module_param(memblk_num, int, S_IRUGO);
MODULE_PARM_DESC(memblk_num, " number of memblk to create");

//use_blk_mq
static int use_blk_mq = 1;
module_param(use_blk_mq, int, S_IRUGO);
MODULE_PARM_DESC(use_blk_mq, " use blk_mq(default=1)");


int zblk_minor = 0;
LIST_HEAD(zblk_list);



static const struct block_device_operations zblk_fops = {
        .owner =    THIS_MODULE,
        .open =     zblk_open,
        .release =  zblk_release,
};

int zblk_open(struct block_device *bdev, fmode_t mode) {
    return 0;
}

void zblk_release(struct gendisk *disk, fmode_t mode) {
    return;
}

void zblk_info(void) {
    struct zblk *curr_zblk;
    struct zblk_sharding *sharding;

    printk("ZBLK: ===zblk info===\n");

    list_for_each_entry(curr_zblk, &zblk_list, list) {
        printk("ZBLK:  zblk(%d) size:%llu sharding_count:%d\n", curr_zblk->minor, curr_zblk->size, curr_zblk->sharding_count);

        list_for_each_entry(sharding, &curr_zblk->sharding, list) {
            printk("ZBLK:    sharding:%d path:%s size:%llu end:%llu\n", sharding->index, sharding->path, sharding->size, sharding->end);
        }
    }
    printk("ZBLK: ===============\n");
}

//timer
void zblk_timer(unsigned long data) {
    struct zblk *curr_zblk = (struct zblk *) data;
//    zblk_info();
    mod_timer(&curr_zblk->timer, jiffies + msecs_to_jiffies(1000));
}

int zblk_init_timer(struct zblk *curr_zblk, int timeout) {
    init_timer(&curr_zblk->timer);
    curr_zblk->timer.expires = jiffies + msecs_to_jiffies(timeout);
    setup_timer(&curr_zblk->timer, zblk_timer, (unsigned long) curr_zblk);
    add_timer(&curr_zblk->timer);
    return 0;
}

int zblk_init_bdev(struct zblk *curr_zblk) {
    int ret = 0;
    struct zblk_sharding *sharding, *pre_sharding;

    int sharding_count = SHARDING_COUNT;
    const char **sharding_paths = zblk_bdev[curr_zblk->minor];

    if(curr_zblk->type == ZBLK_TYPE_MEMBLK) {
        curr_zblk->size = size << (GBYTE_SECTOR_SHIFT);
        return ret;
    } else if(curr_zblk->type == ZBLK_TYPE_LINEAR) {
        curr_zblk->sharding_count = 0;
        curr_zblk->size = 0;

        for(int i = 0; i < sharding_count; i++) {
            sharding = kzalloc(sizeof(struct zblk_sharding), GFP_KERNEL);
            if(!sharding) {
                goto err_remove_bdev;
            }
            sharding->bdev = blkdev_get_by_path(sharding_paths[i], FMODE_READ | FMODE_WRITE | FMODE_EXCL, (void*)curr_zblk);
            if (IS_ERR(sharding->bdev)) {
                printk("ZBLK: zblk(%d) open(\"%s\") failed with %ld\n", curr_zblk->minor, sharding_paths[i], PTR_ERR(sharding->bdev));
                goto err_free_sharding;
            }
            strncpy(sharding->path, sharding_paths[i], 64);
            sharding->index = curr_zblk->sharding_count;
            sharding->size = sharding->bdev->bd_part->nr_sects;
            if(curr_zblk->sharding_count == 0)
                sharding->end = sharding->size;
            else {
                pre_sharding = list_last_entry(&curr_zblk->sharding, struct zblk_sharding, list);
                sharding->end = pre_sharding->end + sharding->size;
            }

            list_add_tail(&sharding->list, &curr_zblk->sharding);
            curr_zblk->sharding_count++;
            curr_zblk->size += sharding->size;
        }
    }
    return ret;
err_free_sharding:
    kfree(sharding);
err_remove_bdev:
    if(curr_zblk->type == ZBLK_TYPE_LINEAR) {
        while (!list_empty(&curr_zblk->sharding)) {
		    sharding = list_first_entry(&curr_zblk->sharding, struct zblk_sharding, list);
		    list_del(&sharding->list);
		    blkdev_put(sharding->bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
            kfree(sharding);
	    }
    }
err:
    printk("ZBLK: init zblk(%d) bdev failed\n", curr_zblk->minor);
    return -1;
}

static int zblk_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	return -ENOTSUPP;
}

static struct blk_mq_ops zblk_mq_ops = {
	.queue_rq       = zblk_queue_rq
};

int zblk_init_tag_set(struct zblk *zblk, struct blk_mq_tag_set *set)
{
	set->ops = &zblk_mq_ops;
	set->nr_hw_queues = num_online_cpus();
	set->numa_node = NUMA_NO_NODE;
	set->queue_depth = BLKDEV_MAX_RQ;

	set->cmd_size = 0;
	set->flags = BLK_MQ_F_SHOULD_MERGE | 0 | BLK_MQ_F_BLOCKING;

	set->driver_data = zblk;

	return blk_mq_alloc_tag_set(set);
}

int zblk_init_dev(struct zblk *curr_zblk) {
    int ret = 0;
    struct gendisk *disk;
    struct request_queue *q;

    if(use_blk_mq) {
        // init queue
        if(zblk_init_tag_set(curr_zblk, &curr_zblk->tag_set))
            goto err;

		q = blk_mq_init_queue(&curr_zblk->tag_set);
        if (IS_ERR(q))
            goto err;
        blk_queue_logical_block_size(q, bs);
        blk_queue_physical_block_size(q, bs);

        q->queuedata = curr_zblk;
        if(curr_zblk->type == ZBLK_TYPE_MEMBLK)
            blk_queue_make_request(q, memblk_make_request);
        else if(curr_zblk->type == ZBLK_TYPE_LINEAR)
            blk_queue_make_request(q, linear_make_request);
        else {
            printk("ZBLK: invalid zblk_type:%d\n", curr_zblk->type);
            goto err_cleanup_queue;
        }
        curr_zblk->q = q;

        // init disk
        disk = alloc_disk(1);
        if (!disk) {
            printk("ZBLK: alloc_disk failed\n");
            goto err_cleanup_queue;
        }
        sprintf(disk->disk_name, "zblk%d", curr_zblk->minor);

        set_capacity(disk, curr_zblk->size);
        disk->major = major;
        disk->first_minor = curr_zblk->minor;
        disk->fops = &zblk_fops;
        disk->queue = curr_zblk->q;
        disk->private_data = curr_zblk;
        curr_zblk->disk = disk;
        add_disk(disk);

    } else {
        // init queue
        q = blk_alloc_queue(GFP_KERNEL);
        if (!q)
            goto err;
        blk_queue_logical_block_size(q, bs);
        blk_queue_physical_block_size(q, bs);

        q->queuedata = curr_zblk;
        if(curr_zblk->type == ZBLK_TYPE_MEMBLK)
            blk_queue_make_request(q, memblk_make_request);
        else if(curr_zblk->type == ZBLK_TYPE_LINEAR)
            blk_queue_make_request(q, linear_make_request);
        else {
            printk("ZBLK: invalid zblk_type:%d\n", curr_zblk->type);
            goto err_cleanup_queue;
        }
        curr_zblk->q = q;

        // init disk
        disk = alloc_disk(1);
        if (!disk) {
            printk("ZBLK: alloc_disk failed\n");
            goto err_cleanup_queue;
        }
        sprintf(disk->disk_name, "zblk%d", curr_zblk->minor);

        set_capacity(disk, curr_zblk->size);
        disk->major = major;
        disk->first_minor = curr_zblk->minor;
        disk->fops = &zblk_fops;
        disk->queue = curr_zblk->q;
        disk->private_data = curr_zblk;
        add_disk(disk);
        curr_zblk->disk = disk;
    }

    return ret;
err_cleanup_queue:
    blk_cleanup_queue(q);
err:
    printk("ZBLK: init zblk(%d) dev failed\n", curr_zblk->minor);
    return -1;
}

int zblk_remove_bdev(struct zblk *curr_zblk) {
    struct zblk_sharding *sharding;

    //free zblk sharding
    if(curr_zblk->type == ZBLK_TYPE_LINEAR) {
        while (!list_empty(&curr_zblk->sharding)) {
		    sharding = list_first_entry(&curr_zblk->sharding, struct zblk_sharding, list);
		    list_del(&sharding->list);
		    blkdev_put(sharding->bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
            kfree(sharding);
	    }
    }
    return 0;
}

int zblk_remove_dev(struct zblk *curr_zblk) {
    blk_cleanup_queue(curr_zblk->q);
    del_gendisk(curr_zblk->disk);
    put_disk(curr_zblk->disk);
    return 0;
}

int zblk_create(enum zblk_type type) {
    int ret = 0;
    struct zblk *curr_zblk;

    curr_zblk = kzalloc(sizeof(struct zblk), GFP_KERNEL);
    if (!curr_zblk)
        goto err;
    curr_zblk->minor = zblk_minor++;
    curr_zblk->type = type;
    curr_zblk->stat = ZBLK_STAT_CREATING;
    INIT_LIST_HEAD(&curr_zblk->sharding);
    curr_zblk->sharding_count = 0;

    zblk_init_timer(curr_zblk, 1000);

    //init backing device
    ret = zblk_init_bdev(curr_zblk);
    if (ret < 0)
        goto err_del_timer;

    //init device
    ret = zblk_init_dev(curr_zblk);
    if (ret < 0)
        goto err_remove_bdev;

    list_add_tail(&curr_zblk->list, &zblk_list);
    printk("ZBLK: create zblk(%d) success\n", curr_zblk->minor);
    return ret;
err_remove_bdev:
    zblk_remove_bdev(curr_zblk);
err_del_timer:
    del_timer(&curr_zblk->timer);
    kfree(curr_zblk);
err:
    printk("ZBLK: create zblk failed\n");
    return -1;
}

int zblk_remove(struct zblk *curr_zblk) {
    curr_zblk->stat = ZBLK_STAT_REMOVING;

    zblk_remove_dev(curr_zblk);
    zblk_remove_bdev(curr_zblk);

    del_timer(&curr_zblk->timer);
    list_del_init(&curr_zblk->list);
    printk("ZBLK: remove zblk(%d)\n", curr_zblk->minor);
    kfree(curr_zblk);
    return 0;
}


static int __init zblk_init(void) {
    int ret;

    ret = register_blkdev(major, "zblk");
    if (ret < 0)
        return ret;
    printk("ZBLK: regist zblk(major:%d) driver\n", major);

    if(type == ZBLK_TYPE_MEMBLK)
        zblk_create(type);
    else {
        for(int i = 0; i < ZBLK_COUNT; i++)
            zblk_create(type);
    }

    zblk_info();
    return 0;
}

static void __exit zblk_exit(void) {
    struct zblk *curr_zblk;

    unregister_blkdev(major, "zblk");
    printk("ZBLK: unregister zblk(major:%d) driver\n", major);

    while (!list_empty(&zblk_list)) {
        curr_zblk = list_entry(zblk_list.next, struct zblk, list);
        zblk_remove(curr_zblk);
    }
}


module_init(zblk_init);
module_exit(zblk_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("h_d_zhang@163.com");
MODULE_VERSION("1.0");
