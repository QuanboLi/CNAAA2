/* sr.h – header for Selective-Repeat (here: GBN-compat) */
#ifndef SR_H
#define SR_H

/* forward declarations – 避免再次包含 emulator.h */
struct msg;
struct pkt;

/* 全局统计量由 emulator.c 定义，协议例程须更新这些变量 */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;

/* 协议接口（单向 A→B） */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 占位 – 本实验不需要真正实现 */
void B_output(struct msg message);
void B_timerinterrupt(void);

/* 该作业始终只测单向 A→B */
#define BIDIRECTIONAL 0

#endif /* SR_H */