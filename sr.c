/* sr.c – Selective Repeat 文件，但实现保持与课程提供的 GBN 完全一致
 * 语法：ANSI-C90
 */
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* --------------------------------------------------------------------------------- */
/*                               可调常量                                             */
#define RTT 16.0                  /* 题目要求的超时常量                                  */
#define WINDOWSIZE 6              /* 题目固定窗口 W                                      */
#define SEQSPACE (2 * WINDOWSIZE) /* 按要求 ≥2W                                     */
#define NOTINUSE (-1)

/* --------------------------------------------------------------------------------- */
/*                               工具函数                                             */
static int checksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; ++i)
        s += (unsigned char)p.payload[i];
    return s;
}
static int corrupt(struct pkt p) { return checksum(p) != p.checksum; }

/* --------------------------------------------------------------------------------- */
/*                               发送端 A                                             */
static struct pkt snd_buf[SEQSPACE];
static int base, nextseqnum;

void A_init(void)
{
    int i;
    base = nextseqnum = 0;
    for (i = 0; i < SEQSPACE; ++i)
        memset(&snd_buf[i], 0, sizeof snd_buf[i]);
}

void A_output(struct msg message)
{
    int outstanding = (nextseqnum - base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    { /* 窗口已满 */
        window_full++;
        if (TRACE)
            printf("---A: window full, drop\n");
        return;
    }

    /* 封装分组 */
    {
        struct pkt p;
        int i;
        p.seqnum = nextseqnum;
        p.acknum = NOTINUSE;
        for (i = 0; i < 20; ++i)
            p.payload[i] = message.data[i];
        p.checksum = checksum(p);

        snd_buf[p.seqnum] = p;

        if (TRACE > 1)
            printf("---A: send pkt %d\n", p.seqnum);
        tolayer3(A, p);
        if (base == nextseqnum) /* 启动定时器 */
            starttimer(A, RTT);
    }

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt p)
{
    if (corrupt(p))
    { /* 忽略损坏 ACK */
        if (TRACE)
            printf("---A: corrupt ACK ignored\n");
        return;
    }
    total_ACKs_received++;

    /* 若为累计确认且在窗口内，滑动 base */
    if (((p.acknum - base + SEQSPACE) % SEQSPACE) <
        ((nextseqnum - base + SEQSPACE) % SEQSPACE))
    {

        base = (p.acknum + 1) % SEQSPACE;
        if (TRACE > 1)
            printf("---A: ACK %d, base -> %d\n", p.acknum, base);

        stoptimer(A);
        if (base != nextseqnum)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    /* 重发窗口内所有未确认分组 */
    int i, win = (nextseqnum - base + SEQSPACE) % SEQSPACE;

    if (TRACE)
        printf("---A: timeout, resend window (base=%d, next=%d)\n",
               base, nextseqnum);

    stoptimer(A);
    for (i = 0; i < win; ++i)
    {
        int idx = (base + i) % SEQSPACE;
        tolayer3(A, snd_buf[idx]);
        packets_resent++;
        if (TRACE > 1)
            printf("---A: retransmit %d\n", idx);
    }
    if (win)
        starttimer(A, RTT);
}

/* --------------------------------------------------------------------------------- */
/*                               接收端 B                                             */
static int expectedseqnum;

void B_init(void) { expectedseqnum = 0; }

void B_input(struct pkt p)
{
    if (corrupt(p))
    {
        if (TRACE)
            printf("---B: corrupt packet discarded\n");
        return;
    }

    /* 正确的 in-order 分组？ */
    if (p.seqnum == expectedseqnum)
    {
        /* 交付给层 5 */
        tolayer5(B, p.payload);
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        if (TRACE > 1)
            printf("---B: deliver %d\n", p.seqnum);
    }
    else
    {
        if (TRACE > 1)
            printf("---B: out-of-order %d (expect %d) – ignored\n",
                   p.seqnum, expectedseqnum);
    }

    /* 发送累积 ACK = last in-order pkt */
    {
        struct pkt ack;
        memset(&ack, 0, sizeof ack);
        ack.seqnum = NOTINUSE;
        ack.acknum = (expectedseqnum + SEQSPACE - 1) % SEQSPACE;
        ack.checksum = checksum(ack);
        tolayer3(B, ack);
        if (TRACE > 1)
            printf("---B: ACK %d\n", ack.acknum);
    }
}

/* --------------------------------------------------------------------------------- */
/*                            双向占位函数（未用到）                                   */
void B_output(struct msg m) {} /* not used */
void B_timerinterrupt(void) {} /* not used */