#include "queue.h"
#include <stdio.h>
#include <string.h>

void queue_init(InstructionQueue *q) {
    if (!q) return;
    q->front = 0;
    q->rear = -1;
    q->count = 0;
    q->total_enqueued = 0;
    q->total_dequeued = 0;
    memset(q->items, 0, sizeof(q->items));
}

bool queue_is_empty(const InstructionQueue *q) {
    return (!q || q->count == 0);
}

bool queue_is_full(const InstructionQueue *q) {
    return (q && q->count >= QUEUE_CAPACITY);
}

int queue_size(const InstructionQueue *q) {
    return q ? q->count : 0;
}

bool queue_enqueue(InstructionQueue *q, const char *instruction) {
    if (!q || !instruction) return false;
    if (queue_is_full(q)) {
        fprintf(stderr, "[Queue Error] Queue Overflow! Capacity is %d\n", QUEUE_CAPACITY);
        return false;
    }
    q->rear = (q->rear + 1) % QUEUE_CAPACITY;
    strncpy(q->items[q->rear], instruction, INSTRUCTION_MAX_LEN - 1);
    q->items[q->rear][INSTRUCTION_MAX_LEN - 1] = '\0';
    q->count++;
    q->total_enqueued++;
    return true;
}

bool queue_dequeue(InstructionQueue *q, char *out_instruction, size_t max_len) {
    if (!q || !out_instruction || max_len == 0) return false;
    if (queue_is_empty(q)) {
        fprintf(stderr, "[Queue Error] Queue Underflow! Queue is empty\n");
        return false;
    }
    strncpy(out_instruction, q->items[q->front], max_len - 1);
    out_instruction[max_len - 1] = '\0';
    q->front = (q->front + 1) % QUEUE_CAPACITY;
    q->count--;
    q->total_dequeued++;
    return true;
}

bool queue_peek(const InstructionQueue *q, char *out_instruction, size_t max_len) {
    if (!q || !out_instruction || max_len == 0) return false;
    if (queue_is_empty(q)) return false;
    strncpy(out_instruction, q->items[q->front], max_len - 1);
    out_instruction[max_len - 1] = '\0';
    return true;
}

void queue_dump(const InstructionQueue *q) {
    if (!q) return;
    printf("\n--- Ready/Instruction Queue (Size: %d / %d) ---\n", q->count, QUEUE_CAPACITY);
    if (queue_is_empty(q)) {
        printf("[Queue Empty]\n");
    } else {
        int idx = q->front;
        for (int i = 0; i < q->count; i++) {
            printf("[%03d] %s \"%s\"\n", i, (i == 0 ? "-> FRONT:" : (i == q->count - 1 ? "   REAR: " : "         ")), q->items[idx]);
            idx = (idx + 1) % QUEUE_CAPACITY;
        }
    }
    printf("Stats: Enqueued = %zu, Dequeued = %zu\n", q->total_enqueued, q->total_dequeued);
    printf("--------------------------------------------\n\n");
}

void queue_reset(InstructionQueue *q) {
    queue_init(q);
}
