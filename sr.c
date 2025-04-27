/* sr.c */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sr.h" /* 只需要 sr.h，里面 forward-decl struct */

/* ******************************************************************
   Selective Repeat ARQ  —— 单计时器实现
   基于教材 Kurose & Ross + 课程给出的 GBN 模板
****************************************************************** */

/* ---------------- 宏参数 ---------------- */
#define RTT 16.0                  /* 定时器超时 */
#define WINDOWSIZE 6              /* 发送 / 接收窗口大小 */
#define SEQSPACE (2 * WINDOWSIZE) /* SR 要求 ≥ 2·窗口 */
#define NOTINUSE (-1)

/* ---------------- Checksum ---------------- */
static int ComputeChecksum(struct pkt p)
{
    int sum = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; i++)
        sum += (unsigned char)p.payload[i];
    return sum;
}
static bool IsCorrupted(struct pkt p) { return p.checksum != ComputeChecksum(p); }

/* ******************************************************************
   发送方 A
****************************************************************** */
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

    /* 封装分组 */
    struct pkt p;
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    memcpy(p.payload, m.data, 20);
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
    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }

    /* ★ 无论重复 / 窗口外，只要未损坏都计数 */
    new_ACKs++;

    int offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    { /* 在窗口内才滑动 */
        acked[p.acknum] = true;

        while (acked[A_base])
        { /* 左边连续 ACK 完毕 → 滑窗 */
            acked[A_base] = false;
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base → %d\n", A_base);
        }

        stoptimer(A);
        if (A_base != A_next)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    if (TRACE)
        printf("----A: timeout, retransmit window\n");
    stoptimer(A);

    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;
    for (i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        if (TRACE > 1)
            printf("----A:  retransmit seq=%d\n", seq);
        tolayer3(A, send_buf[seq]);
        if (i == 0)
            starttimer(A, RTT); /* 重新启动单一计时器 */
        packets_resent++;
    }
}

/* ******************************************************************
   接收方 B
****************************************************************** */
static char rcv_buf[SEQSPACE][20];
static bool rcv_mark[SEQSPACE];
static int B_base;

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; i++)
        rcv_mark[i] = false;
}

void B_input(struct pkt p)
{
    if (IsCorrupted(p))
    { /* 丢弃损坏包 */
        if (TRACE)
            printf("----B: discard corrupt\n");
        return;
    }

    /* ★ 统计：每个未损坏包都计数（即便重复 / 窗口外仍需 ACK） */
    packets_received++;

    int seq = p.seqnum;
    int offset = (seq - B_base + SEQSPACE) % SEQSPACE;

    /* ---- 如果落在当前窗口 ---- */
    if (offset < WINDOWSIZE)
    {
        if (!rcv_mark[seq])
        { /* 首次收到，缓存 */
            rcv_mark[seq] = true;
            memcpy(rcv_buf[seq], p.payload, 20);
            if (TRACE > 1)
                printf("----B: buffer seq=%d\n", seq);
        }

        /* 回 ACK */
        struct pkt ack = {.seqnum = NOTINUSE, .acknum = seq};
        ack.checksum = ComputeChecksum(ack);
        if (TRACE > 1)
            printf("----B: ACK %d\n", seq);
        tolayer3(B, ack);

        /* 依次交付并滑窗 */
        while (rcv_mark[B_base])
        {
            tolayer5(B, rcv_buf[B_base]);
            rcv_mark[B_base] = false;
            B_base = (B_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----B: deliver seq=%d\n", (B_base + SEQSPACE - 1) % SEQSPACE);
        }
    }
    /* ---- 不在当前窗口，但可能属于“前一窗口”需重发 ACK ---- */
    else
    {
        struct pkt ack = {.seqnum = NOTINUSE, .acknum = seq};
        ack.checksum = ComputeChecksum(ack);
        if (TRACE > 1)
            printf("----B: ACK old %d\n", seq);
        tolayer3(B, ack);
    }
}

/* stubs（未用） */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}