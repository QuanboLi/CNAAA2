/* sr.c */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat ARQ protocol.  Adapted from J.F.Kurose
****************************************************************** */

/* ******************************************************************
   Macro definitions
****************************************************************** */
#define RTT 16.0                  /* timeout interval                     */
#define WINDOWSIZE 6              /* sliding window size                  */
#define SEQSPACE (2 * WINDOWSIZE) /* sequence-number space (>= 2⋅WINDOWSIZE) */
#define NOTINUSE (-1)             /* unused header field indicator        */

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
static struct pkt send_buffer[SEQSPACE];
static bool acked[SEQSPACE];
static int A_base;
static int A_nextseqnum;

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

    outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE > 0)
            printf("----A: window full, drop\n");
        return;
    }

    packet.seqnum = A_nextseqnum;
    packet.acknum = NOTINUSE;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(packet);

    send_buffer[packet.seqnum] = packet;
    acked[packet.seqnum] = false;
    if (TRACE > 1)
        printf("----A: send seq=%d\n", packet.seqnum);
    tolayer3(A, packet);

    if (A_base == A_nextseqnum)
        starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet)
{
    int acknum, offset, still_outstanding;

    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: corrupted ACK, ignore\n");
        return;
    }
    acknum = packet.acknum;
    if (TRACE > 1)
        printf("----A: ACK %d received\n", acknum);

    offset = (acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        /* count valid ACK */
        new_ACKs++;

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
            printf("----A: retransmit seq=%d\n", seq);
        tolayer3(A, send_buffer[seq]);
        if (i == 0)
            starttimer(A, RTT);
    }
}

/* ******************************************************************
   Receiver (B)
****************************************************************** */
static char rcv_payload[SEQSPACE][20];
static bool rcv_received[SEQSPACE];
static int B_base;

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
        if (offset < WINDOWSIZE)
        {
            /* count valid data packet */
            packets_received++;

            /* send ACK */
            {
                struct pkt ackpkt;
                memset(&ackpkt, 0, sizeof ackpkt);
                ackpkt.seqnum = NOTINUSE;
                ackpkt.acknum = seq;
                ackpkt.checksum = ComputeChecksum(ackpkt);
                if (TRACE > 1)
                    printf("----B: send ACK %d\n", seq);
                tolayer3(B, ackpkt);
            }

            if (!rcv_received[seq])
            {
                rcv_received[seq] = true;
                memcpy(rcv_payload[seq], packet.payload, 20);
                if (TRACE > 1)
                    printf("----B: buffer seq=%d\n", seq);
            }
            while (rcv_received[B_base])
            {
                if (TRACE > 1)
                    printf("----B: deliver seq=%d\n", B_base);
                tolayer5(B, rcv_payload[B_base]);
                rcv_received[B_base] = false;
                B_base = (B_base + 1) % SEQSPACE;
            }
            return;
        }
    }

    if (TRACE > 0)
        printf("----B: discard packet\n");
}

/* bidirectional stubs */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}