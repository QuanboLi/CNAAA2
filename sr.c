/* sr.c – Selective Repeat, C90 clean */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ---------- 参数 ---------- */
#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE)
#define NOTINUSE (-1)

/* ---------- 校验和 ---------- */
static int ComputeChecksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int k;
    for (k = 0; k < 20; k++)
        s += (unsigned char)p.payload[k];
    return s;
}
static bool IsCorrupted(struct pkt p) { return p.checksum != ComputeChecksum(p); }

/*****************************************************************
 *                         Sender  A
 *****************************************************************/
static struct pkt snd_buf[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base;
static int A_next;

void A_init(void)
{
    int k;
    A_base = A_next = 0;
    for (k = 0; k < SEQSPACE; k++)
        acked[k] = false;
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

    /* build packet */
    struct pkt p;
    int k;
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (k = 0; k < 20; k++)
        p.payload[k] = m.data[k];
    p.checksum = ComputeChecksum(p);

    snd_buf[p.seqnum] = p; /* buffer */
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
    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }

    total_ACKs_received++;
    new_ACKs++;

    /* window-in? */
    {
        int offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
        if (offset < WINDOWSIZE)
        {
            acked[p.acknum] = true;

            while (acked[A_base])
            {
                acked[A_base] = false;
                A_base = (A_base + 1) % SEQSPACE;
                if (TRACE > 1)
                    printf("----A: slide base→%d\n", A_base);
            }

            stoptimer(A);
            if (A_base != A_next)
                starttimer(A, RTT);
        }
    }
}

void A_timerinterrupt(void)
{
    int remain = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;

    if (TRACE)
        printf("----A: timeout, resend window\n");
    stoptimer(A);

    for (i = 0; i < remain; i++)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
        if (i == 0)
            starttimer(A, RTT);
        if (TRACE > 1)
            printf("----A: retransmit %d\n", s);
    }
}

/*****************************************************************
 *                         Receiver  B
 *****************************************************************/
static char rcv_data[SEQSPACE][20];
static bool rcv_mark[SEQSPACE];
static int B_base;

static void send_ack(int seq)
{
    struct pkt a;
    int k;
    a.seqnum = NOTINUSE;
    a.acknum = seq;
    for (k = 0; k < 20; k++)
        a.payload[k] = 0;
    a.checksum = ComputeChecksum(a);
    tolayer3(B, a);
    if (TRACE > 1)
        printf("----B: ACK %d\n", seq);
}

void B_init(void)
{
    int k;
    B_base = 0;
    for (k = 0; k < SEQSPACE; k++)
        rcv_mark[k] = false;
}

void B_input(struct pkt p)
{
    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----B: corrupt, discard\n");
        return;
    }

    /* 每一个未损坏的数据包都必须 ACK */
    send_ack(p.seqnum);

    /* 判断是否需要递交/缓存 */
    {
        int offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
        if (offset >= WINDOWSIZE)
            return; /* 不在窗口，只 ACK */

        if (!rcv_mark[p.seqnum])
        {
            int k;
            rcv_mark[p.seqnum] = true;
            for (k = 0; k < 20; k++)
                rcv_data[p.seqnum][k] = p.payload[k];
            if (TRACE > 1)
                printf("----B: buffer %d\n", p.seqnum);
        }

        /* 只在真正递交前统计一次“正确接收” */
        while (rcv_mark[B_base])
        {
            packets_received++;
            tolayer5(B, rcv_data[B_base]);
            rcv_mark[B_base] = false;
            if (TRACE > 1)
                printf("----B: deliver %d\n", B_base);
            B_base = (B_base + 1) % SEQSPACE;
        }
    }
}

/* stubs (未使用) */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}