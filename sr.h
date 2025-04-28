/* sr.h – header for Selective Repeat (debug-free version) */
#ifndef SR_H
#define SR_H

/* forward declarations – avoid re-including emulator.h */
struct msg;
struct pkt;

/* ensure BIDIRECTIONAL is visible to emulator.c */
#ifndef BIDIRECTIONAL
#define BIDIRECTIONAL 0 /* assignment only tests A→B */
#endif

/* statistics maintained by emulator.c (used for grading) */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* SR interface – unidirectional A→B */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* placeholders for possible future use */
void B_output(struct msg message);
void B_timerinterrupt(void);

#endif /* SR_H */