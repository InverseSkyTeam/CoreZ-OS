#ifndef IOQUEUE_H
#define IOQUEUE_H

#include "../thread/sync.h"
#include "../thread/thread.h"
#include <stdint.h>

#define BUFSIZE 1024

struct ioqueue {
    struct lock lock;
    struct task_struct *producer;
    struct task_struct *consumer;
    char buf[BUFSIZE];
    int32_t head;
    int32_t tail;
};

void ioq_init(struct ioqueue *ioq);
int ioq_full(struct ioqueue *ioq);
int ioq_empty(struct ioqueue *ioq);
char ioq_getchar(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);
uint32_t ioq_length(struct ioqueue *ioq);

#endif
