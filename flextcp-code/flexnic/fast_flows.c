#include <assert.h>
#include <rte_config.h>
#include <rte_ip.h>
#include <rte_hash_crc.h>

#include <flextcp_plif.h>
#include <utils_sync.h>

#include "internal.h"
#include "fastemu.h"
#include "tcp_common.h"

#define TCP_MSS 1448
#define HWXSUM_EN 1

struct flow_key {
  ip_addr_t local_ip;
  ip_addr_t remote_ip;
  beui16_t local_port;
  beui16_t remote_port;
} __attribute__((packed));



static void flow_tx_read(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, void *dst);
static void flow_rx_write(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, const void *src);
#ifdef FLEXNIC_PL_OOO_RECV
static void flow_rx_seq_write(struct flextcp_pl_flowst *fs, uint32_t seq,
    uint16_t len, const void *src);
#endif
static void flow_tx_segment(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *buf, struct flextcp_pl_flowst *fs,
    uint32_t seq, uint32_t ack, uint32_t rxwnd, uint16_t payload,
    uint32_t payload_pos, uint32_t ts_echo, uint32_t ts_my);
static void flow_tx_ack(struct dataplane_context *ctx, uint32_t seq,
    uint32_t ack, uint32_t rxwnd, uint32_t echo_ts, uint32_t my_ts, void *buf,
    uint16_t off, struct network_buf_handle *nbh,
    struct tcp_timestamp_opt *ts_opt);
static void flow_reset_retransmit(struct flextcp_pl_flowst *fs);

static inline void tcp_checksums(struct network_buf_handle *nbh,
    struct pkt_tcp *p);
static inline int fast_flow_lookup(ip_addr_t l_ip, ip_addr_t r_ip, beui16_t l_p,
    beui16_t r_p, struct flextcp_pl_flowst **pfs);

void fast_flows_qman_pf(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n)
{
  uint32_t flow_id;
  uint16_t i;

  for (i = 0; i < n; i++) {
    flow_id = pl_memory->qmant[queues[i]].id & ~FLEXNIC_PL_QMANTE_TYPEMASK;
    rte_prefetch0(&pl_memory->flowst[flow_id]);
  }
}

int fast_flows_qman(struct dataplane_context *ctx, uint32_t queue,
    struct network_buf_handle *nbh, void *buf, uint32_t ts)
{
  uint32_t flow_id = pl_memory->qmant[queue].id & ~FLEXNIC_PL_QMANTE_TYPEMASK;
  struct flextcp_pl_flowst *fs = &pl_memory->flowst[flow_id];
  struct obj_hdr oh;
  uint32_t avail, len, tx_pos, tx_seq, ack, rx_wnd, hdrlen, objlen;
  uint16_t new_core;
  int ret = 0;

  util_spin_lock(&fs->lock);

  /* if connection has been moved, add to forwarding queue and stop */
  new_core = pl_memory->flow_group_steering[fs->flow_group];
  if (new_core != ctx->id) {
    /*fprintf(stderr, "fast_flows_qman: arrived on wrong core, forwarding "
        "%u -> %u (fs=%p, fg=%u)\n", ctx->id, new_core, fs, fs->flow_group);*/

    /* enqueue flo state on forwarding queue */
    if (rte_ring_enqueue(ctxs[new_core]->qman_fwd_ring, fs) != 0) {
      fprintf(stderr, "fast_flows_qman: rte_ring_enqueue failed\n");
      abort();
    }

    /* clear queue manager queue */
    if (qman_set(ctx->qman_t, fs->qman_qid, 0, 0, 0, APP_QMAN_FLOW,
          QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_SET_AVAIL |
          QMAN_SET_OPAQUE) != 0)
    {
      fprintf(stderr, "flast_flows_qman: qman_set clear failed, UNEXPECTED\n");
      abort();
    }

    util_flexnic_kick(&pl_memory->kctx[new_core]);

    ret = -1;
    goto unlock;
  }

  /* calculate how much is available to be sent */
  avail = tcp_txavail(fs, NULL);

#if PL_DEBUG_ATX
  fprintf(stderr, "ATX try_sendseg local=%08x:%05u remote=%08x:%05u "
      "tx_head=%x tx_next_pos=%x avail=%u\n",
      f_beui32(fs->local_ip), f_beui16(fs->local_port),
      f_beui32(fs->remote_ip), f_beui16(fs->remote_port),
      fs->tx_head, fs->tx_next_pos, avail);
#endif
#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_afloqman te_afloqman = {
      .flow_id = flow_id,
      .tx_base = fs->tx_base,
      .tx_head = fs->tx_head,
      .tx_next_pos = fs->tx_next_pos,
      .tx_len = fs->tx_len,
      .rx_remote_avail = fs->rx_remote_avail,
      .tx_sent = fs->tx_sent,
      .tx_objrem = fs->tx_objrem,
    };
  trace_event(FLEXNIC_PL_TREV_AFLOQMAN, sizeof(te_afloqman), &te_afloqman);
