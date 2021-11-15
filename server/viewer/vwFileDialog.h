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
#include "bsOs.h"
#include "bsString.h"


class vwFileDialog {
public:
    enum Mode { SELECT_DIR, OPEN_FILE, SAVE_FILE };
    vwFileDialog(const bsString& title, Mode mode, const bsVec<bsString>& typeFilters);

    void open (const bsString& initialPath, int maxSelectionQty=1);
    void close(void) { _shallClose = true; }
    bool draw (int fontSize);

    bool hasSelection(void) const  { return !_selection.empty(); }
    void clearSelection(void)      { _selection.clear(); }
    const bsVec<bsString>& getSelection(void) { plAssert(hasSelection()); return _selection; }

private:
    struct Entry {
        bsString name;
        bsDate   date;
        s64      size;
        bool     isSelected;
    };

    bsString _title;
    bsString _path;
    bsString _displayedSelection;
    Mode     _mode;
    bsVec<bsString> _typeFilters;
    bsVec<Entry> _dirEntries;
    bsVec<Entry> _fileEntries;
    bsVec<bsString> _selection;
    int      _selectedFilterIdx = 0;
    u32      _driveBitMap       = 0;
    bool     _isEntriesDirty    = true;
    bool     _isSelDisplayDirty = true;
    bool     _doShowHidden      = false;
    bool     _isOpen     = false;
    bool     _shallOpen  = false;
    bool     _shallClose = false;
    int      _maxSelectionQty = 1;
};
