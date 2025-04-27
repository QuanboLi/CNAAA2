/* sr.h */
#ifndef SR_H
#define SR_H

/* 前置声明：不再次包含 emulator.h */
struct msg;
struct pkt;

/* 评测脚本在 emulator.c 中维护的全局统计量 */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* SR 协议接口（由 sr.c 实现） */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 备份占位（本作业单向 A→B 用不到） */
void B_output(struct msg message);
void B_timerinterrupt(void);

#define BIDIRECTIONAL 1 /* 0 = 单向 A→B */

#endif /* SR_H */