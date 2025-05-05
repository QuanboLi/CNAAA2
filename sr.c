#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go-Back-N Protocol (one-way data transfer)
   Adapted for unidirectional flow from Entity A to B.
   Round‐trip time must be set to 16.0 when submitting.
*******************************************************************/

#define RTT 16.0
#define WINDOW_SIZE 6
#define SEQ_SPACE (2 * WINDOW_SIZE)
#define UNUSED -1

/* Compute checksum over header fields and payload */
static int compute_checksum(const struct pkt *p)
{
    int sum = p->seqnum + p->acknum;
    for (int i = 0; i < 20; i++)
        sum += (unsigned char)p->payload[i];
    return sum;
}

/* Returns true if packet is corrupted */
static bool is_corrupted(const struct pkt *p)
{
    return (p->checksum != compute_checksum(p));
}

/******** Sender (A) state ********/
static struct pkt send_buffer[WINDOW_SIZE];
static int base_seq;     /* sequence number of oldest unacked packet */
static int next_seq;     /* next sequence number to use */
static int window_count; /* number of outstanding packets */

/* Initialize sender state */
void A_init(void)
{
    base_seq = 0;
    next_seq = 0;
    window_count = 0;
    /* Buffer contents and ack flags initialized on demand */
}

/* Called from application layer with data to send */
void A_output(struct msg message)
{
    int seq_start = base_seq;
    int seq_end = (base_seq + WINDOW_SIZE - 1) % SEQ_SPACE;

    /* Check if window has room */
    bool in_window = (seq_start <= seq_end)
                         ? (next_seq >= seq_start && next_seq <= seq_end)
                         : (next_seq >= seq_start || next_seq <= seq_end);

    if (!in_window)
    {
        if (TRACE > 0)
            printf("A_output: window full, dropping message\n");
        window_full++;
        return;
    }

    /* Build new packet */
    struct pkt pkt_to_send;
    pkt_to_send.seqnum = next_seq;
    pkt_to_send.acknum = UNUSED;
    memcpy(pkt_to_send.payload, message.data, 20);
    pkt_to_send.checksum = compute_checksum(&pkt_to_send);

    /* Buffer it */
    int buf_index = (next_seq >= seq_start)
                        ? next_seq - seq_start
                        : WINDOW_SIZE - seq_start + next_seq;
    send_buffer[buf_index] = pkt_to_send;
    window_count++;

    /* Send to network layer */
    if (TRACE > 0)
        printf("A_output: sending packet %d\n", pkt_to_send.seqnum);
    tolayer3(A, pkt_to_send);

    /* Start timer if this is the base packet */
    if (next_seq == seq_start)
    {
        starttimer(A, RTT);
    }

    /* Advance sequence number */
    next_seq = (next_seq + 1) % SEQ_SPACE;
}

/* Called when ACK arrives from B at A */
void A_input(struct pkt packet)
{
    if (is_corrupted(&packet))
    {
        if (TRACE > 0)
            printf("A_input: received corrupted ACK, ignoring\n");
        return;
    }
    if (TRACE > 0)
        printf("A_input: got ACK %d\n", packet.acknum);
    total_ACKs_received++;

    int seq_start = base_seq;
    int seq_end = (base_seq + WINDOW_SIZE - 1) % SEQ_SPACE;
    bool in_window = (seq_start <= seq_end)
                         ? (packet.acknum >= seq_start && packet.acknum <= seq_end)
                         : (packet.acknum >= seq_start || packet.acknum <= seq_end);

    if (!in_window)
    {
        /* Either duplicate for already-advanced window or outside window */
        if (TRACE > 0)
            printf("A_input: ACK %d outside window, ignoring\n", packet.acknum);
        return;
    }

    /* Map ack to buffer index */
    int idx = (packet.acknum >= seq_start)
                  ? packet.acknum - seq_start
                  : WINDOW_SIZE - seq_start + packet.acknum;

    /* If first time seeing this ACK */
    if (send_buffer[idx].acknum == UNUSED)
    {
        send_buffer[idx].acknum = packet.acknum;
        window_count--;
        new_ACKs++;
        if (TRACE > 0)
            printf("A_input: ACK %d accepted\n", packet.acknum);
    }
    else
    {
        if (TRACE > 0)
            printf("A_input: duplicate ACK %d\n", packet.acknum);
    }

    /* If this ACK is for the base, slide window */
    if (packet.acknum == seq_start)
    {
        /* Count consecutive ACKs from base */
        int shift = 0;
        for (int i = 0; i < WINDOW_SIZE; i++)
        {
            if (send_buffer[i].acknum != UNUSED)
                shift++;
            else
                break;
        }
        if (shift > 0)
        {
            /* Advance base_seq */
            base_seq = (base_seq + shift) % SEQ_SPACE;

            /* Shift buffer contents left by shift */
            for (int i = 0; i + shift < WINDOW_SIZE; i++)
            {
                send_buffer[i] = send_buffer[i + shift];
            }
        }
    }

    /* Restart or stop timer */
    stoptimer(A);
    if (window_count > 0)
    {
        starttimer(A, RTT);
    }
}