#endif

  /* if there is no data available, stop */
  if (avail == 0) {
    ret = -1;
    goto unlock;
  }
  len = MIN(avail, TCP_MSS);

  /* this is an object connection, we need to be careful to make segments end on
   * segment boundaries*/
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
    /* if we're starting a new object, need to fetch header first to determine
     * length */
    if (fs->tx_objrem == 0) {
      if (len < sizeof(oh)) {
        /* TODO: this should not be an abort of flexnic but the kernel should
         * stop the application */
        fprintf(stderr, "fast_flows_qman: bump not on object boundary\n");
        abort();
      }

      /* fetch header */
      flow_tx_read(fs, fs->tx_next_pos, sizeof(oh), &oh);
      hdrlen = sizeof(oh) + oh.dstlen;
      objlen = hdrlen + f_beui32(oh.len);

      /* make sure whole object header fits in first segment */
      if (len < hdrlen) {
        /* TODO: this should not be an abort of flexnic but the kernel should
         * stop the application */
        fprintf(stderr, "fast_flows_qman: header does not fit in first "
            "segment\n");
        abort();
      }

      fs->tx_objrem = objlen;
    }

    /* crop segment to object end if necessary */
    len = MIN(len, fs->tx_objrem);

    /* update object conn state */
    fs->tx_objrem -= len;

    /* if more data is available need to re-arm queue manager */
    if (avail > len) {
      if (qman_set(ctx->qman_t, fs->qman_qid, 0, 1, 1, APP_QMAN_FLOW,
            QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_ADD_AVAIL |
            QMAN_SET_OPAQUE) != 0)
      {
        fprintf(stderr, "flast_flows_qman: qman_set 1 failed, UNEXPECTED\n");
        abort();
      }
    }
  }

  /* state snapshot for creating segment */
  tx_seq = fs->tx_next_seq;
  tx_pos = fs->tx_next_pos;
  rx_wnd = fs->rx_avail;
  ack = fs->rx_next_seq;

  /* update tx flow state */
  fs->tx_next_seq += len;
  fs->tx_next_pos += len;
  if (fs->tx_next_pos >= fs->tx_len) {
    fs->tx_next_pos -= fs->tx_len;
  }
  fs->tx_sent += len;

  /* send out segment */
  flow_tx_segment(ctx, nbh, buf, fs, tx_seq, ack, rx_wnd, len, tx_pos,
      fs->tx_next_ts, ts);

unlock:
  util_spin_unlock(&fs->lock);
  return ret;
}

int fast_flows_qman_fwd(struct dataplane_context *ctx,
    struct flextcp_pl_flowst *fs)
{
  unsigned avail;

  /*fprintf(stderr, "fast_flows_qman_fwd: fs=%p\n", fs);*/

  util_spin_lock(&fs->lock);

  avail = tcp_txavail(fs, NULL);

  /* re-arm queue manager */
  if (qman_set(ctx->qman_t, fs->qman_qid, fs->tx_rate, avail, TCP_MSS,
        APP_QMAN_FLOW, QMAN_SET_RATE | QMAN_SET_MAXCHUNK
        | QMAN_SET_AVAIL | QMAN_SET_OPAQUE) != 0)
  {
    fprintf(stderr, "fast_flows_qman_fwd: qman_set failed, UNEXPECTED\n");
    abort();
  }

  util_spin_unlock(&fs->lock);
  return 0;
}

/* Received packet */
int fast_flows_packet(struct dataplane_context *ctx, void *buf,
    uint16_t off, uint16_t len, struct network_buf_handle *nbh, void *fsp,
    uint32_t ts)
{
  struct pkt_tcp *p = (struct pkt_tcp *) ((uint8_t *) buf + off);
  struct tcp_opts opts;
  struct flextcp_pl_flowst *fs = fsp;
  struct flextcp_pl_appst *appst = NULL;
  uint32_t payload_bytes, payload_off, seq, ack, old_avail, new_avail,
           orig_payload;
  uint32_t rx_bump = 0, tx_bump = 0, i, rx_pos, rtt;
  int no_permanent_sp = 0;
  uint16_t tcp_extra_hlen, trim_start, trim_end;
  struct flextcp_pl_arx arx;
  struct obj_hdr *oh;
  int trigger_ack = 0;
  struct flextcp_pl_appctx *actx;
  uint64_t steer_id;


  /* abort if not a valid tcp packet */
  if (len < sizeof(*p) ||
      f_beui16(p->eth.type) != ETH_TYPE_IP ||
      p->ip.proto != IP_PROTO_TCP ||
      IPH_V(&p->ip) != 4 || IPH_HL(&p->ip) != 5 ||
      TCPH_HDRLEN(&p->tcp) < 5 ||
      len < f_beui16(p->ip.len) + sizeof(p->eth))
  {
    return -1;
  }

  /* parse options */
  if (tcp_parse_options(p, len, &opts) != 0) {
    fprintf(stderr, "dma_krx_pkt_fastpath: parsing options failed\n");
    return -1;
  }
  if (opts.ts == NULL) {
    fprintf(stderr, "dma_krx_pkt_fastpath: no timestamp option.\n");
    return -1;
  }

  tcp_extra_hlen = (TCPH_HDRLEN(&p->tcp) - 5) * 4;
  payload_off = sizeof(*p) + tcp_extra_hlen;
  payload_bytes =
      f_beui16(p->ip.len) - (sizeof(p->ip) + sizeof(p->tcp) + tcp_extra_hlen);
  orig_payload = payload_bytes;

#if PL_DEBUG_ARX
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u  RX: seq=%u ack=%u "
      "flags=%x payload=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), f_beui32(p->tcp.seqno),
      f_beui32(p->tcp.ackno), TCPH_FLAGS(&p->tcp), payload_bytes);
#endif

  actx = &pl_memory->appctx[ctx->id][fs->db_id];
  fast_actx_rxq_pf(ctx, actx);

  util_spin_lock(&fs->lock);

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_rxfs te_rxfs = {
      .local_ip = f_beui32(p->ip.dest),
      .remote_ip = f_beui32(p->ip.src),
      .local_port = f_beui16(p->tcp.dest),
      .remote_port = f_beui16(p->tcp.src),

      .flow_seq = f_beui32(p->tcp.seqno),
      .flow_ack = f_beui32(p->tcp.ackno),
      .flow_flags = TCPH_FLAGS(&p->tcp),
      .flow_len = payload_bytes,

      .fs_rx_nextpos = fs->rx_next_pos,
      .fs_rx_nextseq = fs->rx_next_seq,
      .fs_rx_avail = fs->rx_avail,
      .fs_tx_nextpos = fs->tx_next_pos,
      .fs_tx_nextseq = fs->tx_next_seq,
      .fs_tx_sent = fs->tx_sent,
    };
  trace_event(FLEXNIC_PL_TREV_RXFS, sizeof(te_rxfs), &te_rxfs);
