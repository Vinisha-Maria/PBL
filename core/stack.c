#include "stack.h"
#include <stdio.h>
#include <string.h>

void stack_init(Stack *s) {
    if (!s) return;
    s->top = -1;
    memset(s->data, 0, sizeof(s->data));
    s->push_count = 0;
    s->pop_count = 0;
}

bool stack_is_empty(const Stack *s) {
    return (!s || s->top < 0);
}

bool stack_is_full(const Stack *s) {
    return (s && s->top >= STACK_CAPACITY - 1);
}

int stack_size(const Stack *s) {
    return s ? (s->top + 1) : 0;
}

bool stack_push(Stack *s, int val) {
    if (!s) return false;
    if (stack_is_full(s)) {
        fprintf(stderr, "[Stack Error] Stack Overflow! Capacity is %d\n", STACK_CAPACITY);
        return false;
    }
    s->top++;
    s->data[s->top] = val;
    s->push_count++;
    return true;
}

bool stack_pop(Stack *s, int *out_val) {
    if (!s || !out_val) return false;
    if (stack_is_empty(s)) {
        fprintf(stderr, "[Stack Error] Stack Underflow! Stack is empty\n");
        return false;
    }
    *out_val = s->data[s->top];
    s->top--;
    s->pop_count++;
    return true;
}

bool stack_peek(const Stack *s, int *out_val) {
    if (!s || !out_val) return false;
    if (stack_is_empty(s)) {
        fprintf(stderr, "[Stack Error] Cannot peek into empty stack\n");
        return false;
    }
    *out_val = s->data[s->top];
    return true;
}

void stack_dump(const Stack *s) {
    if (!s) return;
    printf("\n--- Stack Dump (Size: %d / %d) ---\n", stack_size(s), STACK_CAPACITY);
    if (stack_is_empty(s)) {
        printf("[Empty Stack]\n");
    } else {
        for (int i = s->top; i >= 0; i--) {
            printf("[%03d] %s %d\n", i, (i == s->top ? "-> TOP: " : "        "), s->data[i]);
        }
    }
    printf("Stats: Pushes = %zu, Pops = %zu\n", s->push_count, s->pop_count);
    printf("----------------------------------\n\n");
}

void stack_reset(Stack *s) {
    stack_init(s);
}
