//
// Created by h_d_zhang@163.com on 2021/5/21.
//

#include "memblk.h"

void memblk_make_request(struct request_queue *q, struct bio *bio) {
    unsigned long start_jif = jiffies;
    generic_start_io_acct(bio->bi_bdev->bd_disk->queue, bio_data_dir(bio), bio->bi_size >> 9, &bio->bi_bdev->bd_disk->part0);
    generic_end_io_acct(bio->bi_bdev->bd_disk->queue, bio_data_dir(bio), &bio->bi_bdev->bd_disk->part0, start_jif);
    bio_endio(bio, 0);
}