#endif

#if PL_DEBUG_ARX
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u  ST: op=%"PRIx64
      " rx_pos=%x rx_next_seq=%u rx_avail=%x  tx_pos=%x tx_next_seq=%u"
      " tx_sent=%u sp=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), fs->opaque, fs->rx_next_pos,
      fs->rx_next_seq, fs->rx_avail, fs->tx_next_pos, fs->tx_next_seq,
      fs->tx_sent, fs->slowpath);
#endif

  /* state indicates slow path */
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_SLOWPATH) != 0) {
    fprintf(stderr, "dma_krx_pkt_fastpath: slowpath because of state\n");
    goto slowpath;
  }

  /* if we get weird flags -> kernel */
  if ((TCPH_FLAGS(&p->tcp) & ~(TCP_ACK | TCP_PSH | TCP_ECE | TCP_CWR)) != 0) {
    if ((TCPH_FLAGS(&p->tcp) & TCP_SYN) != 0) {
      /* for SYN/SYN-ACK we'll let the kernel handle them out of band */
      no_permanent_sp = 1;
    } else {
      fprintf(stderr, "dma_krx_pkt_fastpath: slow path because of flags (%x)\n",
          TCPH_FLAGS(&p->tcp));
    }
    goto slowpath;
  }

  /* calculate how much data is available to be sent before processing this
   * packet, to detect whether more data can be sent afterwards */
  old_avail = tcp_txavail(fs, NULL);

  seq = f_beui32(p->tcp.seqno);
  ack = f_beui32(p->tcp.ackno);
  rx_pos = fs->rx_next_pos;

  /* trigger an ACK if there is payload (even if we discard it) */
  if (payload_bytes > 0)
    trigger_ack = 1;

  /* Stats for CC */
  if ((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK) {
    __sync_fetch_and_add(&fs->cnt_rx_acks, 1);
    if ((TCPH_FLAGS(&p->tcp) & TCP_ECE) == TCP_ECE) {
      __sync_fetch_and_add(&fs->cnt_rx_ecn, 1);
    }
  }

  /* if there is a valid ack, process it */
  if ((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK &&
      tcp_valid_rxack(fs, ack, &tx_bump) == 0)
  {
    __sync_fetch_and_add(&fs->cnt_rx_ack_bytes, tx_bump);
    if (tx_bump <= fs->tx_sent) {
      fs->tx_sent -= tx_bump;
    } else {
#ifdef ALLOW_FUTURE_ACKS
      fs->tx_next_seq += tx_bump - fs->tx_sent;
      fs->tx_next_pos += tx_bump - fs->tx_sent;
      if (fs->tx_next_pos >= fs->tx_len)
        fs->tx_next_pos -= fs->tx_len;
      fs->tx_sent = 0;
#else
      /* this should not happen */
      fprintf(stderr, "dma_krx_pkt_fastpath: acked more bytes than sent\n");
      abort();
#endif
    }

    /* duplicate ack */
    if (tx_bump != 0) {
      fs->rx_dupack_cnt = 0;
    } else if (orig_payload == 0 && ++fs->rx_dupack_cnt >= 3) {
      /* reset to last acknowledged position */
      flow_reset_retransmit(fs);
      goto unlock;
    }
  }

#ifdef FLEXNIC_PL_OOO_RECV
  /* check if we should drop this segment */
  if (tcp_trim_rxbuf(fs, seq, payload_bytes, &trim_start, &trim_end) != 0) {
    /* packet is completely outside of unused receive buffer */
    goto unlock;
  }

  /* trim payload to what we can actually use */
  payload_bytes -= trim_start + trim_end;
  payload_off += trim_start;
  oh = (struct obj_hdr *) ((uint8_t *) p + payload_off);
  seq += trim_start;

  /* handle out of order segment */
  if (seq != fs->rx_next_seq) {
    /* if there is no payload abort immediately */
    if (payload_bytes == 0) {
      goto unlock;
    }

    /* otherwise check if we can add it to the out of order interval */
    if (fs->rx_ooo_len == 0) {
      fs->rx_ooo_start = seq;
      fs->rx_ooo_len = payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, oh);
      /*fprintf(stderr, "created OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else if (seq + payload_bytes == fs->rx_ooo_start) {
      /* TODO: those two overlap checks should be more sophisticated */
      fs->rx_ooo_start = seq;
      fs->rx_ooo_len += payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, oh);
      /*fprintf(stderr, "extended OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else if (fs->rx_ooo_start + fs->rx_ooo_len == seq) {
      /* TODO: those two overlap checks should be more sophisticated */
      fs->rx_ooo_len += payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, oh);
      /*fprintf(stderr, "extended OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else {
      /*fprintf(stderr, "Sad, no luck with OOO interval (%p ooo.start=%u "
          "ooo.len=%u seq=%u bytes=%u)\n", fs, fs->rx_ooo_start,
          fs->rx_ooo_len, seq, payload_bytes);*/
    }
    goto unlock;
  }

#else
  /* check if we should drop this segment */
  if (tcp_valid_rxseq(fs, seq, payload_bytes, &trim_start, &trim_end) != 0) {
#if 0
    fprintf(stderr, "dma_krx_pkt_fastpath: packet with bad seq "
        "(got %u, expect %u, avail %u, payload %u)\n", seq, fs->rx_next_seq,
        fs->rx_avail, payload_bytes);
#endif
    goto unlock;
  }

  /* trim payload to what we can actually use */
  payload_bytes -= trim_start + trim_end;
  payload_off += trim_start;
  oh = (struct obj_hdr *) ((uint8_t *) p + payload_off);
