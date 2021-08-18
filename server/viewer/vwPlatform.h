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

// System
#include <atomic>

// Internal
#include "bs.h"
#include "bsTime.h"
#include "bsOs.h"
#include "bsVec.h"
#include "bsKeycode.h"

#define VW_REDRAW_PER_NTF 5
#define VW_REDRAW_PER_BOUNCE 2

class vwMain;


class vwPlatform : public bsOsHandler {
public:
    // Constructor & destructor
    vwPlatform(int rxPort, bool doLoadLastFile, const bsString& overrideStoragePath);
    virtual ~vwPlatform(void);
    void run(void);

    // Application interface
    void   quit(void) { _doExit.store(1); }
    bsUs_t getLastUpdateDuration   (void) const { return _lastUpdateDurationUs; }
    bsUs_t getLastRenderingDuration(void) const { return _lastRenderingDurationUs; }
    void   setNewFontSize(int fontSize) { _newFontSizeToInstall = fontSize; }
    bsUs_t getLastMouseMoveDurationUs(void) const { return bsGetClockUs()-_lastMouseMoveTimeUs; }
    int    getDisplayWidth (void) const { return _displayWidth; }
    int    getDisplayHeight(void) const { return _displayHeight; }

    // Event handling
    bool redraw(void);
    void notifyDrawDirty(void) { _dirtyRedrawCount.store(VW_REDRAW_PER_NTF); }
    void notifyWindowSize(int windowWidth, int windowHeight) { _displayWidth = windowWidth; _displayHeight = windowHeight; notifyDrawDirty(); }
    bool isVisible(void) const { return _isVisible.load(); }

    // Events
    void notifyMapped(void)   { notifyDrawDirty(); }
    void notifyUnmapped(void) { notifyDrawDirty(); }
    void notifyExposed(void)  { notifyDrawDirty(); }
    void notifyFocusOut(void) { notifyDrawDirty(); }
    void notifyEnter(bsKeyModState kms);
    void notifyLeave(bsKeyModState kms);

    void eventChar(char16_t codepoint);
    void eventKeyPressed (bsKeycode keycode, bsKeyModState kms);
    void eventKeyReleased(bsKeycode keycode, bsKeyModState kms);
    void eventButtonPressed (int buttonId, int x, int y, bsKeyModState kms);
    void eventButtonReleased(int buttonId, int x, int y, bsKeyModState kms);
    void eventMouseMotion   (int x, int y);
    void eventWheelScrolled (int x, int y, int steps, bsKeyModState kms);

private:
    vwPlatform(const vwPlatform& other); // To please static analyzers
    vwPlatform& operator=(vwPlatform other);
    void configureStyle(void);

    // Platform state
    std::atomic<int> _doExit;
    std::atomic<int> _isVisible;
    std::atomic<u64> _dirtyRedrawCount;
    vwMain*          _main                 = 0;
    int              _newFontSizeToInstall = -1;
    bsUs_t           _lastMouseMoveTimeUs  = 0;

    // ImGui
    int    _displayWidth  = -1;
    int    _displayHeight = -1;
    float  _dpiScale      =  1.;
    bsUs_t _lastUpdateDurationUs    = 1; // Update only
    bsUs_t _lastRenderingDurationUs = 1; // Update and rendering
    bsUs_t _lastRenderingTimeUs     = 0;
};
