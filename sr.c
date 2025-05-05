/******************************************************************************
 *  sr.c – Selective Repeat (simplex A→B) - ANSI C90 clean
 *  Compile:  gcc -Wall -ansi -pedantic -o sr emulator.c sr.c
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* --------- protocol constants -------- */
#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE (2 * WINDOWSIZE) /* at least 2·W for SR */
#define NOTINUSE (-1)

/* ---------- helpers ---------- */
static int checksum(struct pkt p)
{
    int s = p.seqnum + p.acknum;
    int k;
    for (k = 0; k < 20; ++k)
        s += (unsigned char)p.payload[k];
    return s;
}
static int corrupted(struct pkt p) { return p.checksum != checksum(p); }
static int dist(int from, int to) /* circular distance 0..SEQSPACE-1 */
{
    return (to >= from) ? (to - from) : (to + SEQSPACE - from);
}

/* =========================================================
 *                     SENDER  (A)
 * =========================================================*/
static struct pkt snd_buf[SEQSPACE];
static char snd_state[SEQSPACE]; /* 0 empty | 1 sent | 2 acked */
static int snd_base, snd_next;

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
    struct pkt p;
    int i;
    int outstanding = dist(snd_base, snd_next);

    if (outstanding >= WINDOWSIZE)
    {
        if (TRACE)
            printf("----A: window full, drop\n");
        window_full++;
        return;
    }

    /* build packet */
    p.seqnum = snd_next;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; ++i)
        p.payload[i] = message.data[i];
    p.checksum = checksum(p);

    snd_buf[p.seqnum] = p;
    snd_state[p.seqnum] = 1;

    if (TRACE > 1)
        printf("----A: send %d\n", p.seqnum);
    tolayer3(A, p);

    if (snd_base == snd_next)
        starttimer(A, RTT);

    snd_next = (snd_next + 1) % SEQSPACE;
}

void A_input(struct pkt ackpkt)
{
    int offset;

    if (corrupted(ackpkt))
    {
        if (TRACE)
            printf("----A: corrupted ACK ignored\n");
        return;
    }
    total_ACKs_received++;

    offset = dist(snd_base, ackpkt.acknum);
    if (offset >= WINDOWSIZE)
        return; /* outside window */

    if (snd_state[ackpkt.acknum] == 1)
        new_ACKs++;
    snd_state[ackpkt.acknum] = 2;

    while (snd_state[snd_base] == 2)
    {
        snd_state[snd_base] = 0;
        snd_base = (snd_base + 1) % SEQSPACE;
    }

    stoptimer(A);
    if (snd_base != snd_next)
        starttimer(A, RTT);
}

void A_timerinterrupt(void)
{
    struct pkt p;

    if (snd_state[snd_base] == 1)
    {
        p = snd_buf[snd_base];
        if (TRACE)
            printf("----A: timeout, retransmit %d\n", p.seqnum);
        tolayer3(A, p);
        packets_resent++;
    }
    stoptimer(A);
    if (snd_base != snd_next)
        starttimer(A, RTT);
}

/* =========================================================
 *                     RECEIVER  (B)
 * =========================================================*/
static struct pkt rcv_buf[SEQSPACE];
static char rcv_state[SEQSPACE]; /* 0 empty | 1 stored */
static int rcv_base;

void B_init(void)
{
    int i;
    rcv_base = 0;
    for (i = 0; i < SEQSPACE; ++i)
        rcv_state[i] = 0;
}

void B_input(struct pkt p)
{
    struct pkt ack;
    int i;
    int offset;

    if (corrupted(p))
    {
        if (TRACE)
            printf("----B: corrupted pkt discarded\n");
        return;
    }

    /* send ACK */
    ack.seqnum = NOTINUSE;
    ack.acknum = p.seqnum;
    for (i = 0; i < 20; ++i)
        ack.payload[i] = 0;
    ack.checksum = checksum(ack);
    tolayer3(B, ack);
    if (TRACE > 1)
        printf("----B: ACK %d sent\n", ack.acknum);

    offset = dist(rcv_base, p.seqnum);
    if (offset >= WINDOWSIZE)
        return; /* old pkt */

    if (rcv_state[p.seqnum] == 0)
    {
        rcv_state[p.seqnum] = 1;
        rcv_buf[p.seqnum] = p;
    }

    while (rcv_state[rcv_base])
    {
        tolayer5(B, rcv_buf[rcv_base].payload);
        packets_received++;
        rcv_state[rcv_base] = 0;
        rcv_base = (rcv_base + 1) % SEQSPACE;
    }
}

/* unused stubs */
void B_output(struct msg m) { (void)m; }
void B_timerinterrupt(void) {}