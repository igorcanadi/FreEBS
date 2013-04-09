#ifndef _FREEBS_H
#define  _FREEBS_H

#include <linux/mutex.h>
#include <linux/genhd.h>

struct freebs_socket {
	//struct drbd_work_queue work;
	struct mutex mutex;
	struct socket    *socket;
	/* this way we get our
	 * send/receive buffers off the stack */
	//union p_polymorph sbuf;
	//union p_polymorph rbuf;
};

enum freebs_thread_state {
	None,
	Running,
	Exiting,
	Restarting
};

struct freebs_thread {
	spinlock_t t_lock;
	struct task_struct *task;
	struct completion stop;
	enum freebs_thread_state t_state;
	int (*function) (struct freebs_thread *);
	struct freebs_conf *mdev;
	int reset_cpu_mask;
};

struct freebs_request {
	struct bio *private_bio;
  struct freebs_conf *mdev;
	sector_t sector;
	unsigned int size;
	struct freebs_thread asender;
  struct bio *master_bio;       /* master bio pointer */
};

/* The order of these constants is important.
 * The lower ones (<C_WF_REPORT_PARAMS) indicate
 * that there is no socket!
 * >=C_WF_REPORT_PARAMS ==> There is a socket
 */
enum drbd_conns {
	C_STANDALONE,
	C_DISCONNECTING,  /* Temporal state on the way to StandAlone. */
	C_UNCONNECTED,    /* >= C_UNCONNECTED -> inc_net() succeeds */

	/* These temporal states are all used on the way
	 * from >= C_CONNECTED to Unconnected.
	 * The 'disconnect reason' states
	 * I do not allow to change between them. */
	C_TIMEOUT,
	C_BROKEN_PIPE,
	C_NETWORK_FAILURE,
	C_PROTOCOL_ERROR,
	C_TEAR_DOWN,

	C_WF_CONNECTION,
	C_WF_REPORT_PARAMS, /* we have a socket */
	C_CONNECTED,      /* we have introduced each other */
	C_STARTING_SYNC_S,  /* starting full sync by admin request. */
	C_STARTING_SYNC_T,  /* starting full sync by admin request. */
	C_WF_BITMAP_S,
	C_WF_BITMAP_T,
	C_WF_SYNC_UUID,

	/* All SyncStates are tested with this comparison
	 * xx >= C_SYNC_SOURCE && xx <= C_PAUSED_SYNC_T */
	C_SYNC_SOURCE,
	C_SYNC_TARGET,
	C_VERIFY_S,
	C_VERIFY_T,
	C_PAUSED_SYNC_S,
	C_PAUSED_SYNC_T,

	C_AHEAD,
	C_BEHIND,

	C_MASK = 31
};

union freebs_state {
/* According to gcc's docs is the ...
 * The order of allocation of bit-fields within a unit (C90 6.5.2.1, C99 6.7.2.1).
 * Determined by ABI.
 * pointed out by Maxim Uvarov q<muvarov@ru.mvista.com>
 * even though we transmit as "cpu_to_be32(state)",
 * the offsets of the bitfields still need to be swapped
 * on different endianness.
 */
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned susp:1 ;   /* 2/2	 IO suspended no/yes (by user) */
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned peer_isp:1 ;
		unsigned user_isp:1 ;
		unsigned susp_nod:1 ; /* IO suspended because no data */
		unsigned susp_fen:1 ; /* IO suspended because fence peer handler runs*/
		unsigned _pad:9;   /* 0	 unused */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned _pad:9;
		unsigned susp_fen:1 ;
		unsigned susp_nod:1 ;
		unsigned user_isp:1 ;
		unsigned peer_isp:1 ;
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned susp:1 ;   /* 2/2	 IO suspended  no/yes */
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
#else
# error "this endianness is not supported"
#endif
	};
	unsigned int i;
};

struct freebs_conf {
  struct freebs_socket data;
  struct freebs_socket meta;
	unsigned int        send_cnt;
	atomic_t            packet_seq;
  unsigned long       flags;
  struct gendisk	    *vdisk;
	struct freebs_thread asender;
	union freebs_state state;
	unsigned int ko_count;
};

/* to shorten dev_warn(DEV, "msg"); and relatives statements */
#define DEV (disk_to_dev(mdev->vdisk))

#define D_ASSERT(exp)	if (!(exp)) \
	 dev_err(DEV, "ASSERT( " #exp " ) in %s:%d\n", __FILE__, __LINE__)

#define ERR_IF(exp) if (({						\
	int _b = (exp) != 0;						\
	if (_b) dev_err(DEV, "ASSERT FAILED: %s: (%s) in %s:%d\n",	\
			__func__, #exp, __FILE__, __LINE__);		\
	_b;								\
	}))


#endif
