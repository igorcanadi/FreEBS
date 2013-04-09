#include <linux/blk_types.h>
#include <linux/bio.h>
#include <linux/socket.h>
#include <net/sock.h>

#include "net.h"
#include "freebs.h"

#define FBS_WRITE (1 << 0)

bool disable_sendpage = false;

/*
int freebs_send_write(struct socket *sock, void *buf,
        unsigned int nr_sectors) 
{
    struct freebs_header fbshdr;
    fbshdr.flags = FBS_WRITE;
    fbshdr.nr_sectors = cpu_to_be32(nr_sectors);
    freebs_send(sock, &fbshdr, sizeof(fbshdr), MSG_MORE);
    freebs_send(sock, buf, nr_sectors * KERNEL_SECTOR_SIZE, NULL);
}
*/

/* I understand drbd_no_send_page, but I don't understand drbd_send_page, which
 * seems much more complicated. So I am going with this one.
 * -- Jim
 */
/* The idea of sendpage seems to be to put some kind of reference
 * to the page into the skb, and to hand it over to the NIC. In
 * this process get_page() gets called.
 *
 * As soon as the page was really sent over the network put_page()
 * gets called by some part of the network layer. [ NIC driver? ]
 *
 * [ get_page() / put_page() increment/decrement the count. If count
 *   reaches 0 the page will be freed. ]
 *
 * This works nicely with pages from FSs.
 * But this means that in protocol A we might signal IO completion too early!
 *
 * In order not to corrupt data during a resync we must make sure
 * that we do not reuse our own buffer pages (EEs) to early, therefore
 * we have the net_ee list.
 *
 * XFS seems to have problems, still, it submits pages with page_count == 0!
 * As a workaround, we disable sendpage on pages
 * with page_count == 0 or PageSlab.
 */
static int _freebs_no_send_page(struct freebs_conf *mdev, struct page *page,
		   int offset, size_t size, unsigned msg_flags)
{
	int sent = freebs_send(mdev, mdev->data.socket, kmap(page) + offset, size, msg_flags);
	kunmap(page);
	if (sent == size)
		mdev->send_cnt += size>>9;
	return sent == size;
}

/*
static int _freebs_send_bio(struct freebs_conf *mdev, struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	* hint all but last page with MSG_MORE *
	bio_for_each_segment(bvec, bio, i) {
		if (!_freebs_no_send_page(mdev, bvec->bv_page,
				     bvec->bv_offset, bvec->bv_len,
				     i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
			return 0;
	}
	return 1;
}
*/

static inline void freebs_update_congested(struct freebs_conf *mdev)
{
	struct sock *sk = mdev->data.socket->sk;
	if (sk->sk_wmem_queued > sk->sk_sndbuf * 4 / 5)
		set_bit(NET_CONGESTED, &mdev->flags);
}

static inline enum freebs_thread_state get_t_state(struct freebs_thread *thi)
{
	/* THINK testing the t_state seems to be uncritical in all cases
	 * (but thread_{start,stop}), so we can read it *without* the lock.
	 *	--lge */

	smp_rmb();
	return thi->t_state;
}

static inline void wake_asender(struct freebs_conf *mdev)
{
	if (test_bit(SIGNAL_ASENDER, &mdev->flags))
		force_sig(DRBD_SIG, mdev->asender.task);
}

static inline void request_ping(struct freebs_conf *mdev)
{
	set_bit(SEND_PING, &mdev->flags);
	wake_asender(mdev);
}

/* called on sndtimeo
 * returns false if we should retry,
 * true if we think connection is dead
 */
static int we_should_drop_the_connection(struct freebs_conf *mdev, struct socket *sock)
{
	int drop_it;
	/* long elapsed = (long)(jiffies - mdev->last_received); */

	drop_it =   mdev->meta.socket == sock
		|| !mdev->asender.task
		|| get_t_state(&mdev->asender) != Running
		|| mdev->state.conn < C_CONNECTED;

	if (drop_it)
		return true;

	drop_it = !--mdev->ko_count;
	if (!drop_it) {
		dev_err(DEV, "[%s/%d] sock_sendmsg time expired, ko = %u\n",
		       current->comm, current->pid, mdev->ko_count);
		request_ping(mdev);
	}

	return drop_it; /* && (mdev->state == R_PRIMARY) */;
}

