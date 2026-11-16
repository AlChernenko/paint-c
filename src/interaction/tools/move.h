#pragma once
#include "../tool.h"
Tool *move_tool_new (void);
void move_tool_set_initial_buffer (Tool *t, PixelBuffer *buf, int x, int y);
