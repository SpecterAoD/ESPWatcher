#pragma once

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#warning "Using config.example.h. Create include/config.local.h for real deployment."
#endif
