/* sr.c – Selective Repeat implementation that preserves
 *        ALL original trace strings from gbn.c (C90-clean) */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ---------------- constants ---------------- */
#define RTT 16.0 /* fixed timeout in the assignment */
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* ≥ 2·W */
#define NOTINUSE (-1)

/* ---------------- helpers ------------------ */
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
 *                              Sender  A
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
    struct pkt p; /* declare first (C90 rule) */
    int i;
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;

    if (outstanding >= WINDOWSIZE) /* ---- window full ---- */
    {
        printf("----A: New message arrives, send window is full\n");
        window_full++;
        return;
    }

    /* ---- window not full, build a new packet ---- */
    printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
        p.payload[i] = message.data[i];
    p.checksum = ComputeChecksum(p);

    snd_buf[p.seqnum] = p;
    acked[p.seqnum] = false;

    printf("Sending packet %d to layer 3\n", p.seqnum);
    tolayer3(A, p);
    if (A_base == A_next)
        starttimer(A, RTT);

    A_next = (A_next + 1) % SEQSPACE;
}

void A_input(struct pkt p)
{
    int offset;

    if (IsCorrupted(p))
        return; /* silently ignore – GBN 也不打印 */

    printf("----A: uncorrupted ACK %d is received\n", p.acknum);
    total_ACKs_received++;

    offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE) /* ACK not in window → duplicate/old */
        return;

    if (!acked[p.acknum])
        printf("----A: ACK %d is not a duplicate\n", p.acknum);

    acked[p.acknum] = true;

    /* slide window forward while contiguous ACKs exist */
    while (acked[A_base])
    {
        acked[A_base] = false;
        A_base = (A_base + 1) % SEQSPACE;
    }

    stoptimer(A);
    if (A_base != A_next)
        starttimer(A, RTT);
}

void A_timerinterrupt(void)
{
    int remain = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;

    printf("----A: time out,resend packets!\n"); /* keep original comma-no-space */
    stoptimer(A);

    for (i = 0; i < remain; i++)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
        if (i == 0)
            starttimer(A, RTT);
    }
}

/**********************************************************************
 *                              Receiver  B
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
        printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        /* resend last ACK for cumulative effect */
        send_ack((B_base + SEQSPACE - 1) % SEQSPACE);
        return;
    }

    offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;

    if (offset >= WINDOWSIZE) /* packet outside receive window */
    {
        printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        send_ack((B_base + SEQSPACE - 1) % SEQSPACE);
        return;
    }

    if (!rcv_mark[p.seqnum])
    {
        printf("----B: packet %d is correctly received, send ACK!\n", p.seqnum);
        memcpy(rcv_data[p.seqnum], p.payload, 20);
        rcv_mark[p.seqnum] = true;
    }
    else
    {
        /* duplicate packet inside window → still ACK */
        printf("----B: packet %d is correctly received, send ACK!\n", p.seqnum);
    }

    send_ack(p.seqnum);

    /* deliver in-order and slide window */
    while (rcv_mark[B_base])
    {
        tolayer5(B, rcv_data[B_base]);
        rcv_mark[B_base] = false;
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* ---- unused stubs for future bidirectional use ---- */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}