#endif

  /* update rtt estimate */
  fs->tx_next_ts = f_beui32(opts.ts->ts_val);
  if ((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK &&
      f_beui32(opts.ts->ts_ecr) != 0)
  {
    rtt = ts - f_beui32(opts.ts->ts_ecr);
    if (fs->rtt_est != 0) {
      fs->rtt_est = (fs->rtt_est * 7 + rtt) / 8;
    } else {
      fs->rtt_est = rtt;
    }
  }


  fs->rx_remote_avail = f_beui16(p->tcp.wnd);

  /* if this is an object connection, we can be in one of three cases:
   *   - a new object starts in this segment
   *   - the current object neither starts nor ends in this segment
   *   - the current object ends in this segment
   */
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
    if (fs->rx_objrem == 0 && payload_bytes > 0) {
      /* a new object starts in this segment: make a steering decision */
      if (payload_bytes < sizeof(*oh) ||
          payload_bytes < sizeof(*oh) + oh->dstlen)
      {
        fprintf(stderr, "dma_krx_pkt_fastpath: incomplete object header "
            "(payload=%u, dl=%u)\n", payload_bytes, oh->dstlen);
        goto slowpath;
      }

      if (f_beui16(oh->magic) != OBJ_MAGIC) {
        fprintf(stderr, "dma_krx_pkt_fastpath: invalid object header magic "
            "(got=%x, expected=%x)\n", f_beui16(oh->magic), OBJ_MAGIC);
      }
      oh->magic.x = 0x0;

      /* get app state struct */
      assert(fs->db_id < FLEXNIC_PL_APPCTX_NUM);
      i = pl_memory->appctx[ctx->id][fs->db_id].appst_id;
      assert(i < FLEXNIC_PL_APPST_NUM);
      appst = &pl_memory->appst[i];
      assert(appst->ctx_num > 0);
      assert(appst->ctx_num <= FLEXNIC_PL_APPST_CTX_NUM);

      /* make steering decision */
      if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJNOHASH) == 0) {
        i = rte_hash_crc(oh->dst, oh->dstlen, 0);
        /* TODO: this division is a problem */
        i = i % appst->ctx_num;
      } else {
        steer_id = 0;
        if (oh->dstlen > 8) {
          fprintf(stderr, "dma_krx_pkt_fastpath: dstlen longer than "
              "supported (got %u, support up to 8)\n", oh->dstlen);
          goto slowpath;
        }
        memcpy(&steer_id, oh->dst, oh->dstlen);
        if (steer_id >= appst->ctx_num) {
          fprintf(stderr, "dma_krx_pkt_fastpath: steer_id larger than "
              "number of contexts (got %"PRIu64", have %u cxts)\n", steer_id,
              appst->ctx_num);
          goto slowpath;
        }
        i = steer_id;
      }
      i = appst->ctx_ids[i];
      assert(i < FLEXNIC_PL_APPCTX_NUM);
      fs->db_id = i;

      /* store how many bytes in total for this object */
      fs->rx_objrem = sizeof(*oh) + oh->dstlen + f_beui32(oh->len);
    }

    if (payload_bytes > fs->rx_objrem) {
      fprintf(stderr, "dma_krx_pkt_fastpath: more than 1 object in segment"
          " (payload=%u, objrem=%u)\n", payload_bytes, fs->rx_objrem);
      goto slowpath;
    }

    fs->rx_objrem -= payload_bytes;
  }

  /* if there is payload, dma it to the receive buffer */
  if (payload_bytes > 0) {
    flow_rx_write(fs, fs->rx_next_pos, payload_bytes, oh);

    rx_bump = payload_bytes;
    fs->rx_avail -= payload_bytes;
    fs->rx_next_pos += payload_bytes;
    if (fs->rx_next_pos >= fs->rx_len) {
      fs->rx_next_pos -= fs->rx_len;
    }
    assert(fs->rx_next_pos < fs->rx_len);
    fs->rx_next_seq += payload_bytes;
    trigger_ack = 1;

#ifdef FLEXNIC_PL_OOO_RECV
    /* if we have out of order segments, check whether buffer is continuous
     * or superfluous */
    if (fs->rx_ooo_len != 0) {
      if (tcp_trim_rxbuf(fs, fs->rx_ooo_start, fs->rx_ooo_len, &trim_start,
            &trim_end) != 0) {
          /*fprintf(stderr, "dropping ooo (%p ooo.start=%u ooo.len=%u seq=%u "
              "len=%u next_seq=%u)\n", fs, fs->rx_ooo_start, fs->rx_ooo_len, seq,
              payload_bytes, fs->rx_next_seq);*/
        /* completely superfluous: drop out of order interval */
        fs->rx_ooo_len = 0;
      } else {
        /* adjust based on overlap */
        fs->rx_ooo_start += trim_start;
        fs->rx_ooo_len -= trim_start + trim_end;
        /*fprintf(stderr, "adjusting ooo (%p ooo.start=%u ooo.len=%u seq=%u "
            "len=%u next_seq=%u)\n", fs, fs->rx_ooo_start, fs->rx_ooo_len, seq,
            payload_bytes, fs->rx_next_seq);*/
        if (fs->rx_ooo_len > 0 && fs->rx_ooo_start == fs->rx_next_seq) {
          /* yay, we caught up, make continuous and drop OOO interval */
          /*fprintf(stderr, "caught up with ooo buffer (%p start=%u len=%u)\n",
              fs, fs->rx_ooo_start, fs->rx_ooo_len);*/

          rx_bump += fs->rx_ooo_len;
          fs->rx_avail -= fs->rx_ooo_len;
          fs->rx_next_pos += fs->rx_ooo_len;
          if (fs->rx_next_pos >= fs->rx_len) {
            fs->rx_next_pos -= fs->rx_len;
          }
          assert(fs->rx_next_pos < fs->rx_len);
          fs->rx_next_seq += fs->rx_ooo_len;

          fs->rx_ooo_len = 0;
        }
      }
    }
#endif
  }
