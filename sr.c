/* sr.c – Selective Repeat, single-timer version (C90-clean) */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ---------- 协议参数 ---------- */
#define RTT 16.0 /* timeout interval (题目固定)   */
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* ≥ 2W                        */
#define NOTINUSE (-1)

/* ---------- 校验和 ---------- */
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
    struct pkt p;
    int i, outstanding;

    outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
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
    {
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    total_ACKs_received++;
    new_ACKs++; /* 统计有效 ACK */

    offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* ACK 不在窗口 */

    acked[p.acknum] = true;

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
    /* 只有一只 timer：重发“最早未确认”的那个分组 */
    if (TRACE)
        printf("----A: timeout, resend %d\n", A_base);

    stoptimer(A);
    tolayer3(A, snd_buf[A_base]);
    packets_resent++;
    starttimer(A, RTT);
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

    /* 该包不会被忽略 → 计数 */
    packets_received++;

    send_ack(p.seqnum); /* 任意正确包都要 ACK */

    offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* 旧包：仅重发 ACK */

    if (!rcv_mark[p.seqnum])
    {
        rcv_mark[p.seqnum] = true;
        memcpy(rcv_data[p.seqnum], p.payload, 20);
        if (TRACE > 1)
            printf("----B: buffer %d\n", p.seqnum);
    }

    /* deliver in-order & slide window */
    while (rcv_mark[B_base])
    {
        tolayer5(B, rcv_data[B_base]);
        rcv_mark[B_base] = false;
        if (TRACE > 1)
            printf("----B: deliver %d\n", B_base);
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* 占位（本作业单向） */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}