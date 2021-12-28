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
#include <cinttypes>

#include "imgui.h"
#include "imgui_internal.h" // For ImGui::BringWindowToDisplayFront

// Internal
#include "bsKeycode.h"
#include "bsOs.h"
#include "cmRecord.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"

#ifndef PL_GROUP_SEARCH
#define PL_GROUP_SEARCH 0
#endif


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

    // Compute matching H-tree Elements (only those with a valid non-filtered thread ID)
    bsVec<int> logElemIdxArray;
    bsVec<int> elemIdxArray;
    for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
        const cmRecord::Elem& elem = _record->elems[elemIdx];
        if(elem.nameIdx==s.selectedNameIdx && (elem.threadBitmap&threadBitmap) && elem.threadId<cmConst::MAX_THREAD_QTY) {
            if     (elem.isPartOfHStruct)         elemIdxArray.push_back(elemIdx);
            else if(elem.flags==PL_FLAG_TYPE_LOG) logElemIdxArray.push_back(elemIdx);
        }
    }

    // Resynchronization on a date?
    if(s.forceTimeNs>=0) {
        s.startTimeNs = s.forceTimeNs;
        s.forceTimeNs = -1;
    }

    // Get the data
    s.cachedItems.clear();
    s.aggregatedIt.init(_record, s.startTimeNs, 0., logElemIdxArray, elemIdxArray);
    int maxLineQty = bsMax(10, 1+winHeight/ImGui::GetTextLineHeightWithSpacing()); // 10 minimum for the page down
    AggCacheItem aggrEvt;
    while((maxLineQty--)>=0 && s.aggregatedIt.getNextEvent(aggrEvt)) {
        if(aggrEvt.evt.flags==PL_FLAG_TYPE_LOG) {
            s.cachedItems.push_back({aggrEvt.evt, aggrEvt.evt.vS64, 0., aggrEvt.elemIdx, PL_INVALID, aggrEvt.message, aggrEvt.lineQty});
        } else {
            s.cachedItems.push_back({aggrEvt.evt, aggrEvt.timeNs, aggrEvt.value, aggrEvt.elemIdx, aggrEvt.lIdx, "", 1});
        }
    }

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
                    if((!s.isInputCaseSensitive && !strcasestr(autoComplete, s.input)) ||
                       (s.isInputCaseSensitive && !strstr    (autoComplete, s.input))) continue;
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
                    s.startTimeNs      = 0;
                    for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(s.selectedNameIdx);
                    plLogInfo("user", "New search");
                }
                // Mouse, as up & down arrows,  drives selection too
                else if((ImGui::IsItemHovered() && ImGui::GetMousePos().y!=s.lastMouseY) ||
                        (s.completionNameIdxs.size()==1)) {
                    s.completionIdx = i;
                }
                ImGui::PopID();
            }

            // Case "enter" pressed on a single entry list: autocomplete
            if(isEnterPressed && s.completionNameIdxs.size()==1) {
                snprintf(s.input, sizeof(s.input), "%s", _record->getString(s.completionNameIdxs[0]).value.toChar());
                s.isInputPopupOpen = false;
                s.selectedNameIdx  = s.completionNameIdxs[0];
                s.isCacheDirty     = true;
                s.startTimeNs      = 0;
                for(Profile& prof : _profiles) if(s.threadSelection[prof.threadId]) prof.notifySearch(s.selectedNameIdx);
                plLogInfo("user", "New search");
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
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);  // Display area is virtual so self-managed
    prepareSearch(); // Ensure cache is up to date, even after window creation
    const float winX = ImGui::GetWindowPos().x;
    const float winY = ImGui::GetWindowPos().y;
    const float winWidth      = ImGui::GetWindowContentRegionMax().x;
    const float winHeight     = ImGui::GetWindowSize().y;
    const float fontHeight    = ImGui::GetTextLineHeightWithSpacing();
    const float fontHeightIntra = ImGui::GetTextLineHeight();
    const float mouseX        = ImGui::GetMousePos().x;
    const float mouseY        = ImGui::GetMousePos().y;
    const bool  isWindowHovered = ImGui::IsWindowHovered();

    const float charWidth = ImGui::CalcTextSize("0").x;
    constexpr int maxMsgSize = 256;
    char tmpStr  [maxMsgSize];
    char timeStr [maxMsgSize];
    char nameStr [maxMsgSize];
    char valueStr[maxMsgSize];

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    const double normalizedScrollHeight = 1000000.; // Value does not really matter, it just defines the granularity
    float curScrollPosX = ImGui::GetScrollX();
    float curScrollPosY = ImGui::GetScrollY();
    if(!s.didUserChangedScrollPos && bsAbs(curScrollPosY-s.lastScrollPos)>=1.) {
        plgScope(SEARCH, "New user scroll position from ImGui");
        plgData(SEARCH, "expected pos", s.lastScrollPos);
        plgData(SEARCH, "new pos", curScrollPosY);
        s.cachedScrollRatio = (float)(curScrollPosY/normalizedScrollHeight);
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

    // Draw the text
    // =============
    int timeFormat = getConfig().getTimeFormat();
    float y = winY;
    float mouseTimeBestY      = -1.;
    float maxOffsetX          =  0.;
    s64   mouseTimeBestTimeNs = -1;
    s64   newMouseTimeNs      = -1;
    for(const auto& sci : s.cachedItems) {
        // Get the cached item
        const cmRecord::Elem& elem = _record->elems[sci.elemIdx];
        const cmRecord::Evt&  evt  = sci.evt;
        int flags = evt.flags;
        int v = flags&PL_FLAG_TYPE_MASK;
        int lineQty = _record->getString(evt.nameIdx).lineQty;

        // Build the strings
        nameStr [0] = 0;
        valueStr[0] = 0;
        snprintf(timeStr, maxMsgSize, "%.9f s", 0.000000001*sci.timeNs);
        snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.nameIdx).value.toChar());
        if(flags&PL_FLAG_SCOPE_BEGIN) {
            if (v==PL_FLAG_TYPE_LOCK_WAIT) { snprintf(valueStr, maxMsgSize, "<lock wait>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
            else snprintf(valueStr, maxMsgSize, "{ %s }", getNiceDuration((s64)sci.value));
        }
        else if(v==PL_FLAG_TYPE_LOG) {
            // For logs, category is stored instead of name and message instead of filename
            snprintf(valueStr, maxMsgSize, "<log '%s'>", _record->getString(evt.nameIdx).value.toChar());
            snprintf(nameStr, maxMsgSize, "%s", sci.message.toChar());
            lineQty = bsMax(lineQty, sci.messageLineQty);
            v = PL_FLAG_TYPE_DATA_TIMESTAMP;
        }
        else if(v==PL_FLAG_TYPE_LOCK_ACQUIRED) { snprintf(valueStr, maxMsgSize, "<lock acquired>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else if(v==PL_FLAG_TYPE_LOCK_RELEASED) { snprintf(valueStr, maxMsgSize, "<lock released>  { %s }", getNiceDuration((s64)sci.value)); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else if(v==PL_FLAG_TYPE_LOCK_NOTIFIED) { snprintf(valueStr, maxMsgSize, "<lock notified>"); v = PL_FLAG_TYPE_DATA_TIMESTAMP; }
        else snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.nameIdx).value.toChar());

        switch(v) {
        case PL_FLAG_TYPE_DATA_NONE:
        case PL_FLAG_TYPE_DATA_TIMESTAMP: break;
        case PL_FLAG_TYPE_DATA_S32:       snprintf(valueStr, maxMsgSize, "%d",  evt.vInt); break;
        case PL_FLAG_TYPE_DATA_U32:       snprintf(valueStr, maxMsgSize, "%u",  evt.vU32); break;
        case PL_FLAG_TYPE_DATA_S64:       snprintf(valueStr, maxMsgSize, "%" PRId64, evt.vS64); break;
        case PL_FLAG_TYPE_DATA_U64:       snprintf(valueStr, maxMsgSize, "%" PRIu64, evt.vU64); break;
        case PL_FLAG_TYPE_DATA_FLOAT:     snprintf(valueStr, maxMsgSize, "%f",  evt.vFloat);  break;
        case PL_FLAG_TYPE_DATA_DOUBLE:    snprintf(valueStr, maxMsgSize, "%f",  evt.vDouble); break;
        case PL_FLAG_TYPE_DATA_STRING:
            snprintf(valueStr, maxMsgSize, "%s",  _record->getString(evt.vStringIdx).value.toChar());
            lineQty = bsMax(lineQty, _record->getString(evt.vStringIdx).lineQty);
            break;
        default:                          snprintf(valueStr, maxMsgSize, "<BAD TYPE %d>", v);
        };
        float heightPix = fontHeight+fontHeightIntra*(lineQty-1);

        // Update the mouse time
        if(isWindowHovered && mouseY>y) {
            newMouseTimeNs = sci.timeNs;
        }

        // Update the best fit for the mouse time display (yellow horizontal line)
        if(_mouseTimeNs>=sci.timeNs && sci.timeNs>mouseTimeBestTimeNs) {
            mouseTimeBestTimeNs = sci.timeNs;
            mouseTimeBestY      = y+heightPix;
        }

        // Manage hovering: highlight and clicks
        if(isWindowHovered && mouseY>=y && mouseY<y+heightPix) {
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
                if((ImGui::IsMouseReleased(0)) || tlWheelCounter) {
                    synchronizeNewRange(s.syncMode, bsMax(sci.timeNs-(s64)(0.5*syncTimeRangeNs), 0LL), syncTimeRangeNs);
                    ensureThreadVisibility(s.syncMode, evt.threadId);
                    synchronizeText(s.syncMode, evt.threadId, elem.nestingLevel, sci.lIdx, sci.timeNs, s.uniqueId);
                }
                // Double click: adapt also the scale to have the scope at 10% of the screen
                if(ImGui::IsMouseDoubleClicked(0) && (evt.flags&PL_FLAG_SCOPE_BEGIN)) {
                    s64 newTimeRangeNs =  (s64)(vwConst::DCLICK_RANGE_FACTOR*sci.value);
                    synchronizeNewRange(s.syncMode, syncStartTimeNs+(s64)((double)(sci.timeNs-syncStartTimeNs)/(double)syncTimeRangeNs*(double)(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(s.syncMode, evt.threadId);
                }
                // Zoom the timeline
                if(tlWheelCounter!=0) {
                    s64 newTimeRangeNs = getUpdatedRange(tlWheelCounter, syncTimeRangeNs);
                    synchronizeNewRange(s.syncMode, syncStartTimeNs+(s64)((double)(sci.timeNs-syncStartTimeNs)/(double)syncTimeRangeNs*(double)(syncTimeRangeNs-newTimeRangeNs)),
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
                    if(evt.flags==PL_FLAG_TYPE_LOG) {
                        // Find the log elemIdx suitable for plot/histo
                        u64 itemHashPath = bsHashStepChain(_record->threads[evt.threadId].threadHash, _record->getString(evt.filenameIdx).hash, cmConst::LOG_NAMEIDX);
                        int* elemIdxPtr  = _record->elemPathToId.find(itemHashPath, cmConst::LOG_NAMEIDX);
                        if(elemIdxPtr) prepareGraphLogContextualMenu(*elemIdxPtr, 0LL, _record->durationNs, false);
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
            DRAWLIST->AddRectFilled(ImVec2(winX, y), ImVec2(winX+curScrollPosX+winWidth, y+heightPix), vwConst::uGrey48);
        }

        // Display the date
        float offsetX = winX-curScrollPosX+textPixMargin;
        const char* formTimeStr = getFormattedTimeString(sci.timeNs, timeFormat);
        DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uWhite, formTimeStr);
        int changedOffset = 0;
        while(formTimeStr[changedOffset] && formTimeStr[changedOffset]==s.lastDateStr[changedOffset]) ++changedOffset;
        DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uGrey128, formTimeStr, formTimeStr+changedOffset);
        snprintf(s.lastDateStr, sizeof(s.lastDateStr),"%s", formTimeStr);
        offsetX += charWidth*(float)getFormattedTimeStringCharQty(timeFormat);

        // Display the thread
        snprintf(tmpStr, maxMsgSize, "[%s]", _record->getString(_record->threads[evt.threadId].nameIdx).value.toChar());
        DRAWLIST->AddText(ImVec2(offsetX, y), ImColor(getConfig().getThreadColor(evt.threadId)), tmpStr);
        offsetX += charWidth*(s.maxThreadNameLength+1);

        // Display the name of the item
        DRAWLIST->AddText(ImVec2(offsetX, y), color1, nameStr);
        offsetX += bsMax(ImGui::CalcTextSize(nameStr).x, 20.f*charWidth)+2.f*charWidth;

        // Display the value
        DRAWLIST->AddText(ImVec2(offsetX, y), color1, valueStr);
        offsetX += ImGui::CalcTextSize(valueStr).x;

        // Next line
        if(offsetX>maxOffsetX) maxOffsetX = offsetX;
        if(y>winY+winHeight) break;
        y += heightPix;
    }

    // Drag with middle button
    if(isWindowHovered && ImGui::IsMouseDragging(1)) {
        // Start a range selection
        if(s.rangeSelStartNs<0 && mouseTimeBestTimeNs>=0) {
            s.rangeSelStartNs = mouseTimeBestTimeNs;
            s.rangeSelStartY  = mouseTimeBestY;
        }

        // Drag on-going: display the selection box with transparency and range
        if(s.rangeSelStartNs>=0 && s.rangeSelStartNs<mouseTimeBestTimeNs) {
            char tmpStr[128];
            float y1 = s.rangeSelStartY-fontHeight;
            float y2 = mouseTimeBestY;
            constexpr float arrowSize = 4.f;
            // White background
            DRAWLIST->AddRectFilled(ImVec2(winX, y1), ImVec2(winX+curScrollPosX+winWidth, y2), IM_COL32(255,255,255,128));
            // Range line
            DRAWLIST->AddLine(ImVec2(mouseX, y1), ImVec2(mouseX, y2), vwConst::uBlack, 2.f);
            // Arrows
            DRAWLIST->AddLine(ImVec2(mouseX, y1), ImVec2(mouseX-arrowSize, y1+arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(mouseX, y1), ImVec2(mouseX+arrowSize, y1+arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(mouseX, y2), ImVec2(mouseX-arrowSize, y2-arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(mouseX, y2), ImVec2(mouseX+arrowSize, y2-arrowSize), vwConst::uBlack, 2.f);
            // Text
            snprintf(tmpStr, sizeof(tmpStr), "{ %s }", getNiceDuration(mouseTimeBestTimeNs-s.rangeSelStartNs));
            ImVec2 tb = ImGui::CalcTextSize(tmpStr);
            float x3 = mouseX-0.5f*tb.x;
            DRAWLIST->AddRectFilled(ImVec2(x3-5.f, mouseY-tb.y-5.f), ImVec2(x3+tb.x+5.f, mouseY-5.f), IM_COL32(255,255,255,192));
            DRAWLIST->AddText(ImVec2(x3, mouseY-tb.y-5.f), vwConst::uBlack, tmpStr);
        }
    }
    // Drag ended: set the selected range view
    else if(isWindowHovered && s.rangeSelStartNs>=0) {
        if(s.rangeSelStartNs<mouseTimeBestTimeNs) {
            s64 newRangeNs = mouseTimeBestTimeNs-s.rangeSelStartNs;
            synchronizeNewRange(s.syncMode, s.rangeSelStartNs-(newRangeNs>>4), newRangeNs+(newRangeNs>>3)); // ~12% wider range
        }
        s.rangeSelStartNs = -1;
    }

    // Display and update the mouse time
    if(mouseTimeBestY>=0) {
        DRAWLIST->AddLine(ImVec2(winX, mouseTimeBestY), ImVec2(winX+curScrollPosX+winWidth, mouseTimeBestY), vwConst::uYellow);
    }
    if(newMouseTimeNs>=0.) _mouseTimeNs = newMouseTimeNs;
    if(!ImGui::IsMouseDragging(2)) s.isDragging = false;

    // Contextual menu
    if(!_plotMenuItems.empty() && ImGui::BeginPopup("Search menu", ImGuiWindowFlags_AlwaysAutoResize)) {
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

    // Mark the virtual total size
    s.lastScrollPos = ImGui::GetScrollY();
    ImGui::SetCursorPos(ImVec2(maxOffsetX+curScrollPosX-winX, normalizedScrollHeight));
    plgData(SEARCH, "Current scroll pos", s.lastScrollPos);

    ImGui::EndChild();

    ImGui::End();
}
