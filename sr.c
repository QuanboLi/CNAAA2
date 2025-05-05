/* sr.c – Single-timer Selective Repeat (SR) protocol, C90 compliant
 *
 * Implements unidirectional reliable data transfer from A → B
 * on top of the network emulator provided in the practical.
 *
 * Trace strings are kept IDENTICAL to those in the starter code;
 * do not change them – the autograder performs a textual diff.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h" /* supplied by the course staff */
#include "sr.h"

/* ───────── protocol parameters (fixed for submission) ───────── */
#define WINDOWSIZE 6              /* sender / receiver window */
#define SEQSPACE (2 * WINDOWSIZE) /* SR requirement ≥ 2·W      */
#define RTT 16.0                  /* single timer interval     */
#define NOTINUSE (-1)             /* placeholder for fields    */

/* ───────── checksum helpers ───────── */
static int ComputeChecksum(struct pkt p)
{
    int sum = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; ++i)
        sum += (unsigned char)p.payload[i];
    return sum;
}

static bool IsCorrupted(struct pkt p)
{
    return (p.checksum != ComputeChecksum(p));
}

/* ====================================================================== */
/*                                Sender A                                */
/* ====================================================================== */

static struct pkt snd_buf[SEQSPACE]; /* buffer of all outstanding pkts */
static bool acked[SEQSPACE];         /* true once ACK has arrived      */
static int A_base;                   /* seq# of earliest un-ACKed pkt   */
static int A_next;                   /* next sequence number to use     */

void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; ++i)
        acked[i] = false;
}

/* Application layer hands one 20-byte message to the transport layer */
void A_output(struct msg m)
{
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    { /* window full → drop */
        if (TRACE)
            printf("----A: window full, drop\n");
        window_full++;
        return;
    }

    /* Build packet */
    struct pkt p;
    int i;
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; ++i)
        p.payload[i] = m.data[i];
    p.checksum = ComputeChecksum(p);

    /* Buffer and send */
    snd_buf[p.seqnum] = p;
    acked[p.seqnum] = false;

    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);

    /* Start timer if this is the first un-ACKed packet */
    if (A_base == A_next)
        starttimer(A, RTT);

    A_next = (A_next + 1) % SEQSPACE;
}

/* Called when an ACK arrives from B */
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

    offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE) /* ACK outside current window */
        return;

    if (!acked[p.acknum])
    {
        acked[p.acknum] = true;
        new_ACKs++;
    }
    else if (TRACE > 1)
    {
        printf("----A: duplicate ACK %d\n", p.acknum);
    }

    /* Slide window forward over any newly ACKed consecutive pkts */
    while (acked[A_base])
    {
        acked[A_base] = false; /* recycle slot for future use */
        A_base = (A_base + 1) % SEQSPACE;
    }

    /* Restart timer if we still have un-ACKed packets */
    stoptimer(A);
    if (A_base != A_next)
        starttimer(A, RTT);
}

/* Timer interrupt – resend every un-ACKed packet in the window */
void A_timerinterrupt(void)
{
    if (TRACE)
        printf("----A: timeout, resend window\n");

    stoptimer(A);

    /* Number of outstanding packets */
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;
    for (i = 0; i < outstanding; ++i)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
        if (TRACE > 1)
            printf("----A: retransmit %d\n", s);
    }

    if (outstanding > 0)
        starttimer(A, RTT);
}

/* ====================================================================== */
/*                               Receiver B                               */
/* ====================================================================== */

static struct pkt rcv_buf[SEQSPACE]; /* buffer of out-of-order pkts  */
static bool rcv_mark[SEQSPACE];      /* true once payload stored     */
static int B_base;                   /* seq# of earliest not-delivered */

static void send_ack(int seq)
{
    struct pkt a;
    int i;
    a.seqnum = NOTINUSE; /* not used in an ACK */
    a.acknum = seq;
    for (i = 0; i < 20; ++i)
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
    for (i = 0; i < SEQSPACE; ++i)
        rcv_mark[i] = false;
}

/* Called whenever a packet arrives from A */
void B_input(struct pkt p)
{
    int offset;

    if (IsCorrupted(p))
    {
        if (TRACE)
            printf("----B: corrupt, discard\n");
        return;
    }

    /* Always send an ACK back */
    send_ack(p.seqnum);

    /* Is the packet inside the current receive window? */
    offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE) /* old packet – ACK already covers it */
        return;

    /* First time we see this seq# → buffer it */
    if (!rcv_mark[p.seqnum])
    {
        rcv_mark[p.seqnum] = true;
        memcpy(rcv_buf[p.seqnum].payload, p.payload, 20);
        packets_received++;
        if (TRACE > 1)
            printf("----B: buffer %d\n", p.seqnum);
    }

    /* Deliver any in-order data, slide window */
    while (rcv_mark[B_base])
    {
        tolayer5(B, rcv_buf[B_base].payload);
        rcv_mark[B_base] = false; /* free slot */
        if (TRACE > 1)
            printf("----B: deliver %d\n", B_base);
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* Bidirectional stubs (not used in this practical) */
void B_output(struct msg message) { /* empty */ }
void B_timerinterrupt(void) { /* empty */ }