/* sr.h */
#ifndef SR_H
#define SR_H

/* 由 emulator.h 定义的结构体，只需前置声明 */
struct msg;
struct pkt;

/* 统计量（emulator.c 中定义，sr.c 要更新这些值） */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* SR 接口 */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 备用 */
void B_output(struct msg message);
void B_timerinterrupt(void);

#define BIDIRECTIONAL 1 /* 0 = 单向 A→B */

#endif /* SR_H */