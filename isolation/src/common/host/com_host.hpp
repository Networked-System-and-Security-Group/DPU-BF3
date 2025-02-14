#ifndef __COM_HOST_H__
#define __COM_HOST_H__

/* Convert logarithm to value */
#define L2V(l) (1UL << (l))
/* dev msg stream buffer built from chunks of
 * 2^FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE each */
#define MSG_HOST_BUFF_BSIZE (512 * L2V(FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE))
/* Number of entries in each RQ/SQ/CQ is 2^LOG_Q_DEPTH. */
#define LOG_Q_DEPTH 7
#define Q_DEPTH L2V(LOG_Q_DEPTH)
/* SQ/RQ data entry byte size is 512B (enough for packet data in this case). */
#define LOG_Q_DATA_ENTRY_BSIZE 11
/* SQ/RQ data entry byte size log to value. */
#define Q_DATA_ENTRY_BSIZE L2V(LOG_Q_DATA_ENTRY_BSIZE)
/* SQ/RQ DATA byte size is queue depth times entry byte size. */
#define Q_DATA_BSIZE Q_DEPTH *Q_DATA_ENTRY_BSIZE
#endif