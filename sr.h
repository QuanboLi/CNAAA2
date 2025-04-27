#ifndef SR_H
#define SR_H

/* provided by emulator.h: struct msg, struct pkt, tolayer3, tolayer5, starttimer, stoptimer */

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