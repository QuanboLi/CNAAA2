/*  sr.c – Selective Repeat implementation, clean C90
 *
 *  –  one global timer (A-side) as required by the emulator
 *  –  sender and receiver windows of size WINDOWSIZE
 *  –  sequence-number space ≥ 2·WINDOWSIZE  (textbook rule)
 *  –  all I/O strings unchanged so the autograder can match them
 *
 *  build: gcc -Wall -ansi -pedantic -o sr emulator.c sr.c
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/*--------------------------------------------------------------------
 *                     compile-time parameters
 *------------------------------------------------------------------*/
#define RTT 16.0                  /* one global timeout (given)   */
#define WINDOWSIZE 6              /* sliding-window size          */
#define SEQSPACE (2 * WINDOWSIZE) /* ≥ 2·WINDOWSIZE               */
#define NOTINUSE (-1)             /* marks unused hdr field       */
#define PAYLOADSIZE 20

/*--------------------------------------------------------------------
 *                       checksum helpers
 *------------------------------------------------------------------*/
static int checksum(struct pkt p)
/* textbook additive checksum – good enough for the simulator            */
{
    int s = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < PAYLOADSIZE; ++i)
        s += (unsigned char)p.payload[i];
    return s;
}
static bool corrupted(struct pkt p) { return p.checksum != checksum(p); }

/*--------------------------------------------------------------------
 *                             Sender A
 *------------------------------------------------------------------*/
static struct pkt snd_buf[SEQSPACE]; /* circular buffer of sent pkts  */
static bool acked[SEQSPACE];         /* true ⇒ pkt already ACKed     */
static int A_base;                   /* seq# of left edge of window   */
static int A_next;                   /* seq# to be assigned next      */

void A_init(void)
{
    int i;
    A_base = A_next = 0;
    for (i = 0; i < SEQSPACE; ++i)
        acked[i] = false;
}

void A_output(struct msg m)
/* called by layer-5 when a new message is ready                         */
{
    int outstanding = (A_next - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    { /* window full – drop   */
        window_full++;
        if (TRACE)
            printf("----A: window full, drop\n");
        return;
    }

    /* build packet */
    struct pkt p;
    int i;
    p.seqnum = A_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < PAYLOADSIZE; ++i)
        p.payload[i] = m.data[i];
    p.checksum = checksum(p);

    /* send & book-keeping */
    snd_buf[p.seqnum] = p;
    acked[p.seqnum] = false;
    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);
    if (A_base == A_next)
        starttimer(A, RTT); /* start (or restart)  */

    A_next = (A_next + 1) % SEQSPACE;
}

void A_input(struct pkt p)
/* ACK arrives from B                                                    */
{
    if (corrupted(p))
    { /* ignore bad ACKs      */
        if (TRACE)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    total_ACKs_received++;

    int offset = (p.acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* ACK outside window   */

    acked[p.acknum] = true; /* mark it ACKed        */

    /* slide window over any contiguous ACKed pkts */
    while (acked[A_base])
    {
        acked[A_base] = false; /* re-use slot later    */
        A_base = (A_base + 1) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: slide base→%d\n", A_base);
    }

    /* (re)arm timer if something still outstanding                      */
    stoptimer(A);
    if (A_base != A_next)
        starttimer(A, RTT);
}

void A_timerinterrupt(void)
/* timeout – re-send every packet currently in the window                */
{
    int remaining = (A_next - A_base + SEQSPACE) % SEQSPACE;
    int i;

    if (TRACE)
        printf("----A: timeout, resend window\n");
    stoptimer(A); /* cancel expired timer */

    for (i = 0; i < remaining; ++i)
    {
        int s = (A_base + i) % SEQSPACE;
        tolayer3(A, snd_buf[s]);
        packets_resent++;
        if (TRACE > 1)
            printf("----A: retransmit %d\n", s);
        if (i == 0)
            starttimer(A, RTT); /* one timer for window */
    }
}

/*--------------------------------------------------------------------
 *                            Receiver B
 *------------------------------------------------------------------*/
static char rcv_data[SEQSPACE][PAYLOADSIZE];
static bool present[SEQSPACE]; /* true ⇒ pkt stored     */
static int B_base;             /* lower edge of window  */

static void send_ack(int seq)
/* helper: build & send an ACK                                           */
{
    struct pkt a;
    int i;
    a.seqnum = NOTINUSE;
    a.acknum = seq;
    for (i = 0; i < PAYLOADSIZE; ++i)
        a.payload[i] = 0;
    a.checksum = checksum(a);
    tolayer3(B, a);
    if (TRACE > 1)
        printf("----B: ACK %d\n", seq);
}

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        present[i] = false;
}

void B_input(struct pkt p)
/* data packet arrives from A                                            */
{
    if (corrupted(p))
    { /* drop corrupted packet */
        if (TRACE)
            printf("----B: corrupt, discard\n");
        return;
    }

    send_ack(p.seqnum); /* always ACK – even dup */

    int offset = (p.seqnum - B_base + SEQSPACE) % SEQSPACE;
    if (offset >= WINDOWSIZE)
        return; /* pkt outside window    */

    /* buffer if new                                                     */
    if (!present[p.seqnum])
    {
        present[p.seqnum] = true;
        memcpy(rcv_data[p.seqnum], p.payload, PAYLOADSIZE);
        if (TRACE > 1)
            printf("----B: buffer %d\n", p.seqnum);
    }

    /* deliver in-order data and slide window                            */
    while (present[B_base])
    {
        tolayer5(B, rcv_data[B_base]);
        present[B_base] = false;
        if (TRACE > 1)
            printf("----B: deliver %d\n", B_base);
        B_base = (B_base + 1) % SEQSPACE;
    }
}

/* stubs (bidirectional not required) */
void B_output(struct msg m) {}
void B_timerinterrupt(void) {}