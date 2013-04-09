#include "freebs.h"

/* global flag bits */
enum {
	CREATE_BARRIER,		/* next P_DATA is preceded by a P_BARRIER */
	SIGNAL_ASENDER,		/* whether asender wants to be interrupted */
	SEND_PING,		/* whether asender should send a ping asap */

	UNPLUG_QUEUED,		/* only relevant with kernel 2.4 */
	UNPLUG_REMOTE,		/* sending a "UnplugRemote" could help */
	MD_DIRTY,		/* current uuids and flags not yet on disk */
	DISCARD_CONCURRENT,	/* Set on one node, cleared on the peer! */
	USE_DEGR_WFC_T,		/* degr-wfc-timeout instead of wfc-timeout. */
	CLUSTER_ST_CHANGE,	/* Cluster wide state change going on... */
	CL_ST_CHG_SUCCESS,
	CL_ST_CHG_FAIL,
	CRASHED_PRIMARY,	/* This node was a crashed primary.
				 * Gets cleared when the state.conn
				 * goes into C_CONNECTED state. */
	NO_BARRIER_SUPP,	/* underlying block device doesn't implement barriers */
	CONSIDER_RESYNC,

	MD_NO_FUA,		/* Users wants us to not use FUA/FLUSH on meta data dev */
	SUSPEND_IO,		/* suspend application io */
	BITMAP_IO,		/* suspend application io;
				   once no more io in flight, start bitmap io */
	BITMAP_IO_QUEUED,       /* Started bitmap IO */
	GO_DISKLESS,		/* Disk is being detached, on io-error or admin request. */
	WAS_IO_ERROR,		/* Local disk failed returned IO error */
	RESYNC_AFTER_NEG,       /* Resync after online grow after the attach&negotiate finished. */
	NET_CONGESTED,		/* The data socket is congested */

	CONFIG_PENDING,		/* serialization of (re)configuration requests.
				 * if set, also prevents the device from dying */
	DEVICE_DYING,		/* device became unconfigured,
				 * but worker thread is still handling the cleanup.
				 * reconfiguring (nl_disk_conf, nl_net_conf) is dissalowed,
				 * while this is set. */
	RESIZE_PENDING,		/* Size change detected locally, waiting for the response from
				 * the peer, if it changed there as well. */
	CONN_DRY_RUN,		/* Expect disconnect after resync handshake. */
	GOT_PING_ACK,		/* set when we receive a ping_ack packet, misc wait gets woken */
	NEW_CUR_UUID,		/* Create new current UUID when thawing IO */
	AL_SUSPENDED,		/* Activity logging is currently suspended. */
	AHEAD_TO_SYNC_SOURCE,   /* Ahead -> SyncSource queued */
	STATE_SENT,		/* Do not change state/UUIDs while this is set */
};

/* I don't remember why XCPU ...
 * This is used to wake the asender,
 * and to interrupt sending the sending task
 * on disconnect.
 */
#define DRBD_SIG SIGXCPU

/* on the wire */
enum drbd_packets {
	/* receiver (data socket) */
	P_DATA		      = 0x00,
	P_DATA_REPLY	      = 0x01, /* Response to P_DATA_REQUEST */
	P_RS_DATA_REPLY	      = 0x02, /* Response to P_RS_DATA_REQUEST */
	P_BARRIER	      = 0x03,
	P_BITMAP	      = 0x04,
	P_BECOME_SYNC_TARGET  = 0x05,
	P_BECOME_SYNC_SOURCE  = 0x06,
	P_UNPLUG_REMOTE	      = 0x07, /* Used at various times to hint the peer */
	P_DATA_REQUEST	      = 0x08, /* Used to ask for a data block */
	P_RS_DATA_REQUEST     = 0x09, /* Used to ask for a data block for resync */
	P_SYNC_PARAM	      = 0x0a,
	P_PROTOCOL	      = 0x0b,
	P_UUIDS		      = 0x0c,
	P_SIZES		      = 0x0d,
	P_STATE		      = 0x0e,
	P_SYNC_UUID	      = 0x0f,
	P_AUTH_CHALLENGE      = 0x10,
	P_AUTH_RESPONSE	      = 0x11,
	P_STATE_CHG_REQ	      = 0x12,

