#pragma once
#include "../tool.h"

typedef void (*EyedropperPickFn) (Pixel color, void *user_data);

Tool *eyedropper_tool_new      (void);
void  eyedropper_tool_set_callback (Tool *tool,
                                    EyedropperPickFn fn, void *user_data);
