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

// This file implements the search window

// System
#include <inttypes.h>

#include "imgui.h"
#include "imgui_internal.h" // For ImGui::BringWindowToDisplayFront

// Internal
#include "cmRecord.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


#ifndef PL_GROUP_SEARCH
#define PL_GROUP_SEARCH 0
#endif


void
vwMain::SearchAggregatedIterator::init(cmRecord* record, u32 selectedNameIdx, u64 threadBitmap, s64 initStartTimeNs, int itemMaxQty,
                                       bsVec<SearchCacheItem>& items)
{
    // Loop on elems
    itElems.clear();
    itElemsEvts.clear();
    startTimeNs = initStartTimeNs;
    s64    timeNs = 0;
    u32    lIdx   = 0;
    double value  = 0.;
    cmRecord::Evt e;
    for(int elemIdx=0; elemIdx<record->elems.size(); ++elemIdx) {
        const cmRecord::Elem& elem = record->elems[elemIdx];
        if(elem.isPartOfHStruct && elem.nameIdx==selectedNameIdx &&               // From H-structure and with our target name
           (elem.threadBitmap&threadBitmap) && elem.threadId<cmConst::MAX_THREAD_QTY) { // Only those with a valid non-filtered thread ID
            // Store the iterator
            itElems.push_back(cmRecordIteratorElem(record, elemIdx, startTimeNs, 0.));
            // and the first element after the date
            while((lIdx=itElems.back().getNextPoint(timeNs, value, e))!=PL_INVALID && timeNs<startTimeNs) { }
            itElemsEvts.push_back({e, (lIdx!=PL_INVALID)? timeNs : -1, value, elemIdx, lIdx });
        }
    }

    // Get a working copy of the iterators (so that stored iterators are still relative to the start date and we can go 'backward')
    bsVec<cmRecordIteratorElem> itElems2 = itElems;

    // Get all items to display
    items.clear();
    while(items.size()<itemMaxQty) {
        // Store the earliest event
        int earliestIdx    = -1;
        for(int i=0; i<itElemsEvts.size(); ++i) {
            if(itElemsEvts[i].timeNs>=0 && (earliestIdx==-1 || itElemsEvts[i].timeNs<itElemsEvts[earliestIdx].timeNs)) earliestIdx = i;
        }
        if(earliestIdx<0) break;
        items.push_back(itElemsEvts[earliestIdx]);

        // Refill the used iterator
        lIdx = itElems2[earliestIdx].getNextPoint(timeNs, value, e);
        itElemsEvts[earliestIdx] = {e, (lIdx!=PL_INVALID)? timeNs : -1, value, itElemsEvts[earliestIdx].elemIdx, lIdx };
    }
}


s64
vwMain::SearchAggregatedIterator::getPreviousTime(int itemQty)
{
    // Initialize the return in time by getting the time for each event just before the start date
    bsVec<int> offsets(itElems.size());
    for(int i=0; i< itElems.size(); ++i) {
        offsets[i]            = -1; // One event before the start date (iterator was post incremented once, hence the -1)
        itElemsEvts[i].timeNs = itElems[i].getTimeRelativeIdx(offsets[i]); // Result is -1 if none
        if(itElemsEvts[i].timeNs>=startTimeNs) { // This case should happen all the time, except when reaching the end of the recorded info
            itElemsEvts[i].timeNs = itElems[i].getTimeRelativeIdx(--offsets[i]);
        }
    }

    s64 previousTimeNs = -1;
    while((itemQty--)>0) {
        // Store the earliest time
        int latestIdx = -1;
        for(int i=0; i<itElemsEvts.size(); ++i) {
            if(itElemsEvts[i].timeNs>=0 && (latestIdx==-1 || itElemsEvts[i].timeNs>itElemsEvts[latestIdx].timeNs)) latestIdx = i;
        }
        if(latestIdx<0) return previousTimeNs;
        previousTimeNs = itElemsEvts[latestIdx].timeNs;

        // Refill the used iterator
        itElemsEvts[latestIdx].timeNs = itElems[latestIdx].getTimeRelativeIdx(--offsets[latestIdx]); // Result is -1 if none
    }
    return previousTimeNs;
}