static int _freebs_send_page(struct freebs_conf *mdev, struct page *page,
		    int offset, size_t size, unsigned msg_flags)
{
	mm_segment_t oldfs = get_fs();
	int sent, ok;
	int len = size;

	/* e.g. XFS meta- & log-data is in slab pages, which have a
	 * page_count of 0 and/or have PageSlab() set.
	 * we cannot use send_page for those, as that does get_page();
	 * put_page(); and would cause either a VM_BUG directly, or
	 * __page_cache_release a page that would actually still be referenced
	 * by someone, leading to some obscure delayed Oops somewhere else. */
	if (disable_sendpage || (page_count(page) < 1) || PageSlab(page))
		return _freebs_no_send_page(mdev, page, offset, size, msg_flags);

	msg_flags |= MSG_NOSIGNAL;
	freebs_update_congested(mdev);
	set_fs(KERNEL_DS);
	do {
		sent = mdev->data.socket->ops->sendpage(mdev->data.socket, page,
							offset, len,
							msg_flags);
		if (sent == -EAGAIN) {
			if (we_should_drop_the_connection(mdev,
							  mdev->data.socket))
				break;
			else
				continue;
		}
		if (sent <= 0) {
			dev_warn(DEV, "%s: size=%d len=%d sent=%d\n",
			     __func__, (int)size, len, sent);
			break;
		}
		len    -= sent;
		offset += sent;
	} while (len > 0 /* THINK && mdev->cstate >= C_CONNECTED*/);
	set_fs(oldfs);
	clear_bit(NET_CONGESTED, &mdev->flags);

	ok = (len == 0);
	if (likely(ok))
		mdev->send_cnt += size>>9;
	return ok;
}


