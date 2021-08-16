// Palanteer recording library
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

// Internal
#include "bs.h"
#include "bsTime.h"


class cmConst {
public:

    // Application constants
    static constexpr bsUs_t DELTARECORD_PERIOD_US = 300000; // Period of the update of the live display of record
    static constexpr int    CHILDREN_MAX  = 200000; // Truncation limit for smooth display of very unbalanced tree

    // Storage constants
    static constexpr int    MAX_THREAD_QTY  = 254;
    static constexpr int    MAX_LEVEL_QTY   = 254;

    // Built-in name IDs used to identify an Elem (no overlap with the user nameIdx)
    // Memory management specific
    static constexpr int    MEMORY_ALLOCSIZE_NAMEIDX  = 0x70000000;
    static constexpr int    MEMORY_ALLOCQTY_NAMEIDX   = 0x70000001;
    static constexpr int    MEMORY_DEALLOCQTY_NAMEIDX = 0x70000002;

    // Context switch specific
    static constexpr int    CTX_SWITCH_NAMEIDX        = 0x70000003;
    static constexpr int    CORE_USAGE_NAMEIDX        = 0x70000004;
    static constexpr int    CPU_CURVE_NAMEIDX         = 0x70000005;
    static constexpr int    SOFTIRQ_NAMEIDX           = 0x70000006;

    // Lock specific
    static constexpr int    LOCK_WAIT_NAMEIDX         = 0x70000007;
    static constexpr int    LOCK_USE_NAMEIDX          = 0x70000008;
    static constexpr int    LOCK_NTF_NAMEIDX          = 0x70000009;

    // Other
    static constexpr int    SCOPE_NAMEIDX             = 0x70000010;
    static constexpr int    MARKER_NAMEIDX            = 0x70000011;
};
