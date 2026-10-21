#ifndef RECOVERY_H
#define RECOVERY_H

#include "state.h"
#include "results.h"

RecoveryResult recover_terminate_all(SystemState *s);
RecoveryResult recover_terminate_one_by_one(SystemState *s);
RecoveryResult recover_preempt(SystemState *s, int victim);

#endif

