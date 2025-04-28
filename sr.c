/* sr.c — Selective-Repeat implementation, C90-clean */
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* —— 参数 —— */
#define RTT 16.0 /* 题目固定超时 */
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* ≥ 2W */
#define NOTINUSE (-1)

/* —— 校验和 —— */
static int checksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; ++i)
        s += (unsigned char)p.payload[i];
    return s;
}
static int corrupted(struct pkt p) { return checksum(p) != p.checksum; }

/***********************************************************************
 *                               Sender A
 **********************************************************************/
static struct pkt snd_buf[SEQSPACE];
static char acked[SEQSPACE];
static int A_base, A_next;

void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; ++i)
        acked[i] = 0;
}

/* 上层来报文 */
void A_output(struct msg m)
{
    int in_flight = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (in_flight >= WINDOWSIZE)
    { /* window full */
        window_full++;
        if (TRACE)
            printf("--A: window full; message dropped\n");
        return;
    }

    /* 组包 */
    {
        struct pkt p;
        int i;
        p.seqnum = A_next;
        p.acknum = NOTINUSE;
        for (i = 0; i < 20; ++i)
            p.payload[i] = m.data[i];
        p.checksum = checksum(p);

        snd_buf[p.seqnum] = p;
        acked[p.seqnum] = 0;

        if (TRACE > 1)
            printf("--A: send packet %d\n", p.seqnum);
        tolayer3(A, p);
        if (A_base == A_next)
            starttimer(A, RTT);

        A_next = (A_next + 1) % SEQSPACE;
    }
}

/* 收到 ACK */
void A_input(struct pkt p)
{
    if (corrupted(p))
    {
        if (TRACE)
            printf("--A: ***corrupted ACK***\n");
        return;
    }
    total_ACKs_received++;
    new_ACKs++;

    /* 是否在当前窗口内？*/
    {
        int off = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
        if (off >= WINDOWSIZE)
            return; /* 旧 ACK */

        acked[p.acknum] = 1;

        while (acked[A_base])
        { /* 滑动窗口 */
            acked[A_base] = 0;
            ++A_base;
            A_base %= SEQSPACE;
        }

        stoptimer(A);
        if (A_base != A_next)
            starttimer(A, RTT);
    }
}

/* 超时 —— 只重传 base 分组 */
void A_timerinterrupt(void)
{
    struct pkt p = snd_buf[A_base];

    if (TRACE)
        printf("--A: timeout, resend packet %d\n", p.seqnum);

    tolayer3(A, p);
    packets_resent++;
    starttimer(A, RTT); /* 重新计时 */
}

/***********************************************************************
 *                               Receiver B
 **********************************************************************/
static char rcvd_mark[SEQSPACE];
static char rcvd_data[SEQSPACE][20];
static int B_base;

static void send_ack(int num)
{
    struct pkt a;
    int i;
    a.seqnum = NOTINUSE;
    a.acknum = num;
    for (i = 0; i < 20; ++i)
        a.payload[i] = 0;
    a.checksum = checksum(a);

    tolayer3(B, a);
    if (TRACE > 1)
        printf("--B: send ACK %d\n", num);
}

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        rcvd_mark[i] = 0;
}

void B_input(struct pkt p)
{
    if (corrupted(p))
    {
        if (TRACE)
            printf("--B: ***corrupted pkt***\n");
        return;
    }
    packets_received++;
    send_ack(p.seqnum); /* 对所有正确分组 ACK */

    /* 是否落在接收窗口？*/
    {
        int off = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
        if (off >= WINDOWSIZE)
            return; /* 早期已交付过，纯 ACK */

        if (!rcvd_mark[p.seqnum])
        {
            int i;
            rcvd_mark[p.seqnum] = 1;
            for (i = 0; i < 20; ++i)
                rcvd_data[p.seqnum][i] = p.payload[i];
            if (TRACE > 1)
                printf("--B: buffer %d (in-order=%s)\n",
                       p.seqnum, off == 0 ? "yes" : "no");
        }

        /* 交付尽可能多的连续报文 */
        while (rcvd_mark[B_base])
        {
            tolayer5(B, rcvd_data[B_base]);
            rcvd_mark[B_base] = 0;
            if (TRACE > 1)
                printf("--B: deliver %d\n", B_base);
            ++B_base;
            B_base %= SEQSPACE;
        }
    }
}

/* 双向占位 */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}