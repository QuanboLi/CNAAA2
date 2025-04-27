/* sr.c – Selective Repeat implementation (C90-clean) */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* -------- constants -------- */
#define RTT 16.0 /* 题目固定 */
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* ≥2W */
#define NOTINUSE (-1)

/* -------- checksum -------- */
static int ComputeChecksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; i++)
        s += (unsigned char)p.payload[i];
    return s;
}
static bool IsCorrupted(struct pkt p) { return p.checksum != ComputeChecksum(p); }

/**********************************************************************
 *                              Sender A
 *********************************************************************/
static struct pkt snd_buf[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base, A_next;

void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; i++)
        acked[i] = false;
}

void A_output(struct msg message)
{
    struct pkt p; /* — 所有局部先声明 (C90) — */
    int i, outstanding;

    outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    { /* window full */
        window_full++;
        if (TRACE)
            printf("----A: window full, drop\n");
        return;
    }

    /* build packet */
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
        p.payload[i] = message.data[i];
    p.checksum = ComputeChecksum(p);

    snd_buf[p.seqnum] = p;
    acked[p.seqnum] = false;

    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);
    if (A_base == A_next)
        starttimer(A, RTT);

    A_next = (A_next + 1) % SEQSPACE;
}

void A_input(struct pkt p)
{
    int offset;

    if (IsCorrupted(p))
    { /* ignore */
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    total_ACKs_received++; /* 评分脚本统计 */

    offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* ACK 不在窗口 */

    acked[p.acknum] = true; /* mark */

    while (acked[A_base])
    { /* slide window */
        acked[A_base] = false;
        A_base = (A_base + 1) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: slide base→%d\n", A_base);
    }

    stoptimer(A);
    if (A_base != A_next)
        starttimer(A, RTT);
}

void A_timerinterrupt(void)
{
    int i, remain = (A_next - A_base + SEQSPACE) % SEQSPACE;

    if (TRACE)
        printf("----A: timeout, resend window\n");
    stoptimer(A);

    for (i = 0; i < remain; i++)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
        if (TRACE > 1)
            printf("----A: retransmit %d\n", s);
        if (i == 0)
            starttimer(A, RTT);
    }
}

/**********************************************************************
 *                              Receiver B
 *********************************************************************/
static char rcv_data[SEQSPACE][20];
static bool rcv_mark[SEQSPACE];
static int B_base;

static void send_ack(int seq)
{
    struct pkt a;
    int i;
    a.seqnum = NOTINUSE;
    a.acknum = seq;
    for (i = 0; i < 20; i++)
        a.payload[i] = 0;
    a.checksum = ComputeChecksum(a);
    tolayer3(B, a);
    if (TRACE > 1)
        printf("----B: ACK %d\n", seq);
}

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; i++)
        rcv_mark[i] = false;
}

void B_input(struct pkt p)
{
    int offset;

    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----B: corrupt, discard\n");
        return;
    }
    send_ack(p.seqnum); /* always ACK */

    offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* old packet */

    if (!rcv_mark[p.seqnum])
    {
        rcv_mark[p.seqnum] = true;
        memcpy(rcv_data[p.seqnum], p.payload, 20);
        if (TRACE > 1)
            printf("----B: buffer %d\n", p.seqnum);
    }

    /* deliver in-order & slide */
    while (rcv_mark[B_base])
    {
        tolayer5(B, rcv_data[B_base]);
        rcv_mark[B_base] = false;
        if (TRACE > 1)
            printf("----B: deliver %d\n", B_base);
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* unused stubs */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}