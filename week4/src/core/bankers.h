#ifndef BANKERS_H
#define BANKERS_H

#include "state.h"
#include "results.h"

SafetyResult bankers_is_safe(const SystemState *s);

#endif /* BANKERS_H */