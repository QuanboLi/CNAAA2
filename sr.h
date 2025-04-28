/* sr.h – 只声明 Selective Repeat 接口，避免重复包含 emulator.h */
#ifndef SR_H
#define SR_H

/* 前置声明（emulator.h 内部已定义） */
struct msg;
struct pkt;

/* 如果外部没定义，缺省认为是单向 (A→B) */
#ifndef BIDIRECTIONAL
#define BIDIRECTIONAL 0
#endif

/* 由 emulator.c 定义，评分脚本会读取 */
extern int TRACE;
extern int window_full;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;

/* —— SR 单向接口 —— */
void A_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);

void B_init(void);
void B_input(struct pkt packet);

/* 占位：本次作业用不到 */
void B_output(struct msg message);
void B_timerinterrupt(void);

#endif /* SR_H */