/* sr.c */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "emulator.h" /* 必须先包含，拿到 struct pkt 定义等 */
#include "sr.h"

/* ---------- 参数 ---------- */
#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE)
#define NOTINUSE (-1)

/* ---------- 校验和 ---------- */
static int ComputeChecksum(struct pkt p)
{
    int sum = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; i++)
        sum += (unsigned char)p.payload[i];
    return sum;
}
static bool IsCorrupted(struct pkt p) { return p.checksum != ComputeChecksum(p); }

/********************************************************************
 *                             Sender A
 ********************************************************************/
static struct pkt send_buf[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base;
static int A_next;

void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; i++)
        acked[i] = false;
}

void A_output(struct msg m)
{
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE)
            printf("----A: window full, drop\n");
        return;
    }

    /* 组包 */
    struct pkt p;
    int i;
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
        p.payload[i] = m.data[i];
    p.checksum = ComputeChecksum(p);

    send_buf[p.seqnum] = p;
    acked[p.seqnum] = false;

    if (TRACE > 1)
        printf("----A: send seq=%d\n", p.seqnum);
    tolayer3(A, p);

    if (A_base == A_next)
        starttimer(A, RTT);
    A_next = (A_next + 1) % SEQSPACE;
}

void A_input(struct pkt p)
{
    int offset, still;

    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    /* 任何未损坏 ACK 都计数 */
    new_ACKs++;

    offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        acked[p.acknum] = true;

        while (acked[A_base])
        {
            acked[A_base] = false;
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base → %d\n", A_base);
        }

        stoptimer(A);
        still = (A_next - A_base + SEQSPACE) % SEQSPACE;
        if (still > 0)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;

    if (TRACE)
        printf("----A: timeout, retransmit window\n");
    stoptimer(A);

    for (i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: retransmit seq=%d\n", seq);
        tolayer3(A, send_buf[seq]);
        packets_resent++;
        if (i == 0)
            starttimer(A, RTT); /* 单计时器重新启动 */
    }
}

/********************************************************************
 *                             Receiver B
 ********************************************************************/
static char rbuf[SEQSPACE][20];
static bool rmark[SEQSPACE];
static int B_base;

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; i++)
        rmark[i] = false;
}

static void send_ack(int seq)
{
    struct pkt ack;
    int i;

    ack.seqnum = NOTINUSE;
    ack.acknum = seq;
    for (i = 0; i < 20; i++)
        ack.payload[i] = 0;
    ack.checksum = ComputeChecksum(ack);

    if (TRACE > 1)
        printf("----B: ACK %d\n", seq);
    tolayer3(B, ack);
}

void B_input(struct pkt p)
{
    int seq, offset, i;

    if (IsCorrupted(p))
    { /* 丢弃损坏包 */
        if (TRACE)
            printf("----B: discard corrupt\n");
        return;
    }

    /* 任何未损坏数据包均计数 */
    packets_received++;

    seq = p.seqnum;
    offset = (seq - B_base + SEQSPACE) % SEQSPACE;

    /* ---------- 当前窗口内 ---------- */
    if (offset < WINDOWSIZE)
    {
        if (!rmark[seq])
        {
            rmark[seq] = true;
            memcpy(rbuf[seq], p.payload, 20);
            if (TRACE > 1)
                printf("----B: buffer seq=%d\n", seq);
        }
        send_ack(seq); /* 正常 ACK */

        /* 交付并滑窗 */
        while (rmark[B_base])
        {
            tolayer5(B, rbuf[B_base]);
            rmark[B_base] = false;
            B_base = (B_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----B: deliver seq=%d\n",
                       (B_base + SEQSPACE - 1) % SEQSPACE);
        }
    }
    /* ---------- 窗口外（老窗口） ---------- */
    else
    {
        if (TRACE > 1)
            printf("----B: ACK old %d\n", seq);
        send_ack(seq); /* 仍需 ACK，可能上次 ACK 丢失 */
    }
}

/* stubs 未用 */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}