static int _freebs_send_zc_bio(struct freebs_conf *mdev, struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	/* hint all but last page with MSG_MORE */
	bio_for_each_segment(bvec, bio, i) {
		if (!_freebs_send_page(mdev, bvec->bv_page,
				     bvec->bv_offset, bvec->bv_len,
				     i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
			return 0;
	}
	return 1;
}

/* Used to send write requests
 * R_PRIMARY -> Peer	(P_DATA)
 */
int freebs_send_dblock(struct freebs_conf *mdev, struct freebs_request *req)
{
	int ok = 1;
	struct p_data p;
	//unsigned int dp_flags = 0;
	//void *dgb;
	//int dgs;

	if (!freebs_get_data_sock(mdev))
		return 0;

    /*
	dgs = (mdev->agreed_pro_version >= 87 && mdev->integrity_w_tfm) ?
		crypto_hash_digestsize(mdev->integrity_w_tfm) : 0;

	if (req->size <= DRBD_MAX_SIZE_H80_PACKET) {
		p.head.h80.magic   = BE_DRBD_MAGIC;
		p.head.h80.command = cpu_to_be16(P_DATA);
		p.head.h80.length  =
			cpu_to_be16(sizeof(p) - sizeof(union p_header) + dgs + req->size);
	} else {
		p.head.h95.magic   = BE_DRBD_MAGIC_BIG;
		p.head.h95.command = cpu_to_be16(P_DATA);
		p.head.h95.length  =
			cpu_to_be32(sizeof(p) - sizeof(union p_header) + dgs + req->size);
	}
    */
    p.head.h.command = cpu_to_be16(P_DATA);
    p.head.h.length  =
			cpu_to_be32(sizeof(p) - sizeof(union p_header) + /*dgs +*/ req->size);

	p.sector   = cpu_to_be64(req->sector);
	p.block_id = (unsigned long)req;
	p.seq_num  = cpu_to_be32(atomic_add_return(1, &mdev->packet_seq));

    /*
	dp_flags = bio_flags_to_wire(mdev, req->master_bio->bi_rw);

	if (mdev->state.conn >= C_SYNC_SOURCE &&
	    mdev->state.conn <= C_PAUSED_SYNC_T)
		dp_flags |= DP_MAY_SET_IN_SYNC;

	p.dp_flags = cpu_to_be32(dp_flags);
	set_bit(UNPLUG_REMOTE, &mdev->flags);
    */
	ok = (sizeof(p) ==
		freebs_send(mdev, mdev->data.socket, &p, sizeof(p), 0));
    /*
	if (ok && dgs) {
		dgb = mdev->int_dig_out;
		drbd_csum_bio(mdev, mdev->integrity_w_tfm, req->master_bio, dgb);
		ok = dgs == drbd_send(mdev, mdev->data.socket, dgb, dgs, 0);
	}
    */
	if (ok) {
		/* For protocol A, we have to memcpy the payload into
		 * socket buffers, as we may complete right away
		 * as soon as we handed it over to tcp, at which point the data
		 * pages may become invalid.
		 *
		 * For data-integrity enabled, we copy it as well, so we can be
		 * sure that even if the bio pages may still be modified, it
		 * won't change the data on the wire, thus if the digest checks
		 * out ok after sending on this side, but does not fit on the
		 * receiving side, we sure have detected corruption elsewhere.
		if (mdev->net_conf->wire_protocol == DRBD_PROT_A || dgs)
			ok = _drbd_send_bio(mdev, req->master_bio);
		else
			ok = _drbd_send_zc_bio(mdev, req->master_bio);
		 */
        ok = _freebs_send_zc_bio(mdev, req->master_bio);

		/* double check digest, sometimes buffers have been modified in flight. */
		//if (dgs > 0 && dgs <= 64) {
			/* 64 byte, 512 bit, is the largest digest size
			 * currently supported in kernel crypto.
			unsigned char digest[64];
			drbd_csum_bio(mdev, mdev->integrity_w_tfm, req->master_bio, digest);
			if (memcmp(mdev->int_dig_out, digest, dgs)) {
				dev_warn(DEV,
					"Digest mismatch, buffer modified by upper layers during write: %llus +%u\n",
					(unsigned long long)req->sector, req->size);
			}
		}*/ /* else if (dgs > 64) {
		     ... Be noisy about digest too large ...
		} */
	}

	freebs_put_data_sock(mdev);

	return ok;
}

/*
 * you must have down()ed the appropriate [m]sock_mutex elsewhere!
 *
 * buf = buffer if write, null if read
 * direction = true for write, false for read
 * size = size in bytes
 */
int freebs_send(struct freebs_conf *mdev, struct socket *sock,
	      void *buf, size_t size, unsigned msg_flags)
{
	struct kvec iov;
	struct msghdr msg;
	int rv, sent = 0;

	if (!sock)
		return -1000;

	iov.iov_base = buf;
	iov.iov_len  = size;

	msg.msg_name       = NULL;
	msg.msg_namelen    = 0;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags      = msg_flags | MSG_NOSIGNAL;

	do {
		/* STRANGE
		 * tcp_sendmsg does _not_ use its size parameter at all ?
		 *
		 * -EAGAIN on timeout, -EINTR on signal.
		 */
/* THINK
 * do we need to block DRBD_SIG if sock == &meta.socket ??
 * otherwise wake_asender() might interrupt some send_*Ack !
 */
		rv = kernel_sendmsg(sock, &msg, &iov, 1, size);
		if (rv == -EAGAIN) {
            /*
			if (we_should_drop_the_connection(mdev, sock))
				break;
			else
            */
				continue;
		}
		D_ASSERT(rv != 0);
		if (rv == -EINTR) {
			flush_signals(current);
			rv = 0;
		}
		if (rv < 0)
			break;
		sent += rv;
		iov.iov_base += rv;
		iov.iov_len  -= rv;
	} while (sent < size);

	if (rv <= 0) {
		if (rv != -EAGAIN) {
			dev_err(DEV, "sendmsg returned %d\n",
			    rv);
			//drbd_force_state(mdev, NS(conn, C_BROKEN_PIPE));
		} //else
			//drbd_force_state(mdev, NS(conn, C_TIMEOUT));
	}

	return sent;
}
