//
// Created by h_d_zhang@163.com on 2021/5/21.
//

#ifndef ZBLK_MEMBLK_H
#define ZBLK_MEMBLK_H

#include <linux/blkdev.h>
#include <linux/fs.h>

void memblk_make_request(struct request_queue *q, struct bio *bio);

#endif //ZBLK_MEMBLK_H


