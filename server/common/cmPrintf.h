// [Palanteer] Modified version of stb_printf to replace va_list with our internal log argument list

// stb_sprintf - v1.10 - public domain snprintf() implementation
// originally by Jeff Roberts / RAD Game Tools, 2015/10/20
// http://github.com/nothings/stb

#pragma once

#include "bsVec.h"
#include "cmRecord.h"

// Custom implementation of vsnprintf with replaced va_list
int cmVsnprintf(char *buf, int count, char const *fmt, const cmRecord* record, const bsVec<cmLogParam>& va);