unlock:
  /* if we bumped at least one, then we need to add a notification to the
   * queue */
  if (rx_bump != 0 || tx_bump != 0) {
#if PL_DEBUG_ARX
    fprintf(stderr, "dma_krx_pkt_fastpath: updating application state\n");
#endif

#ifdef FLEXNIC_TRACING
    struct flextcp_pl_trev_arx te_arx = {
        .rx_bump = rx_bump,
        .tx_bump = tx_bump,

        .db_id = fs->db_id,

        .local_ip = f_beui32(p->ip.dest),
        .remote_ip = f_beui32(p->ip.src),
        .local_port = f_beui16(p->tcp.dest),
        .remote_port = f_beui16(p->tcp.src),
      };
    trace_event(FLEXNIC_PL_TREV_ARX, sizeof(te_arx), &te_arx);
#endif

    if (!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
      arx.type = FLEXTCP_PL_ARX_CONNUPDATE;
    } else {
      arx.type = FLEXTCP_PL_ARX_OBJUPDATE;
    }

    arx.msg.connupdate.opaque = fs->opaque;
    arx.msg.connupdate.rx_bump = rx_bump;
    arx.msg.connupdate.rx_pos = rx_pos;
    arx.msg.connupdate.tx_bump = tx_bump;

    if (fast_actx_rxq_add(ctx, actx, &arx) != 0) {
      /* TODO: how do we handle this? */
      fprintf(stderr, "dma_krx_pkt_fastpath: no space in app rx queue\n");
    }
  }

  /* Flow control: More receiver space? -> might need to start sending */
  new_avail = tcp_txavail(fs, NULL);
  if (new_avail > old_avail) {
    if (!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
      /* update qman queue */
      if (qman_set(ctx->qman_t, fs->qman_qid, fs->tx_rate, new_avail -
            old_avail, TCP_MSS, APP_QMAN_FLOW, QMAN_SET_RATE | QMAN_SET_MAXCHUNK
            | QMAN_ADD_AVAIL | QMAN_SET_OPAQUE) != 0)
      {
        fprintf(stderr, "fast_flows_packet: qman_set 1 failed, UNEXPECTED\n");
        abort();
      }
    } else if (old_avail == 0) {
      /* for object connections we only need to re-arm the qman queue if flow
       * control previously capped it to zero. */
      if (qman_set(ctx->qman_t, fs->qman_qid, 0, 1, 1, APP_QMAN_FLOW,
            QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_ADD_AVAIL |
            QMAN_SET_OPAQUE) != 0)
      {
        fprintf(stderr, "flast_flows_packet: qman_set 1 failed, UNEXPECTED\n");
        abort();
      }
    }
  }

  /* if we need to send an ack, also send packet to TX pipeline to do so */
  if (trigger_ack) {
    flow_tx_ack(ctx, fs->tx_next_seq, fs->rx_next_seq, fs->rx_avail,
        fs->tx_next_ts, ts, buf, off, nbh, opts.ts);
  }

  util_spin_unlock(&fs->lock);
  return trigger_ack;

slowpath:
  if (fs != NULL && !no_permanent_sp) {
    fs->rx_base_sp |= FLEXNIC_PL_FLOWST_SLOWPATH;
  }
  if (fs != NULL)
    util_spin_unlock(&fs->lock);
  /* TODO: should pass current flow state to kernel as well */
  return -1;
}

/* Update receive and transmit queue pointers from application */
int flast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint32_t bump_seq, uint32_t rx_tail, uint32_t tx_head,
    struct network_buf_handle *nbh, void *buf, uint32_t ts)
{
  struct flextcp_pl_flowst *fs = &pl_memory->flowst[flow_id];
  uint32_t tail, rx_avail_prev, old_avail, new_avail;
  int ret = -1;

  util_spin_lock(&fs->lock);
#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_atx te_atx = {
      .rx_tail = rx_tail,
      .tx_head = tx_head,
      .bump_seq_ent = bump_seq,
      .bump_seq_flow = fs->bump_seq,

      .local_ip = f_beui32(fs->local_ip),
      .remote_ip = f_beui32(fs->remote_ip),
      .local_port = f_beui16(fs->local_port),
      .remote_port = f_beui16(fs->remote_port),

      .flow_id = flow_id,
      .db_id = fs->db_id,

      .tx_next_pos = fs->tx_next_pos,
      .tx_next_seq = fs->tx_next_seq,
      .tx_head_prev = fs->tx_head,
      .rx_next_pos = fs->rx_next_pos,
      .rx_avail = fs->rx_avail,
      .tx_len = fs->tx_len,
      .rx_len = fs->rx_len,
      .rx_remote_avail = fs->rx_remote_avail,
      .tx_sent = fs->tx_sent,
    };
  trace_event(FLEXNIC_PL_TREV_ATX, sizeof(te_atx), &te_atx);
