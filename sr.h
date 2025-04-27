/* sr.h */
#ifndef SR_H
#define SR_H

/* forward declarations so compiler recognises the structs */
struct msg;
struct pkt;

/* simulator-exported statistics & trace flag */
extern int TRACE;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;
extern int window_full;

/* protocol interface — provided by your sr.c */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* (unused stubs for bidirectional extension) */
void B_output(struct msg message);
void B_timerinterrupt(void);

#define BIDIRECTIONAL 1 /* keep original definition */

#endif /* SR_H */