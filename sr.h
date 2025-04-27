/* sr.h */
#ifndef SR_H
#define SR_H

/* 只做前置声明，避免重复包含 emulator.h */
struct msg;
struct pkt;

/* 由 emulator.c 定义、评分脚本统计的全局量 */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;

/* 单向 A→B 接口 */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 仅占位（本作业不用） */
void B_output(struct msg message);
void B_timerinterrupt(void);

#endif /* SR_H */