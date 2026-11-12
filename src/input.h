#pragma once
#include "platform.h"
void input_load_config(void);
void input_process(InputState *in);
int host_call(const char *name, int argc, double *args);