#endif

  /* catch out of order bumps */
  if ((bump_seq >= fs->bump_seq &&
        bump_seq - fs->bump_seq > (UINT32_MAX / 2)) ||
      (bump_seq < fs->bump_seq &&
       (fs->bump_seq < ((UINT32_MAX / 4) * 3) ||
       bump_seq > (UINT32_MAX / 4))))
  {
    goto unlock;
  }
  fs->bump_seq = bump_seq;

  /* calculate how many bytes can be sent before and after this bump */
  old_avail = tcp_txavail(fs, NULL);
  new_avail = tcp_txavail(fs, &tx_head);

  /* update queue manager queue */
  if (!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
    /* normal connection */
    if (old_avail < new_avail) {
      /* update qman queue */
      if (qman_set(ctx->qman_t, fs->qman_qid, fs->tx_rate, new_avail -
            old_avail, TCP_MSS, APP_QMAN_FLOW, QMAN_SET_RATE | QMAN_SET_MAXCHUNK
            | QMAN_ADD_AVAIL | QMAN_SET_OPAQUE) != 0)
      {
        fprintf(stderr, "flast_flows_bump: qman_set 1 failed, UNEXPECTED\n");
        abort();
      }
    }
  } else {
    /* object connection, here we only have to update the queue manager if the
     * send buffer was previously empty. */
    if (old_avail == 0 && new_avail != 0) {
      /* update qman queue */
      if (qman_set(ctx->qman_t, fs->qman_qid, 0, 1, 1, APP_QMAN_FLOW,
            QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_ADD_AVAIL |
            QMAN_SET_OPAQUE) != 0)
      {
        fprintf(stderr, "flast_flows_bump: qman_set 1 failed, UNEXPECTED\n");
        abort();
      }
    }
  }

  /* update flow state */
  fs->tx_head = tx_head;
  tail = fs->rx_next_pos + fs->rx_avail;
  if (tail >= fs->rx_len) {
    tail -= fs->rx_len;
  }
  rx_avail_prev = fs->rx_avail;
  if (rx_tail >= tail) {
    fs->rx_avail += rx_tail - tail;
  } else {
    fs->rx_avail += fs->rx_len - tail + rx_tail;
  }

  /* receive buffer freed up from empty, need to send out a window update, if
   * we're not sending anyways. */
  if (new_avail == 0 && rx_avail_prev == 0 && fs->rx_avail != 0) {
    flow_tx_segment(ctx, nbh, buf, fs, fs->tx_next_seq, fs->rx_next_seq,
        fs->rx_avail, 0, 0, fs->tx_next_ts, ts);
    ret = 0;
  }

unlock:
  util_spin_unlock(&fs->lock);
  return ret;
}

/* start retransmitting */
void fast_flows_retransmit(struct dataplane_context *ctx, uint32_t flow_id)
{
  struct flextcp_pl_flowst *fs = &pl_memory->flowst[flow_id];
  uint32_t old_avail, new_avail = -1;

  util_spin_lock(&fs->lock);

  /*    uint32_t old_head = fs->tx_head;
      uint32_t old_sent = fs->tx_sent;
      uint32_t old_pos = fs->tx_next_pos;*/

  old_avail = tcp_txavail(fs, NULL);

  if (fs->tx_sent == 0) {
    /*fprintf(stderr, "fast_flows_retransmit: tx sent == 0\n");

      fprintf(stderr, "fast_flows_retransmit: "
          "old_avail=%u new_avail=%u head=%u tx_next_seq=%u old_head=%u "
          "old_sent=%u old_pos=%u new_pos=%u\n", old_avail, new_avail,
          fs->tx_head, fs->tx_next_seq, old_head, old_sent, old_pos,
          fs->tx_next_pos);*/
    goto out;
  }


  flow_reset_retransmit(fs);
  new_avail = tcp_txavail(fs, NULL);

  /*    fprintf(stderr, "fast_flows_retransmit: "
          "old_avail=%u new_avail=%u head=%u tx_next_seq=%u old_head=%u "
          "old_sent=%u old_pos=%u new_pos=%u\n", old_avail, new_avail,
          fs->tx_head, fs->tx_next_seq, old_head, old_sent, old_pos,
          fs->tx_next_pos);*/

  /* update queue manager */
  if (new_avail > old_avail) {
    if (qman_set(ctx->qman_t, fs->qman_qid, fs->tx_rate, new_avail - old_avail,
          TCP_MSS, APP_QMAN_FLOW, QMAN_SET_RATE | QMAN_SET_MAXCHUNK |
          QMAN_ADD_AVAIL | QMAN_SET_OPAQUE) != 0)
    {
      fprintf(stderr, "flast_flows_bump: qman_set 1 failed, UNEXPECTED\n");
      abort();
    }
  }

out:
  util_spin_unlock(&fs->lock);
  return;
}

/* read `len` bytes from position `pos` in cirucular transmit buffer */
static void flow_tx_read(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, void *dst)
{
  uint32_t part;

  if (pos + len <= fs->tx_len) {
    if (dma_read(fs->tx_base + pos, len, dst) != 0) {
      fprintf(stderr, "flow_tx_read: DMA read failed addr=%"PRIx64"\n",
          fs->tx_base + pos);
      abort();
    }
  } else {
    part = fs->tx_len - pos;
    if (dma_read(fs->tx_base + pos, part, dst) != 0) {
      fprintf(stderr, "flow_tx_read: DMA read failed addr=%"PRIx64"\n",
          fs->tx_base + pos);
      abort();
    }
    if (dma_read(fs->tx_base, len - part, (uint8_t *) dst + part)
        != 0)
    {
      fprintf(stderr, "flow_tx_read: DMA read failed addr=%"PRIx64"\n",
          fs->tx_base);
      abort();
    }
  }
}

/* write `len` bytes to position `pos` in cirucular receive buffer */
static void flow_rx_write(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, const void *src)
{
  uint32_t part;
  uint64_t rx_base = fs->rx_base_sp & FLEXNIC_PL_FLOWST_RX_MASK;

  if (pos + len <= fs->rx_len) {
    if (dma_write(rx_base + pos, len, src) != 0) {
      fprintf(stderr, "flow_rx_write: DMA write failed addr=%"PRIx64"\n",
          rx_base + pos);
      abort();
    }
  } else {
    part = fs->rx_len - pos;
    if (dma_write(rx_base + pos, part, src) != 0) {
      fprintf(stderr, "flow_rx_write: DMA write failed addr=%"PRIx64"\n",
          rx_base + pos);
      abort();
    }
    if (dma_write(rx_base, len - part, (const uint8_t *) src + part)
        != 0)
    {
      fprintf(stderr, "flow_rx_write: DMA write failed addr=%"PRIx64"\n",
          rx_base);
      abort();
    }
  }
}

