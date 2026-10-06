#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stddef.h>

#define STACK_CAPACITY 256

typedef struct {
    int data[STACK_CAPACITY];
    int top; // Index of top element (-1 when empty)
    size_t push_count;
    size_t pop_count;
} Stack;

// Function declarations
void stack_init(Stack *s);
bool stack_is_empty(const Stack *s);
bool stack_is_full(const Stack *s);
int  stack_size(const Stack *s);
bool stack_push(Stack *s, int val);
bool stack_pop(Stack *s, int *out_val);
bool stack_peek(const Stack *s, int *out_val);
void stack_dump(const Stack *s);
void stack_reset(Stack *s);

#endif // STACK_H
