/* sr.h – header for Selective-Repeat – keeps original GBN trace strings */
#ifndef SR_H
#define SR_H

/* forward declarations – no second #include "emulator.h" */
struct msg;
struct pkt;

/* if emulator.c hasn't seen BIDIRECTIONAL, default to 0 (one-way A→B) */
#ifndef BIDIRECTIONAL
#define BIDIRECTIONAL 0
#endif

/* global counters defined in emulator.c (grading script uses them) */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int packets_received;

/* SR interface */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* placeholders for future bidirectional use */
void B_output(struct msg message);
void B_timerinterrupt(void);

#endif /* SR_H */