/* sr.h — 只声明 SR 侧用到的接口，避免重复包含 emulator.h */
#ifndef SR_H
#define SR_H

struct msg;
struct pkt;

/* —— 评测脚本里的全局统计量 —— */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* —— Selective-Repeat 单向 (A→B) 接口 —— */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 双向占位（本作业不用改动） */
void B_output(struct msg message);
void B_timerinterrupt(void);

/* 题目始终只测单向传输，置 0 即可 */
#define BIDIRECTIONAL 0
#endif