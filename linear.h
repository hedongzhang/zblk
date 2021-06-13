//
// Created by h_d_zhang@163.com on 2021/5/21.
//

#ifndef ZBLK_LINEAR_H
#define ZBLK_LINEAR_H

#include <linux/blkdev.h>
#include <linux/fs.h>

#include "zblk_init.h"

struct linear_bio_pair {
    struct bio *bio_original;

	struct bio *bio_p1;
	struct bio *bio_p2;
	atomic_t cnt;
	int error;
};

struct linear_request {
    struct zblk *blk;
    struct bio *bio_original;
    struct bio *bio_private;

    unsigned long start_jif;
};

void linear_make_request(struct request_queue *q, struct bio *bio);

struct linear_request *linear_new_request(struct zblk *blk, struct bio *bio);

void linear_end_io(struct bio *bio, int error);

struct linear_bio_pair *linear_new_bp(struct bio *bio, sector_t sectors);

void linear_bp_end_io(struct bio *bio, int error);

#endif //ZBLK_LINEAR_H
