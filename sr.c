/* sr.c */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h" /* 只在这里包含一次 */
#include "sr.h"

/* 参数 */
#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE (-1)

/* 计数器（由 emulator.c 提供） */
extern int new_ACKs, packets_resent, packets_received, window_full;

/*---------------- 校验和与损坏检测 ----------------*/
static int ComputeChecksum(struct pkt packet)
{
    int sum = packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++)
        sum += (unsigned char)packet.payload[i];
    return sum;
}

static bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}

/*---------------- Sender (A) 状态 ----------------*/
static struct pkt send_buffer[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base;
static int A_nextseqnum;
static bool timerIsRunning;

/* 只在未运行时启动 A 端定时器 */
static void start_A_timer(void)
{
    if (!timerIsRunning)
    {
        starttimer(A, RTT);
        timerIsRunning = true;
    }
}

/* 只在运行时停止 A 端定时器 */
static void stop_A_timer(void)
{
    if (timerIsRunning)
    {
        stoptimer(A);
        timerIsRunning = false;
    }
}

void A_init(void)
{
    A_base = 0;
    A_nextseqnum = 0;
    timerIsRunning = false;
    for (int i = 0; i < SEQSPACE; i++)
        acked[i] = false;
}

/* 从应用层读到新消息，就发包 */
void A_output(struct msg message)
{
    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE > 0)
            printf("----A: window full, drop app msg\n");
        return;
    }

    struct pkt p;
    memcpy(p.payload, message.data, 20);
    p.seqnum = A_nextseqnum;
    p.acknum = NOTINUSE;
    p.checksum = ComputeChecksum(p);

    send_buffer[p.seqnum] = p;
    acked[p.seqnum] = false;
    if (TRACE > 1)
        printf("----A: send seq=%d\n", p.seqnum);
    tolayer3(A, p);

    /* 第一个包启动定时器 */
    if (A_base == p.seqnum)
        start_A_timer();

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
}

/* 收到 ACK 时滑动窗口并控制定时器 */
void A_input(struct pkt packet)
{
    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: corrupted ACK ignore\n");
        return;
    }
    if (TRACE > 1)
        printf("----A: ACK %d received\n", packet.acknum);

    int acknum = packet.acknum;
    int offset = (acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        new_ACKs++;
        acked[acknum] = true;
        while (acked[A_base])
        {
            acked[A_base] = false;
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base to %d\n", A_base);
        }
        /* 窗口空则停，否则重启 */
        stop_A_timer();
        int still = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
        if (still > 0)
            start_A_timer();
    }
}

/* 超时中断：只重传并重启，不停定时器 */
void A_timerinterrupt(void)
{
    if (TRACE > 0)
        printf("----A: timeout, retransmit window\n");

    /* *** 不要调用 stop_A_timer() *** */
    timerIsRunning = false;

    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    for (int i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        packets_resent++;
        if (TRACE > 1)
            printf("----A: retransmit seq=%d\n", seq);
        tolayer3(A, send_buffer[seq]);
        if (i == 0)
            start_A_timer();
    }
}

/*---------------- Receiver (B) 状态 ----------------*/
static char rcv_payload[SEQSPACE][20];
static bool rcv_received[SEQSPACE];
static int B_base;

void B_init(void)
{
    B_base = 0;
    for (int i = 0; i < SEQSPACE; i++)
        rcv_received[i] = false;
}

void B_input(struct pkt packet)
{
    if (!IsCorrupted(packet))
    {
        int seq = packet.seqnum;
        int offset = (seq - B_base + SEQSPACE) % SEQSPACE;
        if (offset < WINDOWSIZE)
        {
            /* 发送 ACK */
            struct pkt ackp;
            ackp.seqnum = NOTINUSE;
            ackp.acknum = seq;
            memset(ackp.payload, 0, sizeof ackp.payload);
            ackp.checksum = ComputeChecksum(ackp);
            if (TRACE > 1)
                printf("----B: send ACK %d\n", seq);
            tolayer3(B, ackp);

            if (!rcv_received[seq])
            {
                packets_received++;
                rcv_received[seq] = true;
                memcpy(rcv_payload[seq], packet.payload, 20);
                if (TRACE > 1)
                    printf("----B: buffer %d\n", seq);
            }
            while (rcv_received[B_base])
            {
                if (TRACE > 1)
                    printf("----B: deliver %d\n", B_base);
                tolayer5(B, rcv_payload[B_base]);
                rcv_received[B_base] = false;
                B_base = (B_base + 1) % SEQSPACE;
            }
            return;
        }
    }
    if (TRACE > 0)
        printf("----B: drop seq=%d\n", packet.seqnum);
}

/* 双向扩展，留空 */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}