#ifdef FLEXNIC_PL_OOO_RECV
static void flow_rx_seq_write(struct flextcp_pl_flowst *fs, uint32_t seq,
    uint16_t len, const void *src)
{
  uint32_t diff = seq - fs->rx_next_seq;
  uint32_t pos = fs->rx_next_pos + diff;
  if (pos >= fs->rx_len)
    pos -= fs->rx_len;
  assert(pos < fs->rx_len);
  flow_rx_write(fs, pos, len, src);
}
#endif

static void flow_tx_segment(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *buf, struct flextcp_pl_flowst *fs,
    uint32_t seq, uint32_t ack, uint32_t rxwnd, uint16_t payload,
    uint32_t payload_pos, uint32_t ts_echo, uint32_t ts_my)
{
  uint16_t hdrs_len, optlen;
  struct pkt_tcp *p = buf;
  struct tcp_timestamp_opt *opt_ts;

  /* calculate header length depending on options */
  optlen = (sizeof(*opt_ts) + 3) & ~3;
  hdrs_len = sizeof(*p) + optlen;

  /* fill headers */
  p->eth.dest = fs->remote_mac;
  memcpy(&p->eth.src, &eth_addr, ETH_ADDR_LEN);
  p->eth.type = t_beui16(ETH_TYPE_IP);

  IPH_VHL_SET(&p->ip, 4, 5);
  p->ip._tos = 0;
  p->ip.len = t_beui16(hdrs_len - offsetof(struct pkt_tcp, ip) + payload);
  p->ip.id = t_beui16(3); /* TODO: not sure why we have 3 here */
  p->ip.offset = t_beui16(0);
  p->ip.ttl = 0xff;
  p->ip.proto = IP_PROTO_TCP;
  p->ip.chksum = 0;
  p->ip.src = fs->local_ip;
  p->ip.dest = fs->remote_ip;

  /* mark as ECN capable if flow marked so */
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_ECN) == FLEXNIC_PL_FLOWST_ECN) {
    IPH_ECN_SET(&p->ip, IP_ECN_ECT0);
  }

  p->tcp.src = fs->local_port;
  p->tcp.dest = fs->remote_port;
  p->tcp.seqno = t_beui32(seq);
  p->tcp.ackno = t_beui32(ack);
  TCPH_HDRLEN_FLAGS_SET(&p->tcp, 5 + optlen / 4, TCP_PSH | TCP_ACK);
  p->tcp.wnd = t_beui16(MIN(0xFFFF, rxwnd));
  p->tcp.chksum = 0;
  p->tcp.urgp = t_beui16(0);

  /* fill in timestamp option */
  memset(p + 1, 0, optlen);
  opt_ts = (struct tcp_timestamp_opt *) (p + 1);
  opt_ts->kind = TCP_OPT_TIMESTAMP;
  opt_ts->length = sizeof(*opt_ts);
  opt_ts->ts_val = t_beui32(ts_my);
  opt_ts->ts_ecr = t_beui32(ts_echo);

  /* add payload if requested */
  if (payload > 0) {
    flow_tx_read(fs, payload_pos, payload, (uint8_t *) p + hdrs_len);
  }

  /* checksums */
  tcp_checksums(nbh, p);

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_txseg te_txseg = {
      .local_ip = f_beui32(p->ip.src),
      .remote_ip = f_beui32(p->ip.dest),
      .local_port = f_beui16(p->tcp.src),
      .remote_port = f_beui16(p->tcp.dest),

      .flow_seq = seq,
      .flow_ack = ack,
      .flow_flags = TCPH_FLAGS(&p->tcp),
      .flow_len = payload,
    };
  trace_event(FLEXNIC_PL_TREV_TXSEG, sizeof(te_txseg), &te_txseg);
#endif

  tx_send(ctx, nbh, 0, hdrs_len + payload);
}

static void flow_tx_ack(struct dataplane_context *ctx, uint32_t seq,
    uint32_t ack, uint32_t rxwnd, uint32_t echots, uint32_t myts, void *buf,
    uint16_t off, struct network_buf_handle *nbh,
    struct tcp_timestamp_opt *ts_opt)
{
  struct pkt_tcp *p;
  struct eth_addr eth;
  ip_addr_t ip;
  beui16_t port;
  uint16_t hdrlen;
  uint16_t ecn_flags = 0;

  p = (struct pkt_tcp *) ((uint8_t *) buf + off);

#if PL_DEBUG_TCPACK
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u ACK: seq=%u ack=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), seq, ack);
#endif

  /* swap addresses */
  eth = p->eth.src;
  p->eth.src = p->eth.dest;
  p->eth.dest = eth;
  ip = p->ip.src;
  p->ip.src = p->ip.dest;
  p->ip.dest = ip;
  port = p->tcp.src;
  p->tcp.src = p->tcp.dest;
  p->tcp.dest = port;

  hdrlen = sizeof(*p) + (TCPH_HDRLEN(&p->tcp) - 5) * 4;

  /* If ECN flagged, set TCP response flag */
  if (IPH_ECN(&p->ip) == IP_ECN_CE) {
    ecn_flags = TCP_ECE;
  }

  /* mark ACKs as ECN in-capable */
  IPH_ECN_SET(&p->ip, IP_ECN_NONE);

  /* change TCP header to ACK */
  p->tcp.seqno = t_beui32(seq);
  p->tcp.ackno = t_beui32(ack);
  TCPH_HDRLEN_FLAGS_SET(&p->tcp, TCPH_HDRLEN(&p->tcp), TCP_ACK | ecn_flags);
  p->tcp.wnd = t_beui16(MIN(0xFFFF, rxwnd));
  p->tcp.urgp = t_beui16(0);

  /* fill in timestamp option */
  ts_opt->ts_val = t_beui32(myts);
  ts_opt->ts_ecr = t_beui32(echots);

  p->ip.len = t_beui16(hdrlen - offsetof(struct pkt_tcp, ip));
  p->ip.ttl = 0xff;

  /* checksums */
  tcp_checksums(nbh, p);

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_txack te_txack = {
      .local_ip = f_beui32(p->ip.src),
      .remote_ip = f_beui32(p->ip.dest),
      .local_port = f_beui16(p->tcp.src),
      .remote_port = f_beui16(p->tcp.dest),

      .flow_seq = seq,
      .flow_ack = ack,
      .flow_flags = TCPH_FLAGS(&p->tcp),
    };
  trace_event(FLEXNIC_PL_TREV_TXACK, sizeof(te_txack), &te_txack);
