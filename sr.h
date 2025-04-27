/* sr.h */
#ifndef SR_H
#define SR_H

/* 仅做前置声明，避免重复包含 emulator.h */
struct msg;
struct pkt;

/* 统计量（由 emulator.c 定义、判分脚本读取） */
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

/* 占位：本次作业单向传输 */
void B_output(struct msg message);
void B_timerinterrupt(void);

#define BIDIRECTIONAL 1 /* 0 = A→B；1 = 开启 ACK 返回 */

#endif /* SR_H */