void
vwMain::prepareSearch(void)
{
    // Check if the cache is still valid
    vwMain::Search& s = _search;
    const float winHeight = ImGui::GetWindowSize().y; // Approximated and bigger anyway
    if(!s.isCacheDirty && winHeight<=s.lastWinHeight) return;

    // Worth working?
    plgScope(SEARCH, "prepareSearch");
    s.lastWinHeight = winHeight;
    s.isCacheDirty  = false;
    s.cachedItems.clear();
    if(s.selectedNameIdx==PL_INVALID) return; // No selection

    // Thread name max length and thread bitmap
    s.maxThreadNameLength = 0;
    u64 threadBitmap = 0;
    for(int i=0; i<_record->threads.size(); ++i) {
        if(s.threadSelection[i]) threadBitmap |= (1LL<<i);
        int length = _record->getString(_record->threads[i].nameIdx).value.size();
        if(length>s.maxThreadNameLength) s.maxThreadNameLength = length;
    }

    // Resynchronization on a date?
    if(s.forceTimeNs>=0) {
        s.startTimeNs = s.forceTimeNs;
        s.forceTimeNs = -1;
    }

    // Get the data
    int maxLineQty = bsMax(10, 1+winHeight/ImGui::GetTextLineHeightWithSpacing()); // 10 minimum for the page down
    s.aggregatedIt.init(_record, s.selectedNameIdx, threadBitmap, s.startTimeNs, maxLineQty, s.cachedItems);

    // Compute the scroll ratio (for the scroll bar indication) from the dates
    s.cachedScrollRatio = (float)bsMinMax((double)s.startTimeNs/(double)bsMax(_record->durationNs, 1), 0., 1.);
}


