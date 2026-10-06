#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#define QUEUE_CAPACITY 128
#define INSTRUCTION_MAX_LEN 128

typedef struct {
    char items[QUEUE_CAPACITY][INSTRUCTION_MAX_LEN];
    int front;
    int rear;
    int count;
    size_t total_enqueued;
    size_t total_dequeued;
} InstructionQueue;

// Function declarations
void queue_init(InstructionQueue *q);
bool queue_is_empty(const InstructionQueue *q);
bool queue_is_full(const InstructionQueue *q);
int  queue_size(const InstructionQueue *q);
bool queue_enqueue(InstructionQueue *q, const char *instruction);
bool queue_dequeue(InstructionQueue *q, char *out_instruction, size_t max_len);
bool queue_peek(const InstructionQueue *q, char *out_instruction, size_t max_len);
void queue_dump(const InstructionQueue *q);
void queue_reset(InstructionQueue *q);

#endif // QUEUE_H
