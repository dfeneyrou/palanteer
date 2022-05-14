// Palanteer viewer
// Copyright (C) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

// External
#include "imgui.h"

// Internal
#include "bs.h"
#include "cmConst.h"

class vwConst {
 public:

    // Application constants
    static constexpr int    CACHE_MB_MIN              = 50;       // Minimum and maximum cache size (MB)
    static constexpr int    CACHE_MB_MAX              = 1000;
    static constexpr bsUs_t ANIM_DURATION_US          = 100000;   // Transitions of 100 ms (trade-off reactivity-visibility)
    static constexpr bsUs_t COMPUTATION_TIME_SLICE_US = 100000;   // Duration of a chunk of computation (profile, histogram)
    static constexpr s64    DCLICK_RANGE_FACTOR       = 3;      // The range is N times the item size
    static constexpr int    MAX_EXTRA_LINE_PER_CONFIG = 500;      // Persistence of (temporarily) non used config file lines
    static constexpr int    CLI_HISTORY_MAX_LINE_QTY  = 100;
    static constexpr float  TEXT_BG_FOOTPRINT_ALPHA   = 0.15f;
    static constexpr float  MEM_BG_FOOTPRINT_ALPHA    = 0.25f;
    static constexpr float  OVERVIEW_VBAR_WIDTH       = 8.f;
    static constexpr float  CLI_CONSOLE_MIN_HEIGHT    = 100.f; // Pixels
    static constexpr int    FONT_SIZE_MIN = 10;
    static constexpr int    FONT_SIZE_MAX = 30;
    static constexpr int    LOCK_LATENCY_LIMIT_MAX_US = 20000;  // Maximum lock latency limit set to 20 ms
    static constexpr int    MAX_OVERLAPPED_THREAD     = 8;  // Maximum stored overlapped thread in timeline. Excess is ignored
    static constexpr int    MIN_TIMERANGE_NS          = 1000;  // Cannot zoom more than 1 µs

    // Built-in thread IDs for display
    static constexpr int    LOCKS_THREADID            = cmConst::MAX_THREAD_QTY+1;
    static constexpr int    CORE_USAGE_THREADID       = cmConst::MAX_THREAD_QTY+2;
    static constexpr int    QUANTITY_THREADID         = cmConst::MAX_THREAD_QTY+3;

    // Time formats
    static constexpr int TIME_FORMAT_SECOND = 0;
    static constexpr int TIME_FORMAT_HHMMSS = 1;
    static constexpr int TIME_FORMAT_QTY    = 2;

    // Colors
    static const ImVec4 gold;
    static const ImVec4 grey;
    static const ImVec4 white;
    static const ImVec4 red;
    static const ImVec4 yellow;
    static const ImVec4 cyan;
    static const ImVec4 darkBlue;
    static const ImVec4 darkOrange;

    static const ImU32 uWhite     = IM_COL32(255, 255, 255, 255);
    static const ImU32 uBlack     = IM_COL32(0,     0,   0, 255);
    static const ImU32 uYellow    = IM_COL32(255, 192,  64, 255);
    static const ImU32 uDarkOrange = IM_COL32(160, 130, 40, 255);
    static const ImU32 uCyan      = IM_COL32(32,  192, 192, 255);
    static const ImU32 uRed       = IM_COL32(255,  32,  32, 255);
    static const ImU32 uBrightRed = IM_COL32(255,   0,   0, 255);
    static const ImU32 uLightGrey = IM_COL32(240, 240, 240, 255);
    static const ImU32 uGrey      = IM_COL32(192, 192, 192, 255);
    static const ImU32 uGrey128   = IM_COL32(128, 128, 128, 255);
    static const ImU32 uGrey96    = IM_COL32( 96,  96,  96, 255);
    static const ImU32 uGrey64    = IM_COL32( 64,  64,  64, 255);
    static const ImU32 uGrey48    = IM_COL32( 48,  48,  48, 255);
    static const ImU32 uGreyDark  = IM_COL32( 33,  35,  27, 255);
};


// Helpers
#define DRAWLIST ImGui::GetWindowDrawList()