void
vwMain::drawSearch(void)
{
    if(!_record) return;
    vwMain::Search& s = _search;

    // Open search window
    // ==================
    // Show window?
    bool isCtrlFHit = ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F);
    if(isCtrlFHit && !getConfig().getWindowSearchVisibility()) {
        getConfig().setWindowSearchVisibility(true);
        s.isWindowSelected = true;
        setFullScreenView(-1);
    }
    if(!getConfig().getWindowSearchVisibility()) return; // // Hidden window, nothing to do
    if(_uniqueIdFullScreen>=0 && s.uniqueId!=_uniqueIdFullScreen) return;
    // Window just made visible?
    if(s.isWindowSelected) {
        ImGui::SetNextWindowFocus();
    }
    // Do this once (placement inside the layout)
    if(s.isNew) {
        s.isNew = false;
        if(s.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(s.newDockId);
        else selectBestDockLocation(false, false);
    }
    // Open the window
    bool isOpenWindow = true;
    char windowStr[128];
    snprintf(windowStr, sizeof(windowStr), "Search###%d", _search.uniqueId);
    if(!ImGui::Begin(windowStr, &isOpenWindow, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavInputs)) {
        if(isCtrlFHit) { // Case in a tab bar without visibility
            getConfig().setWindowSearchVisibility(true);
            s.isWindowSelected = true;
        }
        ImGui::End();
        return;
    }
    // User clicked to dismiss the search window?
    if(!isOpenWindow) {
        getConfig().setWindowSearchVisibility(false);
        for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(PL_INVALID);
        setFullScreenView(-1);
    }
    plgScope(SEARCH, "drawSearch");


    // User search input
    // =================

    // Thread filtering
    float textPixMargin = ImGui::GetStyle().ItemSpacing.x;
    float padMenuX    = ImGui::GetStyle().FramePadding.x;
    float widthMenu   = ImGui::CalcTextSize("Filter threads").x;
    float textBgY     = ImGui::GetWindowPos().y+ImGui::GetCursorPos().y;
    ImU32 filterBg    = ImColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    while(s.threadSelection.size()<_record->threads.size()) s.threadSelection.push_back(true);
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+textPixMargin, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+widthMenu+2.f*textPixMargin, textBgY+ImGui::GetTextLineHeightWithSpacing()), filterBg);
    if(s.isFilteredOnThread) ImGui::PushStyleColor(ImGuiCol_Text, vwConst::gold);
    ImGui::SetCursorPosX(textPixMargin+padMenuX);
    ImGui::AlignTextToFramePadding();
    if(ImGui::Selectable("Filter threads", false, 0, ImVec2(widthMenu, 0))) ImGui::OpenPopup("Thread search menu");
    if(s.isFilteredOnThread) ImGui::PopStyleColor();
    if(ImGui::BeginPopup("Thread search menu", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs)) {
        // Global selection
        bool forceSelectAll   = ImGui::Selectable("Select all",   false, ImGuiSelectableFlags_DontClosePopups);
        bool forceDeselectAll = ImGui::Selectable("Deselect all", false, ImGuiSelectableFlags_DontClosePopups);
        ImGui::Separator();

        // Individual selection
        s.isFilteredOnThread = false;
        for(int i=0; i<_record->threads.size(); ++i) {
            if(ImGui::Checkbox(_record->getString(_record->threads[i].nameIdx).value.toChar(), &s.threadSelection[i])) {
                s.isCompletionDirty = s.isCacheDirty = true;
            }
            if(forceSelectAll && !s.threadSelection[i]) {
                s.threadSelection[i] = true;
                s.isCompletionDirty  = s.isCacheDirty = true;
            }
            if(forceDeselectAll && s.threadSelection[i]) {
                s.threadSelection[i] = false;
                s.isCompletionDirty  = s.isCacheDirty = true;
            }
            if(!s.threadSelection[i]) s.isFilteredOnThread = true;
        }
        ImGui::EndPopup();
    }

    // Case sensitivity
    ImGui::SameLine(0, 3.f*textPixMargin);
    ImGui::Checkbox("Case sensitive", &s.isInputCaseSensitive);

    // Sync combo
    float  comboWidth = ImGui::CalcTextSize("Isolated XXX").x;
    float   comboX    = ImGui::GetWindowContentRegionMax().x-comboWidth;
    ImGui::SameLine(comboX);
    drawSynchroGroupCombo(comboWidth, &s.syncMode);
    ImGui::Separator();

    // Input text callback to handle completion list recomputation and arrow keys
    struct CallbackWrapper {
        static int cbk(ImGuiInputTextCallbackData* data)
        {
            vwMain::Search* search = (vwMain::Search*) data->UserData;
            if(data->EventFlag==ImGuiInputTextFlags_CallbackEdit) {
                search->isCompletionDirty = true;
                search->lastMouseY = -1;
            } else if(data->EventFlag==ImGuiInputTextFlags_CallbackHistory) {
                if     (data->EventKey==ImGuiKey_DownArrow && search->completionIdx<search->completionNameIdxs.size()-1) search->completionIdx++;
                else if(data->EventKey==ImGuiKey_UpArrow   && search->completionIdx>0) search->completionIdx--;
            }
            return 0;
        }
    };
    // Input text
    bool isEnterPressed = ImGui::InputText("##search input", s.input, sizeof(s.input),
                                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackEdit |
                                           ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_AutoSelectAll,
                                           CallbackWrapper::cbk, (void*)&s);

    // User hit Ctrl-F and it is not a "show window"?
    if(isCtrlFHit && !s.isWindowSelected) {
        if(ImGui::IsItemActive()) { // Already under focus => hide
            getConfig().setWindowSearchVisibility(false);
            for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(PL_INVALID);
            setFullScreenView(-1);
        }
        else {
            s.isWindowSelected = true;  // else set focus
        }
    }
    // Handle the focus
    ImGui::SetItemDefaultFocus();
    if(s.isWindowSelected) {
        s.isWindowSelected = false;
        ImGui::SetKeyboardFocusHere(-1);
    }
    if(ImGui::IsItemActive()) {
        s.isInputPopupOpen = true;
    }

    // Popup of the input text
    if(s.isInputPopupOpen) {
        ImGui::SetNextWindowPos ({ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y });
        ImGui::SetNextWindowSize({ ImGui::GetItemRectSize().x, 0 });
        constexpr int popupFlags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

        // Open (fake) popup at the fixed placed
        if(ImGui::Begin("##search popup", &s.isInputPopupOpen, popupFlags)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

            // Rebuild the completion list if needed
            if(s.isCompletionDirty) {
                u64 threadBitmap = 0;
                for(int i=0; i<_record->threads.size(); ++i) {
                    if(s.threadSelection[i]) threadBitmap |= (1LL<<i);
                }

                s.completionNameIdxs.clear();
                s.isCompletionDirty = false;
                s.completionIdx     = -1;
                for(int nameIdx=0; s.completionNameIdxs.size()<30 && nameIdx<_record->getStrings().size(); ++nameIdx) {
                    const cmRecord::String& name = _record->getString(nameIdx);
                    if(name.value.size()<=1 || !(name.threadBitmapAsName&threadBitmap)) continue; // Only non-empty strings related to user instrumentation for selected threads
                    const char* autoComplete = name.value.toChar();
                    if     (!s.isInputCaseSensitive && !strcasestr(autoComplete, s.input)) continue;
                    else if( s.isInputCaseSensitive && !strstr    (autoComplete, s.input)) continue;
                    s.completionNameIdxs.push_back(nameIdx);
                }
            }

            // Draw the completion list
            for(int i=0; i<s.completionNameIdxs.size(); ++i) {
                ImGui::PushID(i);
                const char* autoComplete = _record->getString(s.completionNameIdxs[i]).value.toChar();
                bool isSelected = (i==s.completionIdx);
                // Draw selectable
                if(ImGui::Selectable(autoComplete, isSelected, ImGuiSelectableFlags_DontClosePopups) || (isSelected && ImGui::IsKeyPressedMap(ImGuiKey_Enter))) {
                    snprintf(s.input, sizeof(s.input), "%s", autoComplete);
                    s.isInputPopupOpen = false;
                    s.selectedNameIdx  = s.completionNameIdxs[i];
                    s.isCacheDirty     = true;
                    for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(s.selectedNameIdx);
                    plMarker("user", "New search");
                }
                // Mouse, as up & down arrows,  drives selection too
                else if(ImGui::IsItemHovered() && ImGui::GetMousePos().y!=s.lastMouseY) s.completionIdx = i;
                else if(s.completionNameIdxs.size()==1) s.completionIdx = i;
                ImGui::PopID();
            }

            // Case "enter" pressed on a single entry list: autocomplete
            if(isEnterPressed && s.completionNameIdxs.size()==1) {
                snprintf(s.input, sizeof(s.input), "%s", _record->getString(s.completionNameIdxs[0]).value.toChar());
                s.isInputPopupOpen = false;
                s.selectedNameIdx  = s.completionNameIdxs[0];
                s.isCacheDirty     = true;
                for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(s.selectedNameIdx);
                plMarker("user", "New search");
            }
            // Keep track of the mouse move to detect change
            s.lastMouseY = ImGui::GetMousePos().y;
        } // End of popup drawing
        bool isPopupFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
        ImGui::End();
        if(s.isInputPopupOpen && !isPopupFocused && (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow) || !ImGui::IsItemActive())) s.isInputPopupOpen = false;
    } // End of popup of the input text
    ImGui::Separator();


    // Search result display
    // =====================

    // Some init
    ImGui::BeginChild("Search", ImVec2(0,0), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar |
                      ImGuiWindowFlags_NoNavInputs);  // Display area is virtual so self-managed
    prepareSearch(); // Ensure cache is up to date, even after window creation
    const float winX = ImGui::GetWindowPos().x;
    const float winY = ImGui::GetWindowPos().y;
    const float winWidth      = ImGui::GetWindowContentRegionMax().x;
    const float winHeight     = ImGui::GetWindowSize().y;
    const float fontHeight    = ImGui::GetTextLineHeightWithSpacing();
    const float mouseY        = ImGui::GetMousePos().y;
    const bool   isWindowHovered = ImGui::IsWindowHovered();

    const float charWidth = ImGui::CalcTextSize("0").x;
    constexpr int maxMsgSize = 256;
    char tmpStr  [maxMsgSize];
    char timeStr [maxMsgSize];
    char nameStr [maxMsgSize];
    char valueStr[maxMsgSize];

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    const double normalizedScrollHeight = 1000000.; // Value does not really matter, it just defines the granularity
    float curScrollPos = ImGui::GetScrollY();
    if(!s.didUserChangedScrollPos && bsAbs(curScrollPos-s.lastScrollPos)>=1.) {
        plgScope(SEARCH, "New user scroll position from ImGui");
        plgData(SEARCH, "expected pos", s.lastScrollPos);
        plgData(SEARCH, "new pos", curScrollPos);
        s.cachedScrollRatio = (float)(curScrollPos/normalizedScrollHeight);
        s.setStartPosition((s64)(s.cachedScrollRatio*_record->durationNs));
        s.didUserChangedScrollPos = false;
    }

    // Manage keys and mouse inputs
    // ============================
    s.didUserChangedScrollPos = false;

    int tlWheelCounter = 0;
    if(isWindowHovered) {
        // Check mouse input
        int textWheelCounter =  (ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getVWheelInversion()); // No Ctrl key: wheel is for the text
        tlWheelCounter       = (!ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getHWheelInversion()); // Ctrl key: wheel is for the timeline (processed in highlighted text display)
        int dragLineQty = 0;
        if(ImGui::IsMouseDragging(2)) {
            s.isDragging = true;
            if(bsAbs(ImGui::GetMouseDragDelta(2).y)>1.) {
                float tmp = (ImGui::GetMouseDragDelta(2).y+s.dragReminder);
                ImGui::ResetMouseDragDelta(2);
                dragLineQty   = (int)(tmp/fontHeight);
                s.dragReminder = tmp-fontHeight*dragLineQty;
            }
        } else s.dragReminder = 0.;

        // Move start position depending on keys, wheel or drag
        if(ImGui::IsKeyPressed(KC_Down)) {
            plgText(SEARCH, "Key", "Down pressed");
            if(s.cachedItems.size()>=2) {
                s.setStartPosition(s.cachedItems[1].timeNs);
            }
        }

        if(ImGui::IsKeyPressed(KC_Up)) {
            plgText(SEARCH, "Key", "Up pressed");
            s64 newTimeNs = s.aggregatedIt.getPreviousTime(1);
            if(newTimeNs>=0) {
                s.setStartPosition(newTimeNs);
            }
        }

        if(textWheelCounter<0 || dragLineQty<0 || ImGui::IsKeyPressed(KC_PageDown)) {
            plgText(SEARCH, "Key", "Page Down pressed");
            const int steps = bsMin((dragLineQty!=0)?-dragLineQty:10, s.cachedItems.size()-1);
            if(steps>0 && steps<s.cachedItems.size()) {
                s.setStartPosition(s.cachedItems[steps].timeNs);
            }
        }

        if(textWheelCounter>0 || dragLineQty>0 || ImGui::IsKeyPressed(KC_PageUp)) {
            plgText(SEARCH, "Key", "Page Up pressed");
            const int steps = (dragLineQty!=0)? dragLineQty : 10;
            s64 newTimeNs = s.aggregatedIt.getPreviousTime(steps);
            if(newTimeNs>=0) {
                s.setStartPosition(newTimeNs);
            }
        }

        if(!s.isInputPopupOpen && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
            plgText(SEARCH, "Key", "Full screen pressed");
            setFullScreenView(s.uniqueId);
        }
    }
    else s.dragReminder = 0.;


    // Prepare the drawing
    // ===================
    // Previous navigation may have made dirty the cached data
    prepareSearch();

    // Set the modified scroll position in ImGui, if not changed through imGui
    if(s.didUserChangedScrollPos) {
        plgData(SEARCH, "Set new scroll pos from user", s.cachedScrollRatio*normalizedScrollHeight);
        ImGui::SetScrollY((float)(s.cachedScrollRatio*normalizedScrollHeight));
    }
    // Mark the virtual total size
    s.lastScrollPos = ImGui::GetScrollY();
    ImGui::SetCursorPosY(normalizedScrollHeight);
    plgData(SEARCH, "Current scroll pos", s.lastScrollPos);



    // Draw the text
    // =============
    float y = winY;
    float mouseTimeBestY = -1.;
    s64 mouseTimeBestTimeNs = -1;
    s64 newMouseTimeNs = -1;
    for(const auto& sci : s.cachedItems) {
        // Get the cached item
        const cmRecord::Elem& elem = _record->elems[sci.elemIdx];
        const cmRecord::Evt&  evt  = sci.evt;
        int flags = evt.flags;
        int v = flags&PL_FLAG_TYPE_MASK;

        // Build the strings
        nameStr [0] = 0;
        valueStr[0] = 0;
        snprintf(timeStr, maxMsgSize, "%f s", 0.000000001*sci.timeNs);
        snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.nameIdx).value.toChar());
        if(flags&PL_FLAG_SCOPE_BEGIN) {
            if (v==PL_FLAG_TYPE_LOCK_WAIT) { snprintf(valueStr, maxMsgSize, "<lock wait>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
            else snprintf(valueStr, maxMsgSize, "{ %s }", getNiceDuration((s64)sci.value));
        }
        else if(v==PL_FLAG_TYPE_MARKER) {
            // For markers, category is stored instead of name and message instead of filename
            snprintf(valueStr, maxMsgSize, "<marker '%s'>", _record->getString(evt.nameIdx).value.toChar());
            snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.filenameIdx).value.toChar());
            v = PL_FLAG_TYPE_DATA_TIMESTAMP;
        }
        else if(v==PL_FLAG_TYPE_LOCK_ACQUIRED) { snprintf(valueStr, maxMsgSize, "<lock acquired>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else if(v==PL_FLAG_TYPE_LOCK_RELEASED) { snprintf(valueStr, maxMsgSize, "<lock released>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else if(v==PL_FLAG_TYPE_LOCK_NOTIFIED) { snprintf(valueStr, maxMsgSize, "<lock notified>"); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.nameIdx).value.toChar());

        switch(v) {
        case PL_FLAG_TYPE_DATA_NONE:      break;
        case PL_FLAG_TYPE_DATA_TIMESTAMP: break;
        case PL_FLAG_TYPE_DATA_S32:       snprintf(valueStr, maxMsgSize, "%d",  evt.vInt); break;
        case PL_FLAG_TYPE_DATA_U32:       snprintf(valueStr, maxMsgSize, "%u",  evt.vU32); break;
        case PL_FLAG_TYPE_DATA_S64:       snprintf(valueStr, maxMsgSize, "%" PRId64, evt.vS64); break;
        case PL_FLAG_TYPE_DATA_U64:       snprintf(valueStr, maxMsgSize, "%" PRIu64, evt.vU64); break;
        case PL_FLAG_TYPE_DATA_FLOAT:     snprintf(valueStr, maxMsgSize, "%f",  evt.vFloat);  break;
        case PL_FLAG_TYPE_DATA_DOUBLE:    snprintf(valueStr, maxMsgSize, "%f",  evt.vDouble); break;
        case PL_FLAG_TYPE_DATA_STRING:    snprintf(valueStr, maxMsgSize, "%s",  _record->getString(evt.vStringIdx).value.toChar()); break;
        default:                          snprintf(valueStr, maxMsgSize, "<BAD TYPE %d>", v);
        };

        // Update the mouse time
        if(isWindowHovered && mouseY>y) {
            newMouseTimeNs = sci.timeNs;
        }

        // Update the best fit for the mouse time display (yellow horizontal line)
        if(_mouseTimeNs>=sci.timeNs && sci.timeNs>mouseTimeBestTimeNs) {
            mouseTimeBestTimeNs = sci.timeNs;
            mouseTimeBestY      = y+fontHeight;
        }

        // Manage hovering: highlight and clicks
        if(isWindowHovered && mouseY>=y && mouseY<y+fontHeight) {
            // This section shall be highlighted
            if(elem.nameIdx!=elem.hlNameIdx) // "Flat" event, so we highlight its block scope
                setScopeHighlight(elem.threadId, sci.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx);
            else
                setScopeHighlight(elem.threadId, sci.timeNs, elem.flags, elem.nestingLevel, elem.hlNameIdx);

            // Synchronized navigation
            if(s.syncMode>0) { // No synchronized navigation for isolated windows
                s64 syncStartTimeNs, syncTimeRangeNs;
                getSynchronizedRange(s.syncMode, syncStartTimeNs, syncTimeRangeNs);

                // Click: set timeline position at middle screen only if outside the center third of screen
                if((ImGui::IsMouseReleased(0) && ImGui::GetMousePos().x<winX+winWidth) || tlWheelCounter) {
                    synchronizeNewRange(s.syncMode, bsMax(sci.timeNs-(s64)(0.5*syncTimeRangeNs), 0LL), syncTimeRangeNs);
                    ensureThreadVisibility(s.syncMode, evt.threadId);
                    synchronizeText(s.syncMode, evt.threadId, elem.nestingLevel, sci.lIdx, sci.timeNs, s.uniqueId);
                }
                // Double click: adapt also the scale to have the scope at 10% of the screen
                if(ImGui::IsMouseDoubleClicked(0) && (evt.flags&PL_FLAG_SCOPE_BEGIN)) {
                    s64 newTimeRangeNs =  (s64)(vwConst::DCLICK_RANGE_FACTOR*sci.value);
                    synchronizeNewRange(s.syncMode, syncStartTimeNs+(s64)((double)(sci.timeNs-syncStartTimeNs)/syncTimeRangeNs*(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(s.syncMode, evt.threadId);
                }
                // Zoom the timeline
                if(tlWheelCounter!=0) {
                    s64 newTimeRangeNs = getUpdatedRange(tlWheelCounter, syncTimeRangeNs);
                    synchronizeNewRange(s.syncMode, syncStartTimeNs+(s64)((double)(sci.timeNs-syncStartTimeNs)/syncTimeRangeNs*(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(s.syncMode, evt.threadId);
                }

                // Right click: contextual menu, only on scope start
                if(!s.isDragging && ImGui::IsMouseReleased(2)) {
                    s.ctxThreadId     = evt.threadId;
                    s.ctxNestingLevel = elem.nestingLevel;
                    s.ctxScopeLIdx     = sci.lIdx;
                    s.ctxNameIdx      = evt.nameIdx;
                    ImGui::OpenPopup("Search menu");
                    _plotMenuItems.clear(); // Reset the popup menu state
                    if((evt.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER) {
                        // Find the marker elemIdx suitable for plot/histo
                        u64  itemHashPath = bsHashStepChain(_record->threads[evt.threadId].threadHash, evt.nameIdx, cmConst::MARKER_NAMEIDX);
                        int* elemIdx      = _record->elemPathToId.find(itemHashPath, cmConst::MARKER_NAMEIDX);
                        if(elemIdx) prepareGraphContextualMenu(*elemIdx, 0LL, _record->durationNs, false, false);
                    } else {
                        prepareGraphContextualMenu(s.ctxThreadId, s.ctxNestingLevel, s.ctxScopeLIdx, 0, _record->durationNs);
                    }
                }
            }

            // Tooltip
            if(getLastMouseMoveDurationUs()>500000) {
                int pathQty = 1;
                int path[cmConst::MAX_LEVEL_QTY+1] = {sci.elemIdx};
                while(pathQty<cmConst::MAX_LEVEL_QTY+1 && path[pathQty-1]>=0) { path[pathQty] = _record->elems[path[pathQty-1]].prevElemIdx; ++pathQty; }
                int offset = snprintf(tmpStr, sizeof(tmpStr), "[%s] ", _record->getString(_record->threads[elem.threadId].nameIdx).value.toChar());
                for(int i=pathQty-2; i>=0; --i) {
                    offset += snprintf(tmpStr+offset, sizeof(tmpStr)-offset, "%s>", _record->getString(_record->elems[path[i]].nameIdx).value.toChar());
                }
                tmpStr[offset-1] = 0; // Remove the last '>'
                ImGui::SetTooltip("%s", tmpStr);
            }

        }
        bool doHighlight = (elem.nameIdx!=elem.hlNameIdx)?
            isScopeHighlighted(elem.threadId, sci.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx) :
            isScopeHighlighted(elem.threadId, sci.timeNs, evt.flags, elem.nestingLevel, elem.hlNameIdx);

        // Compute colors
        ImU32 color1 = getConfig().getCurveColor(sci.elemIdx, true);

        // Display the text background if highlighted
        if(doHighlight) {
            DRAWLIST->AddRectFilled(ImVec2(winX, y), ImVec2(winX+winWidth, y+fontHeight), vwConst::uGrey48);
        }

        // Display the date
        float offsetX = winX+textPixMargin;
        DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uWhite, timeStr);
        offsetX += charWidth*14;

        // Display the thread
        snprintf(tmpStr, maxMsgSize, "[%s]", _record->getString(_record->threads[evt.threadId].nameIdx).value.toChar());
        DRAWLIST->AddText(ImVec2(offsetX, y), ImColor(getConfig().getThreadColor(evt.threadId)), tmpStr);
        offsetX += charWidth*(s.maxThreadNameLength+1);

        // Display the name of the item
        DRAWLIST->AddText(ImVec2(offsetX, y), color1, nameStr);
        offsetX += bsMax(ImGui::CalcTextSize(nameStr).x, 20.f*charWidth)+2.f*charWidth;

        // Display the value
        DRAWLIST->AddText(ImVec2(offsetX, y), color1, valueStr);

        // Next line
        if(y>winY+winHeight) break;
        y += fontHeight;
    }

    // Display and update the mouse time
    if(mouseTimeBestY>=0) {
        DRAWLIST->AddLine(ImVec2(winX, mouseTimeBestY), ImVec2(winX+winWidth, mouseTimeBestY), vwConst::uYellow);
    }
    if(newMouseTimeNs>=0.) _mouseTimeNs = newMouseTimeNs;
    if(!ImGui::IsMouseDragging(2)) s.isDragging = false;

    // Contextual menu
    if(ImGui::BeginPopup("Search menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
        ImGui::TextColored(vwConst::grey, "%s", _record->getString(s.ctxNameIdx).value.toChar());
        ImGui::Separator();

        // Plot & histogram
        if(!displayPlotContextualMenu(s.ctxThreadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
        ImGui::Separator();
        if(!displayHistoContextualMenu(headerWidth))                       ImGui::CloseCurrentPopup();

        // Color
        if(!_plotMenuItems.empty()) {
            ImGui::Separator();
            std::function<void(int)> curveSetColor = [this] (int colorIdx) { getConfig().setCurveColorIdx(_plotMenuItems[0].elemIdx, colorIdx); };
            displayColorSelectMenu("Color", getConfig().getCurveColorIdx(_plotMenuItems[0].elemIdx), curveSetColor);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    ImGui::End();
}