/* Timer interrupt: resend oldest unacked packet */
void A_timerinterrupt(void)
{
    if (TRACE > 0)
    {
        printf("A_timerinterrupt: timeout, resending packet %d\n",
               send_buffer[0].seqnum);
    }
    tolayer3(A, send_buffer[0]);
    packets_resent++;
    starttimer(A, RTT);
}

/******** Receiver (B) state ********/
static struct pkt recv_buffer[WINDOW_SIZE];
static int recv_base;
static int highest_index; /* highest buffered index */

/* Initialize receiver state */
void B_init(void)
{
    recv_base = 0;
    highest_index = -1;
}

/* Called when a data packet arrives at B from layer 3 */
void B_input(struct pkt packet)
{
    if (is_corrupted(&packet))
    {
        if (TRACE > 0)
            printf("B_input: corrupted packet %d, discarding\n", packet.seqnum);
        return;
    }

    if (TRACE > 0)
        printf("B_input: received packet %d, sending ACK\n", packet.seqnum);
    packets_received++;

    /* Always send ACK back */
    struct pkt ack_pkt;
    ack_pkt.seqnum = UNUSED;
    ack_pkt.acknum = packet.seqnum;
    memset(ack_pkt.payload, '0', 20);
    ack_pkt.checksum = compute_checksum(&ack_pkt);
    tolayer3(B, ack_pkt);

    /* Check if packet is inside B’s window */
    int win_start = recv_base;
    int win_end = (recv_base + WINDOW_SIZE - 1) % SEQ_SPACE;
    bool in_window = (win_start <= win_end)
                         ? (packet.seqnum >= win_start && packet.seqnum <= win_end)
                         : (packet.seqnum >= win_start || packet.seqnum <= win_end);

    if (!in_window)
    {
        if (TRACE > 0)
            printf("B_input: packet %d outside window, ignoring data\n", packet.seqnum);
        return;
    }

    /* Map to buffer index */
    int idx = (packet.seqnum >= win_start)
                  ? packet.seqnum - win_start
                  : WINDOW_SIZE - win_start + packet.seqnum;

    /* If new packet, buffer it */
    if (strcmp(recv_buffer[idx].payload, packet.payload) != 0)
    {
        recv_buffer[idx] = packet;
        if (idx > highest_index)
            highest_index = idx;
    }
    else
    {
        if (TRACE > 0)
            printf("B_input: duplicate packet %d\n", packet.seqnum);
    }

    /* If it’s the base of the window, deliver in-order and slide */
    if (packet.seqnum == recv_base)
    {
        int deliver_count = 0;
        for (int i = 0; i <= highest_index; i++)
        {
            if (strcmp(recv_buffer[i].payload, "") != 0)
            {
                tolayer5(B, recv_buffer[i].payload);
                deliver_count++;
            }
            else
            {
                break;
            }
        }
        /* Advance window */
        recv_base = (recv_base + deliver_count) % SEQ_SPACE;
        /* Shift buffer */
        for (int i = 0; i + deliver_count <= highest_index; i++)
        {
            recv_buffer[i] = recv_buffer[i + deliver_count];
        }
        highest_index -= deliver_count;
    }
}

/* Unused for simplex transfer */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}