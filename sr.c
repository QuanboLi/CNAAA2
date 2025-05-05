#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOW_SIZE 6
#define SEQ_SPACE (2 * WINDOW_SIZE)
#define UNUSED_FIELD (-1)

static double TIMER_INTERVAL = RTT;

/* Packet buffer for A and acknowledgement buffer flags */
static struct pkt send_buffer[WINDOW_SIZE];
static int send_base = 0;
static int next_seq = 0;
static int unacked_count = 0;

/* Packet buffer for B */
static struct pkt recv_buffer[WINDOW_SIZE];
static int recv_base = 0;
static int highest_indexed = -1;

/* -------------------------------------------------------------------
 * Utility: compute and check checksum
 * -------------------------------------------------------------------
 */
static int compute_checksum(const struct pkt *p)
{
    int sum = p->seqnum + p->acknum;
    for (int i = 0; i < 20; i++)
        sum += (unsigned char)p->payload[i];
    return sum;
}

static bool is_corrupt(const struct pkt *p)
{
    return (p->checksum != compute_checksum(p));
}

/* -------------------------------------------------------------------
 * A-side logic
 * -------------------------------------------------------------------
 */

/* Initialize sender A */
void A_init()
{
    send_base = 0;
    next_seq = 0;
    unacked_count = 0;
    memset(send_buffer, 0, sizeof(send_buffer));
}

/* Send a new message if window not full */
void A_output(struct msg message)
{
    int window_end = (send_base + WINDOW_SIZE - 1) % SEQ_SPACE;
    bool in_window = (send_base <= window_end)
                         ? (next_seq >= send_base && next_seq <= window_end)
                         : (next_seq >= send_base || next_seq <= window_end);

    if (!in_window)
    {
        if (TRACE > 0)
            printf("A_output: window full, dropping message\n");
        return;
    }

    /* Build packet */
    struct pkt p;
    p.seqnum = next_seq;
    p.acknum = UNUSED_FIELD;
    memcpy(p.payload, message.data, 20);
    p.checksum = compute_checksum(&p);

    /* Buffer it */
    int buf_idx = (next_seq - send_base + SEQ_SPACE) % SEQ_SPACE;
    send_buffer[buf_idx] = p;
    unacked_count++;

    /* Send and maybe start timer */
    if (TRACE > 0)
        printf("A_output: sending packet %d\n", p.seqnum);
    tolayer3(A, p);
    if (next_seq == send_base)
        starttimer(A, TIMER_INTERVAL);

    next_seq = (next_seq + 1) % SEQ_SPACE;
}

/* Handle incoming ACKs at A */
void A_input(struct pkt ack_pkt)
{
    if (is_corrupt(&ack_pkt))
    {
        if (TRACE > 0)
            printf("A_input: received corrupt ACK\n");
        return;
    }

    int ack = ack_pkt.acknum;
    if (TRACE > 0)
        printf("A_input: ACK %d received\n", ack);

    /* Check if ACK is within current window */
    int window_end = (send_base + WINDOW_SIZE - 1) % SEQ_SPACE;
    bool valid_ack = (send_base <= window_end)
                         ? (ack >= send_base && ack <= window_end)
                         : (ack >= send_base || ack <= window_end);

    if (!valid_ack)
        return;

    /* Mark buffer slot as acked */
    int idx = (ack - send_base + SEQ_SPACE) % SEQ_SPACE;
    if (send_buffer[idx].acknum == UNUSED_FIELD)
    {
        send_buffer[idx].acknum = ack;
        unacked_count--;
    }

    /* Slide window if base was acknowledged */
    if (ack == send_base)
    {
        int shift = 1;
        while (shift < WINDOW_SIZE && send_buffer[shift].acknum != UNUSED_FIELD)
        {
            shift++;
        }
        /* Advance base and shift buffer down */
        send_base = (send_base + shift) % SEQ_SPACE;
        for (int i = 0; i + shift < WINDOW_SIZE; i++)
        {
            send_buffer[i] = send_buffer[i + shift];
        }
        /* Clear out new slots */
        for (int i = WINDOW_SIZE - shift; i < WINDOW_SIZE; i++)
        {
            send_buffer[i].acknum = UNUSED_FIELD;
        }
        /* Restart timer if needed */
        stoptimer(A);
        if (unacked_count > 0)
            starttimer(A, TIMER_INTERVAL);
    }
}

/* Timeout: retransmit oldest unacked packet */
void A_timerinterrupt()
{
    if (TRACE > 0)
        printf("A_timerinterrupt: timeout, retransmitting pkt %d\n",
               send_buffer[0].seqnum);
    tolayer3(A, send_buffer[0]);
    starttimer(A, TIMER_INTERVAL);
}

/* -------------------------------------------------------------------
 * B-side logic
 * -------------------------------------------------------------------
 */

/* Initialize receiver B */
void B_init()
{
    recv_base = 0;
    highest_indexed = -1;
    memset(recv_buffer, 0, sizeof(recv_buffer));
}

/* Handle incoming data at B */
void B_input(struct pkt data_pkt)
{
    if (is_corrupt(&data_pkt))
    {
        if (TRACE > 0)
            printf("B_input: received corrupt packet\n");
        return;
    }

    int seq = data_pkt.seqnum;
    if (TRACE > 0)
        printf("B_input: packet %d received\n", seq);

    /* Always ACK back */
    struct pkt ack;
    ack.seqnum = UNUSED_FIELD;
    ack.acknum = seq;
    memset(ack.payload, '0', sizeof(ack.payload));
    ack.checksum = compute_checksum(&ack);
    tolayer3(B, ack);

    /* Check if within recv window */
    int window_end = (recv_base + WINDOW_SIZE - 1) % SEQ_SPACE;
    bool in_window = (recv_base <= window_end)
                         ? (seq >= recv_base && seq <= window_end)
                         : (seq >= recv_base || seq <= window_end);

    if (!in_window)
        return;

    /* Buffer non-duplicates */
    int idx = (seq - recv_base + SEQ_SPACE) % SEQ_SPACE;
    if (recv_buffer[idx].acknum != seq)
    {
        recv_buffer[idx] = data_pkt;
        recv_buffer[idx].acknum = seq;
        highest_indexed = (highest_indexed < idx ? idx : highest_indexed);
    }

    /* Deliver in-order packets */
    if (seq == recv_base)
    {
        int deliver_count = 0;
        while (deliver_count <= highest_indexed && recv_buffer[deliver_count].acknum >= 0)
        {
            tolayer5(B, recv_buffer[deliver_count].payload);
            deliver_count++;
        }
        /* Slide receive window */
        recv_base = (recv_base + deliver_count) % SEQ_SPACE;
        for (int i = 0; i + deliver_count < WINDOW_SIZE; i++)
        {
            recv_buffer[i] = recv_buffer[i + deliver_count];
        }
        highest_indexed -= deliver_count;
    }
}

/* Unused in simplex scenario */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}