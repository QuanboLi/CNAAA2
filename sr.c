/* sr.c – Selective Repeat implementation with timeout-safety (ANSI-C90) */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ---------------- constants ---------------- */
#define RTT 16.0 /* fixed timeout given by assignment   */
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* sequence-number space ≥ 2·WINDOWSIZE */
#define NOTINUSE (-1)

/* ---------------- helpers ------------------ */
static int ComputeChecksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; ++i)
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

/* -------------------------------------------------------------- */
void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; ++i)
        acked[i] = false;
}

/* -------------------------------------------------------------- */
void A_output(struct msg message)
{
    if (((A_next - A_base + SEQSPACE) % SEQSPACE) >= WINDOWSIZE) /* window full */
    {
        printf("----A: New message arrives, send window is full\n");
        window_full++;
        return;
    }

    printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    {
        struct pkt p;
        int i;
        p.seqnum = A_next;
        p.acknum = NOTINUSE;
        for (i = 0; i < 20; ++i)
            p.payload[i] = message.data[i];
        p.checksum = ComputeChecksum(p);

        snd_buf[p.seqnum] = p;
        acked[p.seqnum] = false;

        printf("Sending packet %d to layer 3\n", p.seqnum);
        tolayer3(A, p);
    }

    if (A_base == A_next) /* start timer only when window was empty */
        starttimer(A, RTT);

    A_next = (A_next + 1) % SEQSPACE;
}

/* -------------------------------------------------------------- */
void A_input(struct pkt p)
{
    if (IsCorrupted(p))
        return; /* silently ignore corrupted ACK */

    printf("----A: uncorrupted ACK %d is received\n", p.acknum);
    total_ACKs_received++;

    /* ACK outside current window → duplicate / old */
    if (((p.acknum - A_base + SEQSPACE) % SEQSPACE) >= WINDOWSIZE)
        return;

    if (!acked[p.acknum])
    {
        printf("----A: ACK %d is not a duplicate\n", p.acknum);
        new_ACKs++; /* *** count first-time ACK *** */
    }
    else
        printf("----A: ACK %d is a duplicate\n", p.acknum);

    acked[p.acknum] = true;

    /* slide send window forward while contiguous ACKs exist */
    while (acked[A_base])
    {
        acked[A_base] = false;
        A_base = (A_base + 1) % SEQSPACE;
    }

    stoptimer(A);
    if (A_base != A_next) /* outstanding pkts remain → restart timer */
        starttimer(A, RTT);
}

/* -------------------------------------------------------------- */
void A_timerinterrupt(void)
{
    int remain = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;

    printf("----A: time out,resend packets!\n");
    stoptimer(A);

    for (i = 0; i < remain; ++i)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
    }

    if (remain > 0) /* restart timer only if we really resent */
        starttimer(A, RTT);
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
    for (i = 0; i < 20; ++i)
        a.payload[i] = 0;
    a.checksum = ComputeChecksum(a);
    tolayer3(B, a);
}

/* -------------------------------------------------------------- */
void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        rcv_mark[i] = false;
}

/* -------------------------------------------------------------- */
void B_input(struct pkt p)
{
    int offset;

    if (IsCorrupted(p)) /* corrupted packet */
    {
        printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        send_ack((B_base + SEQSPACE - 1) % SEQSPACE);
        return;
    }

    offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE) /* out-of-window packet */
    {
        printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
        send_ack((B_base + SEQSPACE - 1) % SEQSPACE);
        return;
    }

    printf("----B: packet %d is correctly received, send ACK!\n", p.seqnum);
    packets_received++; /* *** count every valid in-window packet *** */
    send_ack(p.seqnum);

    /* buffer if first arrival */
    if (!rcv_mark[p.seqnum])
    {
        memcpy(rcv_data[p.seqnum], p.payload, 20);
        rcv_mark[p.seqnum] = true;
    }

    /* deliver in-order packets and slide receive window */
    while (rcv_mark[B_base])
    {
        tolayer5(B, rcv_data[B_base]);
        rcv_mark[B_base] = false;
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* unused stubs for future bidirectional support */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}