/* sr.h */
#ifndef SR_H
#define SR_H

/* ───── forward declarations (避免再 #include emulator.h) ───── */
struct msg;
struct pkt;

/* ───── 统计量 & trace flag（由 emulator.c 定义，SR 需要更新） ───── */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;         /* # valid ACKs counted at A */
extern int packets_received; /* # non-ignored data pkts at B */

/* ───── SR protocol interface ───── */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* stubs for future bidirectional use (未用到) */
void B_output(struct msg message);
void B_timerinterrupt(void);

#define BIDIRECTIONAL 1 /* 0 = 单向 A→B, 1 = A↔B */

#endif /* SR_H */