	/* asender (meta socket */
	P_PING		      = 0x13,
	P_PING_ACK	      = 0x14,
	P_RECV_ACK	      = 0x15, /* Used in protocol B */
	P_WRITE_ACK	      = 0x16, /* Used in protocol C */
	P_RS_WRITE_ACK	      = 0x17, /* Is a P_WRITE_ACK, additionally call set_in_sync(). */
	P_DISCARD_ACK	      = 0x18, /* Used in proto C, two-primaries conflict detection */
	P_NEG_ACK	      = 0x19, /* Sent if local disk is unusable */
	P_NEG_DREPLY	      = 0x1a, /* Local disk is broken... */
	P_NEG_RS_DREPLY	      = 0x1b, /* Local disk is broken... */
	P_BARRIER_ACK	      = 0x1c,
	P_STATE_CHG_REPLY     = 0x1d,

	/* "new" commands, no longer fitting into the ordering scheme above */

	P_OV_REQUEST	      = 0x1e, /* data socket */
	P_OV_REPLY	      = 0x1f,
	P_OV_RESULT	      = 0x20, /* meta socket */
	P_CSUM_RS_REQUEST     = 0x21, /* data socket */
	P_RS_IS_IN_SYNC	      = 0x22, /* meta socket */
	P_SYNC_PARAM89	      = 0x23, /* data socket, protocol version 89 replacement for P_SYNC_PARAM */
	P_COMPRESSED_BITMAP   = 0x24, /* compressed or otherwise encoded bitmap transfer */
	/* P_CKPT_FENCE_REQ      = 0x25, * currently reserved for protocol D */
	/* P_CKPT_DISABLE_REQ    = 0x26, * currently reserved for protocol D */
	P_DELAY_PROBE         = 0x27, /* is used on BOTH sockets */
	P_OUT_OF_SYNC         = 0x28, /* Mark as out of sync (Outrunning), data socket */
	P_RS_CANCEL           = 0x29, /* meta: Used to cancel RS_DATA_REQUEST packet by SyncSource */

	P_MAX_CMD	      = 0x2A,
	P_MAY_IGNORE	      = 0x100, /* Flag to test if (cmd > P_MAY_IGNORE) ... */
	P_MAX_OPT_CMD	      = 0x101,

	/* special command ids for handshake */

	P_HAND_SHAKE_M	      = 0xfff1, /* First Packet on the MetaSock */
	P_HAND_SHAKE_S	      = 0xfff2, /* First Packet on the Socket */

	P_HAND_SHAKE	      = 0xfffe	/* FIXED for the next century! */
};

#define __packed __attribute__((packed))

struct p_header_only {
	//u16	  magic;	/* use DRBD_MAGIC_BIG here */
	u16	  command;
	u32	  length;	/* Use only 24 bits of that. Ignore the highest 8 bit. */
	u8	  payload[0];
} __packed;

union p_header {
  struct p_header_only h;
};

struct p_data {
	union p_header head;
	u64	    sector;    /* 64 bits sector number */
	u64	    block_id;  /* to identify the request in protocol B&C */
	u32	    seq_num;
	u32	    dp_flags;
} __packed;

/* returns 1 if it was successful,
 * returns 0 if there was no data socket.
 * so wherever you are going to use the data.socket, e.g. do
 * if (!freebs_get_data_sock(mdev))
 *	return 0;
 *	CODE();
 * freebs_put_data_sock(mdev);
 */
static inline int freebs_get_data_sock(struct freebs_conf *mdev)
{
	mutex_lock(&mdev->data.mutex);
	/* freebs_disconnect() could have called freebs_free_sock()
	 * while we were waiting in down()... */
	if (unlikely(mdev->data.socket == NULL)) {
		mutex_unlock(&mdev->data.mutex);
		return 0;
	}
	return 1;
}

static inline void freebs_put_data_sock(struct freebs_conf *mdev)
{
	mutex_unlock(&mdev->data.mutex);
}

int freebs_send(struct freebs_conf *, struct socket *,
	      void *, size_t, unsigned);
