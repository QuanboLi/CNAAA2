#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat ARQ protocol.  Adapted from J.F.Kurose
   SELECTIVE REPEAT NETWORK EMULATOR: VERSION 1.0

   Network properties:
   - one way network delay per emulator RTT constant of 16.0 time units
   - packets can be corrupted (header or payload) or lost per user-defined probabilities
   - receiver buffers out-of-order packets and delivers them in order
   - uses individual timers per packet for selective retransmission
****************************************************************** */

/* ******************************************************************
   Macro definitions
****************************************************************** */
#define RTT 16.0      /* timer timeout interval: must be set to 16.0 */
#define WINDOWSIZE 6  /* sliding window size */
#define SEQSPACE 7    /* sequence number space size: at least WINDOWSIZE + 1 */
#define NOTINUSE (-1) /* indicator for unused header fields */

/* ******************************************************************
   generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   Computes checksum over seqnum, acknum, and payload bytes.
****************************************************************** */
static int ComputeChecksum(struct pkt packet)
{
    int checksum = 0;
    checksum += packet.seqnum;
    checksum += packet.acknum;
    for (int i = 0; i < 20; i++)
        checksum += (unsigned char)packet.payload[i];
    return checksum;
}

static bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}

/* ******************************************************************
   Sender (A) functions
****************************************************************** */
static struct pkt send_buffer[SEQSPACE]; /* buffer to store sent but unacked packets */
static bool acked[SEQSPACE];             /* acked[i] == true indicates sequence number i has been acknowledged */
static int A_base;                       /* current base sequence number of the window */
static int A_nextseqnum;                 /* next sequence number to be used by the sender */

void A_init(void)
{
    A_base = 0;
    A_nextseqnum = 0;
    for (int i = 0; i < SEQSPACE; i++)
    {
        acked[i] = false;
    }
}

void A_output(struct msg message)
{
    /* check if window is full */
    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    if (outstanding >= WINDOWSIZE)
    {
        window_full++;
        if (TRACE > 0)
            printf("----A: window full, drop message from layer5\n");
        return;
    }

    /* create packet */
    struct pkt packet;
    packet.seqnum = A_nextseqnum;
    packet.acknum = NOTINUSE;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(packet);

    /* buffer and send packet */
    send_buffer[packet.seqnum] = packet;
    acked[packet.seqnum] = false;
    if (TRACE > 1)
        printf("----A: send packet seq=%d\n", packet.seqnum);
    tolayer3(A, packet);

    /* if this is the first packet in window, start timer */
    if (A_base == A_nextseqnum)
    {
        starttimer(A, RTT);
    }

    /* update next sequence number */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet)
{
    /* only process non-corrupted ACKs */
    if (IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: received corrupted ACK, ignore\n");
        return;
    }
    int acknum = packet.acknum;
    if (TRACE > 1)
        printf("----A: ACK %d received\n", acknum);

    /* mark ACK and slide window if within range */
    int offset = (acknum - A_base + SEQSPACE) % SEQSPACE;
    if (offset < WINDOWSIZE)
    {
        acked[acknum] = true;
        /* slide window right until an unACKed packet is found */
        while (acked[A_base])
        {
            acked[A_base] = false; /* clear acknowledgement flag (optional) */
            A_base = (A_base + 1) % SEQSPACE;
            if (TRACE > 1)
                printf("----A: slide base to %d\n", A_base);
        }
        /* stop current timer; if packets still unACKed, restart */
        stoptimer(A);
        int still_outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
        if (still_outstanding > 0)
        {
            starttimer(A, RTT);
        }
    }
}

void A_timerinterrupt(void)
{
    /* timeout: retransmit all unACKed packets in window */
    if (TRACE > 0)
        printf("----A: timeout, retransmit window\n");
    stoptimer(A);
    int outstanding = (A_nextseqnum - A_base + SEQSPACE) % SEQSPACE;
    for (int i = 0; i < outstanding; i++)
    {
        int seq = (A_base + i) % SEQSPACE;
        if (TRACE > 1)
            printf("----A: retransmit packet seq=%d\n", seq);
        tolayer3(A, send_buffer[seq]);
        if (i == 0)
        {
            starttimer(A, RTT);
        }
    }
}

/* ******************************************************************
   Receiver (B) functions
****************************************************************** */
static char rcv_payload[SEQSPACE][20]; /* buffer to store received payloads */
static bool rcv_received[SEQSPACE];    /* rcv_received[i] == true indicates sequence number i has been buffered */
static int B_base;                     /* current expected sequence number at receiver */

void B_init(void)
{
    B_base = 0;
    for (int i = 0; i < SEQSPACE; i++)
    {
        rcv_received[i] = false;
    }
}

void B_input(struct pkt packet)
{
    /* if packet not corrupted and within receive window, buffer and send ACK */
    if (!IsCorrupted(packet))
    {
        int seq = packet.seqnum;
        int offset = (seq - B_base + SEQSPACE) % SEQSPACE;
        if (offset < WINDOWSIZE)
        {
            struct pkt ackpkt;
            ackpkt.seqnum = NOTINUSE;
            ackpkt.acknum = seq;
            memset(ackpkt.payload, 0, sizeof(ackpkt.payload));
            ackpkt.checksum = ComputeChecksum(ackpkt);
            if (TRACE > 1)
                printf("----B: send ACK %d\n", seq);
            tolayer3(B, ackpkt);

            /* buffer packet upon first reception */
            if (!rcv_received[seq])
            {
                rcv_received[seq] = true;
                memcpy(rcv_payload[seq], packet.payload, 20);
                if (TRACE > 1)
                    printf("----B: buffer packet %d\n", seq);
            }

            /* deliver all consecutively received data to upper layer */
            while (rcv_received[B_base])
            {
                if (TRACE > 1)
                    printf("----B: deliver payload for seq=%d\n", B_base);
                tolayer5(B, rcv_payload[B_base]);
                rcv_received[B_base] = false; /* release buffer slot */
                B_base = (B_base + 1) % SEQSPACE;
            }
            return;
        }
    }
    /* corrupted or out-of-window packets: optionally resend last ACK or discard */
    if (TRACE > 0)
        printf("----B: packet corrupted or out of window, drop\n");
}

/* bidirectional extension (not used) */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}