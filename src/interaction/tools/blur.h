#pragma once
#include "../tool.h"

Tool *blur_tool_new        (void);
void  blur_tool_set_radius (Tool *tool, int radius);
int   blur_tool_get_radius (Tool *tool);
