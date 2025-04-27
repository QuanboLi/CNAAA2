/* sr.h */
#ifndef SR_H
#define SR_H

/* provided by emulator.h: struct msg, struct pkt, tolayer3, tolayer5, starttimer, stoptimer */
#include "emulator.h"

#define BIDIRECTIONAL 1 /* 0 = A->B only; 1 = A<->B bidirectional */

/* statistics updated by SR and checked by Gradescope */
extern int TRACE;
extern int total_ACKs_received; /* from emulator */
extern int packets_resent;      /* from emulator */
extern int new_ACKs;            /* SR: number of valid ACKs received at A */
extern int packets_received;    /* SR: number of correct packets received at B */
extern int window_full;         /* SR: times A_output dropped because window was full */

/* your SR interface */
extern void A_init(void);
extern void A_output(struct msg message);
extern void A_input(struct pkt packet);
extern void A_timerinterrupt(void);

extern void B_init(void);
extern void B_input(struct pkt packet);

/* included for extension to bidirectional communication (not used) */
extern void B_output(struct msg message);
extern void B_timerinterrupt(void);

#endif /* SR_H */