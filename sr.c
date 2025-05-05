/********************************************************************
 *  sr.c  –  Selective Repeat (simplex A→B) implementation
 *  Target: ANSI C90, compile with
 *      gcc -Wall -ansi -pedantic -o sr emulator.c sr.c
 *******************************************************************/
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ---------------- configuration ---------------- */
#define RTT 16.0                  /* single timer value required by prac */
#define WINDOWSIZE 6              /* sliding-window size */
#define SEQSPACE (2 * WINDOWSIZE) /* sequence-number space (≥ 2W) */
#define NOTINUSE (-1)

/* ---------------- checksum helpers ------------- */
static int compute_checksum(struct pkt p)
{
    int csum = p.seqnum + p.acknum;
    /* payload is exactly 20 bytes */
    int i;
    for (i = 0; i < 20; ++i)
        csum += (unsigned char)p.payload[i];
    return csum;
}

static int is_corrupted(struct pkt p)
{
    return p.checksum != compute_checksum(p);
}

/* =========================================================
 *                      SENDER  A
 * =========================================================*/
static struct pkt snd_buf[SEQSPACE]; /* circular buffer of sent packets      */
static char snd_state[SEQSPACE];     /* 0 = empty, 1 = sent-unacked, 2 = acked*/
static int snd_base;                 /* seq # of left edge of sender window  */
static int snd_next;                 /* next seq # to use                    */
static int timer_seq;                /* seq # that current timer belongs to  */

/* ---------- helper: distance within sequence space -------- */
static int seq_distance(int from, int to)
/* return (to - from) in modulo SEQSPACE, value in 0..SEQSPACE-1 */
{
    if (to >= from)
        return to - from;
    return to + SEQSPACE - from;
}

/* ---------------- interface functions ---------------------*/
void A_init(void)
{
    int i;
    snd_base = 0;
    snd_next = 0;
    timer_seq = NOTINUSE;
    for (i = 0; i < SEQSPACE; ++i)
    {
        snd_state[i] = 0;
    }
}

void A_output(struct msg message)
{
    /* check window fullness */
    if (seq_distance(snd_base, snd_next) >= WINDOWSIZE)
    {
        if (TRACE)
            printf("----A: window full, message dropped\n");
        window_full++;
        return;
    }

    /* build packet */
    struct pkt p;
    int i;
    p.seqnum = snd_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; ++i)
        p.payload[i] = message.data[i];
    p.checksum = compute_checksum(p);

    /* buffer & mark state */
    snd_buf[p.seqnum] = p;
    snd_state[p.seqnum] = 1; /* sent, not yet acked */

    /* send */
    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);

    /* if this is the first unacked packet, start timer */
    if (snd_base == p.seqnum)
    {
        starttimer(A, RTT);
        timer_seq = p.seqnum;
    }

    /* advance next seq */
    snd_next = (snd_next + 1) % SEQSPACE;
}

void A_input(struct pkt ackpkt)
{
    if (is_corrupted(ackpkt))
    {
        if (TRACE)
            printf("----A: corrupted ACK ignored\n");
        return;
    }
    total_ACKs_received++;

    /* is ack within window? */
    int dist = seq_distance(snd_base, ackpkt.acknum);
    if (dist >= WINDOWSIZE)
    {
        if (TRACE)
            printf("----A: ACK %d outside window\n", ackpkt.acknum);
        return;
    }

    /* mark acked if first time */
    if (snd_state[ackpkt.acknum] == 1)
    {
        snd_state[ackpkt.acknum] = 2; /* acked */
        new_ACKs++;
        if (TRACE > 1)
            printf("----A: ACK %d accepted\n", ackpkt.acknum);
    }
    else
    {
        if (TRACE > 1)
            printf("----A: duplicate ACK %d\n", ackpkt.acknum);
    }

    /* slide window forward over continuous ACKs */
    while (snd_state[snd_base] == 2)
    {
        snd_state[snd_base] = 0; /* clear slot */
        snd_base = (snd_base + 1) % SEQSPACE;
    }

    /* (re)start or stop timer as needed */
    stoptimer(A);
    timer_seq = NOTINUSE;
    if (snd_base != snd_next)
    {
        starttimer(A, RTT);
        timer_seq = snd_base;
    }
}

void A_timerinterrupt(void)
{
    if (snd_base == snd_next)
        return; /* nothing outstanding */

    /* retransmit the oldest unacked packet (= snd_base) */
    struct pkt p = snd_buf[snd_base];
    if (TRACE)
        printf("----A: timeout, retransmit %d\n", p.seqnum);
    tolayer3(A, p);
    packets_resent++;

    /* restart timer for the same packet */
    stoptimer(A);
    starttimer(A, RTT);
    timer_seq = snd_base;
}

/* =========================================================
 *                      RECEIVER  B
 * =========================================================*/
static struct pkt rcv_buf[SEQSPACE]; /* buffer for out-of-order pkts        */
static char rcv_state[SEQSPACE];     /* 0 = not recvd, 1 = recvd-not-deliv  */
static int rcv_base;                 /* seq # the receiver is waiting for   */

void B_init(void)
{
    int i;
    rcv_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        rcv_state[i] = 0;
}

void B_input(struct pkt p)
{
    if (is_corrupted(p))
    {
        if (TRACE)
            printf("----B: corrupted packet discarded\n");
        return;
    }

    /* always send ACK for correctly received packet */
    struct pkt ack;
    int i;
    ack.seqnum = NOTINUSE;
    ack.acknum = p.seqnum;
    for (i = 0; i < 20; ++i)
        ack.payload[i] = 0;
    ack.checksum = compute_checksum(ack);
    tolayer3(B, ack);
    if (TRACE > 1)
        printf("----B: ACK %d sent\n", ack.acknum);

    /* check if in receive window */
    int dist = seq_distance(rcv_base, p.seqnum);
    if (dist >= WINDOWSIZE)
    {
        /* old packet from previous window – already delivered */
        return;
    }

    /* buffer if first time */
    if (rcv_state[p.seqnum] == 0)
    {
        rcv_state[p.seqnum] = 1;
        rcv_buf[p.seqnum] = p;
    }

    /* deliver all in-order packets starting from rcv_base */
    while (rcv_state[rcv_base])
    {
        tolayer5(B, rcv_buf[rcv_base].payload);
        packets_received++;
        rcv_state[rcv_base] = 0;
        rcv_base = (rcv_base + 1) % SEQSPACE;
    }
}

/* stubs required by sr.h in simplex mode */
void B_output(struct msg m) { (void)m; }
void B_timerinterrupt(void) {}