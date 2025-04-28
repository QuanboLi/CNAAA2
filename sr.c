/* sr.c – Selective Repeat ARQ implementation (ANSI C90 clean) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* -------- protocol parameters -------- */
#define RTT 16.0                  /* timeout interval */
#define WINDOWSIZE 6              /* sliding window size */
#define SEQSPACE (2 * WINDOWSIZE) /* must be ≥ 2*WINDOWSIZE */
#define NOTINUSE (-1)             /* for unused acknum */

/* -------- sender state -------- */
static struct pkt send_buffer[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base;
static int A_nextseqnum;

/* -------- receiver state -------- */
static char rcv_buffer[SEQSPACE][20];
static bool rcv_received[SEQSPACE];
static int B_base;

/* -------- checksum helpers -------- */
static int ComputeChecksum(struct pkt packet)
{
    int checksum = packet.seqnum + packet.acknum;
    int i;
    for (i = 0; i < 20; i++)
        checksum += (unsigned char)packet.payload[i];
    return checksum;
}

static bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}

/* ******************************************************************
   Sender (A) routines
****************************************************************** */

void A_init(void)
{
    int i;
    A_base = 0;
    A_nextseqnum = 0;
    for (i = 0; i < SEQSPACE; i++)
        acked[i] = false;
}

void A_output(struct msg message)
{
    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    /* 如果窗口已满就丢包 */
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE > 0)
            printf("----A: window full, drop\n");
        return;
    }

    /* 构造并发送新包 */
    struct pkt packet;
    packet.seqnum = A_nextseqnum;
    packet.acknum = NOTINUSE;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(packet);

    send_buffer[packet.seqnum] = packet;
    acked[packet.seqnum] = false;

    if (TRACE > 1)
        printf("----A: send seq=%d\n", packet.seqnum);
    tolayer3(A, packet);

    /* 如果这是窗口中第一个包，启动定时器 */
    if (A_base == A_nextseqnum)
        starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet)
{
    /* 收到 ACK */
    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    int acknum = packet.acknum;
    if (TRACE > 1)
        printf("----A: ACK %d received\n", acknum);

    /* 仅处理在窗口内的 ACK */
    int offset = (acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        new_ACKs++;
        acked[acknum] = true;
        /* 滑动窗口 */
        while (acked[A_base])
        {
            acked[A_base] = false;
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base to %d\n", A_base);
        }
        /* 重新启动或停止定时器 */
        stoptimer(A);
        int still = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
        if (still > 0)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    /* 定时器到期，重发窗口内所有未 ACK 包 */
    if (TRACE > 0)
        printf("----A: timeout, retransmit window\n");
    stoptimer(A);

    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    int i;
    for (i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: retransmit seq=%d\n", seq);
        tolayer3(A, send_buffer[seq]);
        if (i == 0)
            starttimer(A, RTT);
    }
}

/* ******************************************************************
   Receiver (B) routines
****************************************************************** */

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; i++)
        rcv_received[i] = false;
}

void B_input(struct pkt packet)
{
    /* 如果包损坏就丢弃 */
    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----B: corrupt, discard\n");
        return;
    }

    int seq = packet.seqnum;
    int offset = (seq - B_base + SEQSPACE) % SEQSPACE;

    /* 总是发送 ACK，以防 ACK 丢失 */
    {
        struct pkt ackpkt;
        int i;
        ackpkt.seqnum = NOTINUSE;
        ackpkt.acknum = seq;
        for (i = 0; i < 20; i++)
            ackpkt.payload[i] = 0;
        ackpkt.checksum = ComputeChecksum(ackpkt);
        if (TRACE > 1)
            printf("----B: send ACK %d\n", seq);
        tolayer3(B, ackpkt);
    }

    /* 如果在接收窗口内，就缓存并尝试交付 */
    if (offset < WINDOWSIZE)
    {
        if (!rcv_received[seq])
        {
            rcv_received[seq] = true;
            memcpy(rcv_buffer[seq], packet.payload, 20);
            if (TRACE > 1)
                printf("----B: buffer seq=%d\n", seq);
        }
        /* 交付所有按序到达的包 */
        while (rcv_received[B_base])
        {
            if (TRACE > 1)
                printf("----B: deliver seq=%d\n", B_base);
            tolayer5(B, rcv_buffer[B_base]);
            rcv_received[B_base] = false;
            B_base = (B_base + 1) % SEQSPACE;
        }
    }
}

/* 双向占位（本作业单向 A→B，不会调用） */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}