#endif

  tx_send(ctx, nbh, off, hdrlen);
}

static void flow_reset_retransmit(struct flextcp_pl_flowst *fs)
{
  uint32_t x;

  /* reset flow state as if we never transmitted those segments */
  fs->rx_dupack_cnt = 0;

  fs->tx_next_seq -= fs->tx_sent;
  if (fs->tx_next_pos >= fs->tx_sent) {
    fs->tx_next_pos -= fs->tx_sent;
  } else {
    x = fs->tx_sent - fs->tx_next_pos;
    fs->tx_next_pos = fs->tx_len - x;
  }
  fs->tx_sent = 0;

  /* cut rate by half if first drop in control interval */
  if (fs->cnt_tx_drops == 0) {
    fs->tx_rate /= 2;
  }

  __sync_fetch_and_add(&fs->cnt_tx_drops, 1);
}

static inline void tcp_checksums(struct network_buf_handle *nbh,
    struct pkt_tcp *p)
{
  p->ip.chksum = 0;
#ifdef HWXSUM_EN
  p->tcp.chksum = tx_xsum_enable(nbh, &p->ip);
#else
  p->tcp.chksum = 0;
  p->ip.chksum = rte_ipv4_cksum((void *) &p->ip);
  p->tcp.chksum = rte_ipv4_udptcp_cksum((void *) &p->ip, (void *) &p->tcp);
#endif
}

static inline uint32_t flow_hash(struct flow_key *k)
{
  MEM_BARRIER();
  return rte_hash_crc(k, sizeof(*k), 0);
}

static inline int fast_flow_lookup(ip_addr_t l_ip, ip_addr_t r_ip, beui16_t l_p,
    beui16_t r_p, struct flextcp_pl_flowst **pfs)
{
  uint32_t h, i, j, eh, fid, ffid;
  struct flow_key key;
  struct flextcp_pl_flowhte *e;
  struct flextcp_pl_flowst *fs;

  key.local_ip = l_ip;
  key.remote_ip = r_ip;
  key.local_port = l_p;
  key.remote_port = r_p;
  h = flow_hash(&key);

  for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
    i = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
    e = &pl_memory->flowht[i];

    ffid = e->flow_id;
    MEM_BARRIER();
    eh = e->flow_hash;

    fid = ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1);
    if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
      continue;
    }

    MEM_BARRIER();
    fs = &pl_memory->flowst[fid];
    if (fs->local_ip.x == key.local_ip.x &&
        fs->remote_ip.x == key.remote_ip.x &&
        fs->local_port.x == key.local_port.x &&
        fs->remote_port.x == key.remote_port.x)
    {
      *pfs = &pl_memory->flowst[fid];
      return 0;
    }
  }

  return -1;
}

void fast_flows_packet_fss(struct dataplane_context *ctx, void **bufs,
    uint16_t *offs, void **fss, uint16_t n)
{
  uint32_t hashes[n];
  uint32_t h, k, j, eh, fid, ffid;
  uint16_t i;
  struct pkt_tcp *p;
  struct flow_key key;
  struct flextcp_pl_flowhte *e;
  struct flextcp_pl_flowst *fs;

  /* calculate hashes and prefetch hash table buckets */
  for (i = 0; i < n; i++) {
    p = (struct pkt_tcp *) ((uint8_t *) bufs[i] + offs[i]);

    key.local_ip = p->ip.dest;
    key.remote_ip = p->ip.src;
    key.local_port = p->tcp.dest;
    key.remote_port = p->tcp.src;
    h = flow_hash(&key);

    rte_prefetch0(&pl_memory->flowht[h % FLEXNIC_PL_FLOWHT_ENTRIES]);
    hashes[i] = h;
  }

  /* prefetch flow state for buckets with matching hashes
   * (usually 1 per packet, except in case of collisions) */
  for (i = 0; i < n; i++) {
    h = hashes[i];
    for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
      k = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
      e = &pl_memory->flowht[k];

      ffid = e->flow_id;
      MEM_BARRIER();
      eh = e->flow_hash;

      fid = ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1);
      if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
        continue;
      }

      rte_prefetch0(&pl_memory->flowst[fid]);
    }
  }

  /* finish hash table lookup by checking 5-tuple in flow state */
  for (i = 0; i < n; i++) {
    p = (struct pkt_tcp *) ((uint8_t *) bufs[i] + offs[i]);
    fss[i] = NULL;
    h = hashes[i];

    for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
      k = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
      e = &pl_memory->flowht[k];

      ffid = e->flow_id;
      MEM_BARRIER();
      eh = e->flow_hash;

      fid = ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1);
      if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
        continue;
      }

      MEM_BARRIER();
      fs = &pl_memory->flowst[fid];
      if (fs->local_ip.x == p->ip.dest.x &&
          fs->remote_ip.x == p->ip.src.x &&
          fs->local_port.x == p->tcp.dest.x &&
          fs->remote_port.x == p->tcp.src.x)
      {
        fss[i] = &pl_memory->flowst[fid];
        break;
      }
    }
  }
}
