//
// Created by h_d_zhang@163.com on 2021/5/21.
//

#include "linear.h"

void linear_make_request(struct request_queue *q, struct bio *bio) {
    struct linear_request *req;
    struct zblk_sharding *sharding = NULL;
    struct zblk *curr_zblk = (struct zblk *)q->queuedata;

    list_for_each_entry(sharding, &curr_zblk->sharding, list) {
        if(bio->bi_sector < sharding->end)
            break;
    }

    if(!sharding) {
        printk("ZBLK: can not found appropriate sharding \n");
        goto err;
    }

    //bio crosses a sharding
	if (unlikely(bio_end_sector(bio) > sharding->end)) {
		sector_t split_sectors;
		struct linear_bio_pair *bp;

		printk("ZBLK: bio(%s) crosses a sharding, so we split it\n",
         (bio_data_dir(bio) == WRITE) ? "WRITE" : "READ");
		split_sectors = sharding->end - bio->bi_sector;
        bp = linear_new_bp(bio, split_sectors);
        if(!bp)
            goto err;

        printk("ZBLK: bio bi_sector:%llu bi_size:%u  bi_idx:%u bi_vcnt:%u\n",
               bio->bi_sector, bio->bi_size, bio->bi_idx, bio->bi_vcnt);
        printk("ZBLK: bio_p1 bi_sector:%llu bi_size:%u  bi_idx:%u bi_vcnt:%u\n",
               bp->bio_p1->bi_sector, bp->bio_p1->bi_size, bp->bio_p1->bi_idx, bp->bio_p1->bi_vcnt);
        printk("ZBLK: bio_p2 bi_sector:%llu bi_size:%u  bi_idx:%u bi_vcnt:%u\n",
               bp->bio_p2->bi_sector, bp->bio_p2->bi_size, bp->bio_p2->bi_idx, bp->bio_p2->bi_vcnt);

        int split1_idx = bp->bio_p2->bi_idx;
        int split2_idx = bp->bio_p2->bi_idx;
        if(bp->bio_p2->bi_io_vec[split2_idx].bv_offset == 0) {
            split1_idx--;
        }
        printk("ZBLK: bio_p1 bi_io_vec:%d bv_offset:%d  bv_len:%d\n",
               split1_idx, bp->bio_p1->bi_io_vec[split1_idx].bv_offset, bp->bio_p1->bi_io_vec[split1_idx].bv_len);
        printk("ZBLK: bio_p2 bi_io_vec:%d bv_offset:%d  bv_len:%d\n",
               split2_idx, bp->bio_p2->bi_io_vec[split2_idx].bv_offset, bp->bio_p2->bi_io_vec[split2_idx].bv_len);

		linear_make_request(q, bp->bio_p1);
		linear_make_request(q, bp->bio_p2);
		return;
	}

	req = linear_new_request(curr_zblk, bio);
	if(!req)
	    goto err;
	req->bio_private->bi_bdev = sharding->bdev;
    req->bio_private->bi_sector = bio->bi_sector - (sharding->end - sharding->size);

	generic_make_request(req->bio_private);
	return;
err:
    bio_io_error(bio);
    printk("ZBLK: linear_make_request failed\n");
    return;
}

struct linear_request *linear_new_request(struct zblk *blk, struct bio *bio) {
    struct linear_request *req = kzalloc(sizeof(struct linear_request), GFP_KERNEL);
    if(!req) {
        printk("ZBLK: kzalloc failed\n");
        goto err;
    }

    req->blk = blk;
    req->bio_original = bio;
    req->bio_private = bio_clone(bio, GFP_NOIO);
    if(!req->bio_private) {
        printk("ZBLK: bio_clone failed\n");
        goto err_free_req;
    }
    req->bio_private->bi_private = req;
    req->bio_private->bi_end_io = linear_end_io;

    req->start_jif = jiffies;
    generic_start_io_acct(bio_data_dir(bio), bio->bi_size >> 9, &blk->disk->part0);
    return req;
err_free_req:
    kfree(req);
err:
    printk("ZBLK: linear_new_request failed\n");
    return NULL;
}

void linear_end_io(struct bio *bio, int error) {
    struct linear_request *req = (struct linear_request *)bio->bi_private;

    generic_end_io_acct(bio_data_dir(req->bio_original), &req->blk->disk->part0, req->start_jif);

    bio_put(req->bio_private);
    bio_endio(req->bio_original, error);
    kfree(req);
}

struct linear_bio_pair *linear_new_bp(struct bio *bio, sector_t offect_sectors) {
    struct linear_bio_pair *bp;
    unsigned int bv_idx, bv_offset;

    bv_idx = bio->bi_idx;
    bv_offset = offect_sectors << SECTOR_SHIFT;
    while(bv_idx < bio->bi_vcnt) {
        if(bv_offset < bio->bi_io_vec[bv_idx].bv_len)
            break;
        bv_offset -= bio->bi_io_vec[bv_idx].bv_len;
        bv_idx++;
    }

    if(bv_idx == bio->bi_vcnt) {
        printk("ZBLK: offect_sectors is invalid\n");
        goto err;
    }

    bp = kzalloc(sizeof(struct linear_bio_pair), GFP_KERNEL);
    if(!bp) {
        printk("ZBLK: kzalloc failed\n");
        goto err;
    }
    atomic_set(&bp->cnt, 2);
    bp->bio_original = bio;
    bp->error = 0;

    bp->bio_p1 = bio_clone(bp->bio_original, GFP_NOIO);
    if(!bp->bio_p1) {
        printk("ZBLK: bio_clone bio_p1 failed\n");
        goto err_free_bp;
    }
    bp->bio_p1->bi_private = bp;
    bp->bio_p1->bi_end_io = linear_bp_end_io;
    bp->bio_p1->bi_size = offect_sectors << SECTOR_SHIFT;
    bp->bio_p1->bi_io_vec[bv_idx].bv_len = bv_offset;

    bp->bio_p2 = bio_clone(bp->bio_original, GFP_NOIO);
    if(!bp->bio_p2) {
        printk("ZBLK: bio_clone bio_p2 failed\n");
        goto err_put_bio_p1;
    }
    bp->bio_p2->bi_private = bp;
    bp->bio_p2->bi_end_io = linear_bp_end_io;
    bp->bio_p2->bi_sector += offect_sectors;
    bp->bio_p2->bi_size = bio->bi_size - (offect_sectors << SECTOR_SHIFT);
    bp->bio_p2->bi_idx = bv_idx;
    bp->bio_p2->bi_io_vec[bv_idx].bv_offset += bv_offset;
    bp->bio_p2->bi_io_vec[bv_idx].bv_len -= bv_offset;

    return bp;
err_put_bio_p1:
    bio_put(bp->bio_p1);
err_free_bp:
    kfree(bp);
err:
    printk("ZBLK: linear_new_bp failed\n");
    return NULL;
}

void linear_bp_end_io(struct bio *bio, int error) {
    struct linear_bio_pair *bp = (struct linear_bio_pair *)bio->bi_private;

    if (error)
        bp->error = error;

    if (atomic_dec_and_test(&bp->cnt)) {
		bio_put(bp->bio_p1);
		bio_put(bp->bio_p2);

		bio_endio(bp->bio_original, bp->error);
		kfree(bp);
	}
}