/* sr.h */
#ifndef SR_H
#define SR_H

/* -------- 前置声明，避免重复包含 emulator.h -------- */
struct msg;
struct pkt;

/* -------- 让 emulator.c 找得到 BIDIRECTIONAL -------- */
#ifndef BIDIRECTIONAL   /* 如果外面没定义，就缺省为单向 */
#define BIDIRECTIONAL 0 /* 题目只测 A→B，所以设 0 即可   */
#endif

/* -------- 评测脚本统计用全局变量 -------- */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* -------- SR 接口 -------- */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 占位（本次作业不需要真正实现） */
void B_output(struct msg message);
void B_timerinterrupt(void);

#endif /* SR_H */