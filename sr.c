/* sr.c : Selective Repeat implementation */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat ARQ protocol.  Adapted from J.F.Kurose
   SELECTIVE REPEAT NETWORK EMULATOR: VERSION 1.0
****************************************************************** */

/* ******************************************************************
   Macro definitions
****************************************************************** */
#define RTT 16.0     /* timer expiration interval                    */
#define WINDOWSIZE 6 /* window size                                   */
#define SEQSPACE 7   /* sequence‐number space size (≥ WINDOWSIZE+1)  */
#define NOTINUSE (-1)

/* ******************************************************************
   Checksum helpers
****************************************************************** */
static int ComputeChecksum(struct pkt packet)
{
    int checksum = 0;
    int i;

    checksum += packet.seqnum;
    checksum += packet.acknum;
    for (i = 0; i < 20; i++)
        checksum += (unsigned char)packet.payload[i];
    return checksum;
}

static bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}

/* ******************************************************************
   Sender (A)
****************************************************************** */
static struct pkt send_buffer[SEQSPACE]; /* saved but unACKed packets          */
static bool acked[SEQSPACE];             /* true if seqnum already acknowledged */
static int A_base;                       /* left edge of A’s window             */
static int A_nextseqnum;                 /* next sequence number to use         */

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
    int outstanding;
    struct pkt packet;

    /* window full? */
    outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE > 0)
            printf("----A: window full, drop message from layer5\n");
        return;
    }

    /* build packet */
    packet.seqnum = A_nextseqnum;
    packet.acknum = NOTINUSE;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(packet);

    /* buffer & send */
    send_buffer[packet.seqnum] = packet;
    acked[packet.seqnum] = false;

    if (TRACE > 1)
        printf("----A: send packet seq=%d\n", packet.seqnum);
    tolayer3(A, packet);

    /* first in window => start timer */
    if (A_base == A_nextseqnum)
        starttimer(A, RTT);

    /* advance next seqnum */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet)
{
    int acknum, offset, still_outstanding;

    /* ignore corrupted ACK */
    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: received corrupted ACK, ignore\n");
        return;
    }

    acknum = packet.acknum;
    if (TRACE > 1)
        printf("----A: ACK %d received\n", acknum);

    /* mark ACK and slide window */
    offset = (acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        acked[acknum] = true;

        while (acked[A_base])
        {
            acked[A_base] = false;
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base to %d\n", A_base);
        }

        stoptimer(A);
        still_outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
        if (still_outstanding > 0)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    int outstanding;
    int i;

    if (TRACE > 0)
        printf("----A: timeout, retransmit window\n");
    stoptimer(A);

    outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    for (i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: retransmit packet seq=%d\n", seq);
        tolayer3(A, send_buffer[seq]);

        /* first retransmission gets timer */
        if (i == 0)
            starttimer(A, RTT);
    }
}

/* ******************************************************************
   Receiver (B)
****************************************************************** */
static char rcv_payload[SEQSPACE][20]; /* buffered payloads                    */
static bool rcv_received[SEQSPACE];    /* true if seqnum already buffered      */
static int B_base;                     /* lower edge of B’s receive window     */

void B_init(void)
{
    int i;
    B_base = 0;
    for (i = 0; i < SEQSPACE; i++)
        rcv_received[i] = false;
}

void B_input(struct pkt packet)
{
    int seq, offset;

    if (!IsCorrupted(packet))
    {
        seq = packet.seqnum;
        offset = (seq - B_base + SEQSPACE) % SEQSPACE;

        if (offset < WINDOWSIZE) /* inside receive window */
        {
            struct pkt ackpkt;

            /* build ACK */
            memset(&ackpkt, 0, sizeof ackpkt);
            ackpkt.seqnum = NOTINUSE;
            ackpkt.acknum = seq;
            ackpkt.checksum = ComputeChecksum(ackpkt);

            if (TRACE > 1)
                printf("----B: send ACK %d\n", seq);
            tolayer3(B, ackpkt);

            /* buffer if first time */
            if (!rcv_received[seq])
            {
                rcv_received[seq] = true;
                memcpy(rcv_payload[seq], packet.payload, 20);
                if (TRACE > 1)
                    printf("----B: buffer packet %d\n", seq);
            }

            /* deliver any in-order packets */
            while (rcv_received[B_base])
            {
                if (TRACE > 1)
                    printf("----B: deliver payload for seq=%d\n", B_base);
                tolayer5(B, rcv_payload[B_base]);
                rcv_received[B_base] = false;
                B_base = (B_base + 1) % SEQSPACE;
            }
            return;
        }
    }

    if (TRACE > 0)
        printf("----B: packet corrupted or out of window, drop\n");
}

/* bidirectional extension (unused) */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
