/******************************************************************************
 *  sr.c – Selective Repeat (simplex A→B) implementation
 *  Course: Computer Networking – Reliable Transport Practical
 *  Compile:  gcc -Wall -ansi -pedantic -o sr emulator.c sr.c
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ---------------- configuration ---------------- */
#define RTT 16.0                  /* one single timer value (required)    */
#define WINDOWSIZE 6              /* sender / receiver window size        */
#define SEQSPACE (2 * WINDOWSIZE) /* SR requires ≥ 2·W sequence numbers   */
#define NOTINUSE (-1)

/* ---------------- checksum helpers ------------- */
static int checksum(struct pkt p)
{
    int cs = p.seqnum + p.acknum;
    int i;
    for (i = 0; i < 20; ++i)
        cs += (unsigned char)p.payload[i];
    return cs;
}
static int corrupted(struct pkt p) { return p.checksum != checksum(p); }

/* handy distance within circular sequence space: val in 0..SEQSPACE-1 */
static int dist(int from, int to)
{
    return (to >= from) ? (to - from) : (to + SEQSPACE - from);
}

/* =========================================================
 *                    SENDER  (entity A)
 * =========================================================*/
static struct pkt snd_buf[SEQSPACE]; /* saved copies of outstanding packets  */
static char snd_state[SEQSPACE];     /* 0 empty | 1 sent-unacked | 2 acked */
static int snd_base;                 /* seq# of left edge of sender window  */
static int snd_next;                 /* next seq# to use for new data       */

void A_init(void)
{
    int i;
    snd_base = 0;
    snd_next = 0;
    for (i = 0; i < SEQSPACE; ++i)
        snd_state[i] = 0;
}

void A_output(struct msg message)
{
    int outstanding = dist(snd_base, snd_next);

    if (outstanding >= WINDOWSIZE)
    {
        if (TRACE)
            printf("----A: window full, message dropped\n");
        window_full++;
        return;
    }

    /* build packet (variables declared at top for C90) */
    struct pkt p;
    int i;

    p.seqnum = snd_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; ++i)
        p.payload[i] = message.data[i];
    p.checksum = checksum(p);

    /* store & mark */
    snd_buf[p.seqnum] = p;
    snd_state[p.seqnum] = 1; /* in-flight */

    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);

    if (snd_base == snd_next)
        starttimer(A, RTT);

    snd_next = (snd_next + 1) % SEQSPACE;
}

void A_input(struct pkt ackpkt)
{
    if (corrupted(ackpkt))
    {
        if (TRACE)
            printf("----A: corrupted ACK ignored\n");
        return;
    }
    total_ACKs_received++;

    /* ignore acks outside current window */
    if (dist(snd_base, ackpkt.acknum) >= WINDOWSIZE)
        return;

    if (snd_state[ackpkt.acknum] == 1)
        new_ACKs++;
    snd_state[ackpkt.acknum] = 2; /* mark ACKed */

    /* slide window over consecutive ACKed packets */
    while (snd_state[snd_base] == 2)
    {
        snd_state[snd_base] = 0; /* clear slot */
        snd_base = (snd_base + 1) % SEQSPACE;
    }

    /* (re)manage timer */
    stoptimer(A);
    if (snd_base != snd_next)
        starttimer(A, RTT);
}

void A_timerinterrupt(void)
{
    /* retransmit ONLY the left-edge (oldest unACKed) packet */
    if (snd_state[snd_base] == 1)
    {
        struct pkt p = snd_buf[snd_base];
        if (TRACE)
            printf("----A: timeout, retransmit %d\n", p.seqnum);
        tolayer3(A, p);
        packets_resent++;
    }
    /* restart timer if something is still unACKed */
    stoptimer(A);
    if (snd_base != snd_next)
        starttimer(A, RTT);
}

/* =========================================================
 *                    RECEIVER (entity B)
 * =========================================================*/
static struct pkt rcv_buf[SEQSPACE]; /* out-of-order buffer                 */
static char rcv_state[SEQSPACE];     /* 0 not arrived | 1 stored           */
static int rcv_base;                 /* seq# expected next by upper layer   */

void B_init(void)
{
    int i;
    rcv_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        rcv_state[i] = 0;
}

void B_input(struct pkt p)
{
    int i, offset;
    struct pkt ack;

    if (corrupted(p))
    {
        if (TRACE)
            printf("----B: corrupted pkt, discard\n");
        return;
    }

    /* always ACK a correct packet */
    ack.seqnum = NOTINUSE;
    ack.acknum = p.seqnum;
    for (i = 0; i < 20; ++i)
        ack.payload[i] = 0;
    ack.checksum = checksum(ack);
    tolayer3(B, ack);
    if (TRACE > 1)
        printf("----B: ACK %d sent\n", ack.acknum);

    /* check if within receiver window */
    offset = dist(rcv_base, p.seqnum);
    if (offset >= WINDOWSIZE)
        return; /* old pkt – already delivered */

    /* buffer if first arrival */
    if (rcv_state[p.seqnum] == 0)
    {
        rcv_state[p.seqnum] = 1;
        rcv_buf[p.seqnum] = p;
    }

    /* deliver consecutive packets & slide window */
    while (rcv_state[rcv_base])
    {
        tolayer5(B, rcv_buf[rcv_base].payload);
        packets_received++;
        rcv_state[rcv_base] = 0;
        rcv_base = (rcv_base + 1) % SEQSPACE;
    }
}

/* stubs required by sr.h but unused in simplex mode */
void B_output(struct msg m) { (void)m; }
void B_timerinterrupt(void) {}