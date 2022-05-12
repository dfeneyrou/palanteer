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

// This file implements the log view

// Internal
#include "bsKeycode.h"
#include "cmRecord.h"
#include "cmPrintf.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


#ifndef PL_GROUP_LOG
#define PL_GROUP_LOG 0
#endif



bsString
vwMain::LogView::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "log %d", syncMode);
    return tmpStr;
}


bool
vwMain::addLog(int id, s64 startTimeNs)
{
    _logViews.push_back( { id } );
    _logViews.back().forceTimeNs = startTimeNs;
    setFullScreenView(-1);
    plLogInfo("user", "Add a log view");
    return true;
}


void
vwMain::prepareLog(LogView& lv)
{
    // Check if the cache is still valid
    const float winHeight = ImGui::GetWindowSize().y; // Approximated and bigger anyway
    if(!lv.isCacheDirty && winHeight<=lv.lastWinHeight) return;

    // Worth working
    plgScope(LOG, "prepareLog");
    lv.lastWinHeight = winHeight;
    lv.isCacheDirty  = false;
    lv.cachedItems.clear();

    // Precompute category max length
    lv.maxCategoryLength = 8+2; // For the header word "Category" and a margin
    for(int catIdx=0; catIdx<_record->logCategories.size(); ++catIdx) {
        if(lv.categorySelection[catIdx]) {
            int catNameIdx = _record->logCategories[catIdx];
            int length     = _record->getString(catNameIdx).value.size();
            if(length>lv.maxCategoryLength) lv.maxCategoryLength = length;
        }
    }
    // Precompute thread name max length
    lv.maxThreadNameLength = 6+2; // For the header word "Thread" and a margin
    for(int i=0; i<_record->threads.size(); ++i) {
        if(lv.threadSelection[i]) {
            int length = (int)strlen(getFullThreadName(i));
            if(length>lv.maxThreadNameLength) lv.maxThreadNameLength = length;
        }
    }

    // Compute matching log Elements
    lv.logElemIdxArray.clear();
    for(const cmRecord::LogElem& me : _record->logElems) {
        if(lv.threadSelection[me.threadId] && lv.categorySelection[me.categoryId] && me.logLevel==lv.levelSelection) {
            lv.logElemIdxArray.push_back(me.elemIdx);
        }
    }

    // Resynchronization on a date?
    if(lv.forceTimeNs>=0) {
        lv.startTimeNs = lv.forceTimeNs;
        lv.forceTimeNs = -1;
    }

    // Get the data
    lv.aggregatedIt.init(_record, lv.startTimeNs, 0., lv.logElemIdxArray, {});
    lv.cachedItems.clear();
    int maxLineQty = bsMax(10, 1+winHeight/ImGui::GetTextLineHeightWithSpacing()); // 10 minimum for the page down
    AggCacheItem aggrEvt;
    while((maxLineQty--)>=0 && lv.aggregatedIt.getNextEvent(aggrEvt)) {
        lv.cachedItems.push_back({aggrEvt.evt, aggrEvt.elemIdx, aggrEvt.lineQty, aggrEvt.message});
    }

    // Compute the scroll ratio (for the scroll bar indication) from the dates
    lv.cachedScrollRatio = (float)bsMinMax((double)lv.startTimeNs/(double)bsMax(_record->durationNs, 1), 0., 1.);
}


void
vwMain::drawLogs(void)
{
    if(!_record || _logViews.empty()) return;
    plScope("drawLogs");
    int itemToRemoveIdx = -1;

    for(int logIdx=0; logIdx<_logViews.size(); ++logIdx) {
        auto& logView = _logViews[logIdx];
        if(_uniqueIdFullScreen>=0 && logView.uniqueId!=_uniqueIdFullScreen) continue;

        // Display complete tabs
        char tmpStr[256];
        snprintf(tmpStr, sizeof(tmpStr), "Logs###%d", logView.uniqueId);
        bool isOpen = true;

        if(logView.isWindowSelected) {
            logView.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }
        if(logView.isNew) {
            logView.isNew = false;
            if(logView.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(logView.newDockId);
            else selectBestDockLocation(false, true);
        }
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs)) {
            drawLog(logView);
        }

        // End the window and cleaning
        if(!isOpen) itemToRemoveIdx = logIdx;
        ImGui::End();
    } // End of loop on log views

    // Remove profile if needed
    if(itemToRemoveIdx>=0) {
        releaseId((_logViews.begin()+itemToRemoveIdx)->uniqueId);
        _logViews.erase(_logViews.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::drawLog(LogView& lv)
{
    plgScope(LOG, "drawLog");

    // Display the thread name
    float fontHeight    = ImGui::GetTextLineHeightWithSpacing();
    float fontHeightIntra = ImGui::GetTextLineHeight();
    float textPixMargin = ImGui::GetStyle().ItemSpacing.x;
    float charWidth     = ImGui::CalcTextSize("0").x;
    float comboWidth    = ImGui::CalcTextSize("Isolated XXX").x;
    float textBgY       = ImGui::GetWindowPos().y+ImGui::GetCursorPos().y;
    float comboX        = ImGui::GetWindowContentRegionMax().x-comboWidth;
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+ImGui::GetCursorPos().x-2.f, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+comboX, textBgY+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetStyle().FramePadding.y), vwConst::uGrey48);

    // Configuration and filtering menu
    // Sanity
    while(lv.threadSelection.size()<_record->threads.size())         lv.threadSelection.push_back(true);
    while(lv.categorySelection.size()<_record->logCategories.size()) lv.categorySelection.push_back(true);
    float padMenuX    = ImGui::GetStyle().FramePadding.x;
    ImU32 filterBg    = ImColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

    // Get date format
    int timeFormat = getConfig().getTimeFormat();
    float offsetMenuX = ImGui::GetStyle().ItemSpacing.x+padMenuX + charWidth*(float)getFormattedTimeStringCharQty(timeFormat);

    // Level filtering
    float widthMenu = ImGui::CalcTextSize("Level").x;
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+offsetMenuX-padMenuX, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+offsetMenuX+widthMenu+padMenuX, textBgY+ImGui::GetTextLineHeightWithSpacing()), filterBg);
    ImGui::SameLine(offsetMenuX);
    ImGui::AlignTextToFramePadding();
    if(ImGui::Selectable("Level", false, 0, ImVec2(widthMenu, 0))) ImGui::OpenPopup("Level log menu");
    if(ImGui::BeginPopup("Level log menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::RadioButton("Debug", &lv.levelSelection, 0)) { lv.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
        if(ImGui::RadioButton("Info",  &lv.levelSelection, 1)) { lv.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
        if(ImGui::RadioButton("Warn",  &lv.levelSelection, 2)) { lv.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
        if(ImGui::RadioButton("Error", &lv.levelSelection, 3)) { lv.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    offsetMenuX += charWidth*8.f;

    // Thread filtering
    widthMenu = ImGui::CalcTextSize("Thread").x;
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+offsetMenuX-padMenuX, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+offsetMenuX+widthMenu+padMenuX, textBgY+ImGui::GetTextLineHeightWithSpacing()), filterBg);
    ImGui::SameLine(offsetMenuX);
    if(lv.isFilteredOnThread) ImGui::PushStyleColor(ImGuiCol_Text, vwConst::gold);
    if(ImGui::Selectable("Thread", false, 0, ImVec2(widthMenu, 0))) ImGui::OpenPopup("Thread log menu");
    if(lv.isFilteredOnThread) ImGui::PopStyleColor();
    if(ImGui::BeginPopup("Thread log menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        // Global selection
        bool forceSelectAll   = ImGui::Selectable("Select all",   false, ImGuiSelectableFlags_DontClosePopups);
        bool forceDeselectAll = ImGui::Selectable("Deselect all", false, ImGuiSelectableFlags_DontClosePopups);
        ImGui::Separator();

        // Individual selection
        lv.isFilteredOnThread = false;
        // Loop on thread layout instead of direct thread list, as the layout have sorted threads
        for(int layoutIdx=0; layoutIdx<getConfig().getLayout().size(); ++layoutIdx) {
            const vwConfig::ThreadLayout& ti = getConfig().getLayout()[layoutIdx];
            if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue;
            if(ImGui::Checkbox(getFullThreadName(ti.threadId), &lv.threadSelection[ti.threadId])) lv.isCacheDirty = true;
            if(forceSelectAll   && !lv.threadSelection[ti.threadId]) { lv.threadSelection[ti.threadId] = true;  lv.isCacheDirty = true; }
            if(forceDeselectAll &&  lv.threadSelection[ti.threadId]) { lv.threadSelection[ti.threadId] = false; lv.isCacheDirty = true; }
            if(!lv.threadSelection[ti.threadId]) lv.isFilteredOnThread = true;
        }
        ImGui::EndPopup();
    }
    offsetMenuX += charWidth*(lv.maxThreadNameLength+1);

    // Category filtering
    widthMenu = ImGui::CalcTextSize("Category").x;
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+offsetMenuX-padMenuX, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+offsetMenuX+widthMenu+padMenuX, textBgY+ImGui::GetTextLineHeightWithSpacing()), filterBg);
    ImGui::SameLine(offsetMenuX);
    if(lv.isFilteredOnCategory) ImGui::PushStyleColor(ImGuiCol_Text, vwConst::gold);
    if(ImGui::Selectable("Category", false, 0, ImVec2(widthMenu, 0))) ImGui::OpenPopup("Category log menu");
    if(lv.isFilteredOnCategory) ImGui::PopStyleColor();
    if(ImGui::BeginPopup("Category log menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        // Global selection
        bool forceSelectAll   = ImGui::Selectable("Select all",   false, ImGuiSelectableFlags_DontClosePopups);
        bool forceDeselectAll = ImGui::Selectable("Deselect all", false, ImGuiSelectableFlags_DontClosePopups);
        ImGui::Separator();

        // Individual selection
        lv.isFilteredOnCategory = false;
        for(int i=0; i<_record->logCategories.size(); ++i) {
            if(ImGui::Checkbox(_record->getString(_record->logCategories[i]).value.toChar(), &lv.categorySelection[i])) lv.isCacheDirty = true;
            if(forceSelectAll   && !lv.categorySelection[i]) { lv.categorySelection[i] = true;  lv.isCacheDirty = true; }
            if(forceDeselectAll &&  lv.categorySelection[i]) { lv.categorySelection[i] = false; lv.isCacheDirty = true; }
            if(!lv.categorySelection[i]) lv.isFilteredOnCategory = true;
        }
        ImGui::EndPopup();
    }

    // Sync combo
    ImGui::SameLine(comboX);
    drawSynchroGroupCombo(comboWidth, &lv.syncMode);
    ImGui::Separator();

    // Some init
    ImGui::BeginChild("log", ImVec2(0,0), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar |
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);  // Display area is virtual so self-managed
    prepareLog(lv); // Ensure cache is up to date, even after window creation
    const float winX = ImGui::GetWindowPos().x;
    const float winY = ImGui::GetWindowPos().y;
    const float winWidth  = ImGui::GetWindowContentRegionMax().x;
    const float winHeight = ImGui::GetWindowSize().y;
    const float mouseX    = ImGui::GetMousePos().x;
    const float mouseY    = ImGui::GetMousePos().y;
    const bool isWindowHovered = ImGui::IsWindowHovered();

    // Get keyboard focus on window hovering
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && !_search.isInputPopupOpen && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGui::SetWindowFocus();
    }

    constexpr int maxMsgSize = 256;
    char tmpStr [maxMsgSize];
    lv.lastDateStr[0] = 0;  // No previous displayed date

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    const double normalizedScrollHeight = 1000000.; // Value does not really matter, it just defines the granularity
    float curScrollPosX = ImGui::GetScrollX();
    float curScrollPosY = ImGui::GetScrollY();
    if(!lv.didUserChangedScrollPos && bsAbs(curScrollPosY-lv.lastScrollPos)>=1.) {
        plgScope(LOG, "New user scroll position from ImGui");
        plgData(LOG, "expected pos", lv.lastScrollPos);
        plgData(LOG, "new pos", curScrollPosY);
        lv.cachedScrollRatio = (float)(curScrollPosY/normalizedScrollHeight);
        lv.setStartPosition((s64)(lv.cachedScrollRatio*_record->durationNs));
        lv.didUserChangedScrollPos = false;
    }

    // Manage keys and mouse inputs
    // ============================
    lv.didUserChangedScrollPos = false;

    int tlWheelCounter = 0;
    if(isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Check mouse input
        int textWheelCounter =  (ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getVWheelInversion()); // No Ctrl key: wheel is for the text
        tlWheelCounter       = (!ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getHWheelInversion()); // Ctrl key: wheel is for the timeline (processed in highlighted text display)
        int  dragLineQty = 0;
        if(ImGui::IsMouseDragging(2)) {
            lv.isDragging = true;
            if(bsAbs(ImGui::GetMouseDragDelta(2).y)>1.) {
                float tmp = (ImGui::GetMouseDragDelta(2).y+lv.dragReminder);
                ImGui::ResetMouseDragDelta(2);
                dragLineQty      = (int)(tmp/fontHeight);
                lv.dragReminder = tmp-fontHeight*dragLineQty;
            }
        } else lv.dragReminder = 0.;

        // Move start position depending on keys, wheel or drag
        if(ImGui::IsKeyPressed(KC_Down)) {
            plgText(LOG, "Key", "Down pressed");
            if(lv.cachedItems.size()>=2) {
                lv.setStartPosition(lv.cachedItems[1].evt.vS64);
            }
        }

        if(ImGui::IsKeyPressed(KC_Up)) {
            plgText(LOG, "Key", "Up pressed");
            s64 newTimeNs = lv.aggregatedIt.getPreviousTime(1);
            if(newTimeNs>=0) {
                lv.setStartPosition(newTimeNs);
            }
        }

        if(textWheelCounter<0 || dragLineQty<0 || ImGui::IsKeyPressed(KC_PageDown)) {
            plgText(LOG, "Key", "Page Down pressed");
            const int steps = bsMin((dragLineQty!=0)?-dragLineQty:10, lv.cachedItems.size()-1);
            if(steps>0 && steps<lv.cachedItems.size()) {
                lv.setStartPosition(lv.cachedItems[steps].evt.vS64);
            }
        }

        if(textWheelCounter>0 || dragLineQty>0 || ImGui::IsKeyPressed(KC_PageUp)) {
            plgText(LOG, "Key", "Page Up pressed");
            const int steps = (dragLineQty!=0)? dragLineQty : 10;
            s64 newTimeNs = lv.aggregatedIt.getPreviousTime(steps);
            if(newTimeNs>=0) {
                lv.setStartPosition(newTimeNs);
            }
        }

        if(!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
            plgText(LOG, "Key", "Full screen pressed");
            setFullScreenView(lv.uniqueId);
        }

        if(!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_H)) {
            plgText(LOG, "Key", "Help pressed");
            openHelpTooltip(lv.uniqueId, "Help Log");
        }
    }
    else lv.dragReminder = 0.;

    // Prepare the drawing
    // ===================
    // Previous navigation may have made dirty the cached data
    prepareLog(lv);

    // Set the modified scroll position in ImGui, if not changed through imGui
    if(lv.didUserChangedScrollPos) {
        plgData(LOG, "Set new scroll pos from user", lv.cachedScrollRatio*normalizedScrollHeight);
        ImGui::SetScrollY((float)(lv.cachedScrollRatio*normalizedScrollHeight));
    }

    // Compute initial state for all levels
    const bsVec<ImVec4>& palette = getConfig().getColorPalette(true);

    // Draw the log
    // =============
    float y = winY;
    float mouseTimeBestY      = -1.;
    float maxOffsetX          = 0.;
    s64   mouseTimeBestTimeNs = -1;
    s64   newMouseTimeNs      = -1;
    for(const auto& ci : lv.cachedItems) {
        const cmRecord::Evt& evt = ci.evt;
        const bsString& s = ci.message;
        float heightPix = fontHeight+fontHeightIntra*(ci.lineQty-1);

        // Manage hovering: highlight and clicks
        bool doHighlight = isScopeHighlighted(evt.threadId, evt.vS64, evt.flags, -1, evt.nameIdx);

        if(isWindowHovered && mouseY>=y && mouseY<y+heightPix) {
            // Synchronized navigation
            if(lv.syncMode>0) { // No synchronized navigation for isolated windows
                s64 syncStartTimeNs, syncTimeRangeNs;
                getSynchronizedRange(lv.syncMode, syncStartTimeNs, syncTimeRangeNs);

                // Click: set timeline position at middle screen only if outside the center third of screen
                if((ImGui::IsMouseReleased(0) && ImGui::GetMousePos().x<winX+winWidth) || tlWheelCounter) {
                    synchronizeNewRange(lv.syncMode, bsMax(evt.vS64-(s64)(0.5*syncTimeRangeNs), 0LL), syncTimeRangeNs);
                    ensureThreadVisibility(lv.syncMode, evt.threadId);
                    synchronizeText(lv.syncMode, evt.threadId, -1, PL_INVALID, evt.vS64, lv.uniqueId);
                }

                // Zoom the timeline
                if(tlWheelCounter!=0) {
                    s64 newTimeRangeNs = getUpdatedRange(tlWheelCounter, syncTimeRangeNs);
                    synchronizeNewRange(lv.syncMode, syncStartTimeNs+(s64)((double)(evt.vS64-syncStartTimeNs)/syncTimeRangeNs*(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(lv.syncMode, evt.threadId);
                }
            }

            // Right click: contextual menu
            if(!lv.isDragging && ImGui::IsMouseReleased(2) && ci.elemIdx>=0) {
                lv.ctxThreadId = evt.threadId;
                lv.ctxNameIdx  = evt.nameIdx;
                _plotMenuItems.clear(); // Reset the popup menu state
                u64 itemHashPath = bsHashStepChain(_record->threads[evt.threadId].threadHash, _record->getString(evt.filenameIdx).hash, cmConst::LOG_NAMEIDX);
                int* elemIdxPtr  = _record->elemPathToId.find(itemHashPath, cmConst::LOG_NAMEIDX);
                if(elemIdxPtr) {
                    prepareGraphLogContextualMenu(*elemIdxPtr, 0LL, _record->durationNs, false);
                    ImGui::OpenPopup("log menu");
                }
            }

            setScopeHighlight(evt.threadId, evt.vS64, evt.flags, -1, evt.nameIdx);
            doHighlight = true;
        }

        if(doHighlight) {
            // Display some text background if highlighted
            DRAWLIST->AddRectFilled(ImVec2(winX, y), ImVec2(winX+curScrollPosX+winWidth, y+heightPix), vwConst::uGrey48);
        }

        // Display the date
        float offsetX = winX-curScrollPosX+textPixMargin;
        const char* timeStr = getFormattedTimeString(evt.vS64, timeFormat);
        DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uWhite, timeStr);
        int changedOffset = 0;
        while(timeStr[changedOffset] && timeStr[changedOffset]==lv.lastDateStr[changedOffset]) ++changedOffset;
        DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uGrey128, timeStr, timeStr+changedOffset);
        snprintf(lv.lastDateStr, sizeof(lv.lastDateStr),"%s", timeStr);
        offsetX += charWidth*(float)getFormattedTimeStringCharQty(timeFormat);

        // Display the level
        const char* levelStr = 0; ImU32 levelColor = 0;
        switch(evt.lineNbr&0x7FFF) {
        case 0: levelStr = "debug"; levelColor = vwConst::uGrey; break;
        case 1: levelStr = "info";  levelColor = vwConst::uCyan; break;
        case 2: levelStr = "warn";  levelColor = vwConst::uDarkOrange; break;
        case 3: levelStr = "error"; levelColor = vwConst::uRed; break;
        default: levelStr = "";     levelColor = vwConst::uWhite;
        };
        snprintf(tmpStr, maxMsgSize, "[%s]", getFullThreadName(evt.threadId));
        DRAWLIST->AddText(ImVec2(offsetX, y), levelColor, levelStr);
        offsetX += charWidth*8;

        // Display the thread
        snprintf(tmpStr, maxMsgSize, "[%s]", getFullThreadName(evt.threadId));
        DRAWLIST->AddText(ImVec2(offsetX, y), ImColor(getConfig().getThreadColor(evt.threadId, true)), tmpStr);
        offsetX += charWidth*(lv.maxThreadNameLength+1);

        // Display the category
        DRAWLIST->AddText(ImVec2(offsetX, y), (ci.elemIdx>=0)? (ImU32)ImColor(getConfig().getCurveColor(ci.elemIdx, true)) : vwConst::uGrey,
                          _record->getString(evt.nameIdx).value.toChar());
        offsetX += charWidth*(lv.maxCategoryLength+1);

        // Display the value
        DRAWLIST->AddText(ImVec2(offsetX, y), ImColor(palette[evt.filenameIdx%palette.size()]), s.toChar());
        offsetX += ImGui::CalcTextSize(s.toChar()).x;

        if(isWindowHovered && mouseY>y) newMouseTimeNs = evt.vS64;
        if(_mouseTimeNs>=evt.vS64 && evt.vS64>mouseTimeBestTimeNs) {
            mouseTimeBestTimeNs = evt.vS64;
            mouseTimeBestY      = y+heightPix;
        }

        // Next line
        if(offsetX>maxOffsetX) maxOffsetX = offsetX;
        if(y>winY+winHeight) break;
        y += heightPix;
    }

    // Drag with middle button
    if(isWindowHovered && ImGui::IsMouseDragging(1)) {
        // Start a range selection
        if(lv.rangeSelStartNs<0 && mouseTimeBestTimeNs>=0) {
            lv.rangeSelStartNs = mouseTimeBestTimeNs;
            lv.rangeSelStartY  = mouseTimeBestY;
        }

        // Drag on-going: display the selection box with transparency and range
        if(lv.rangeSelStartNs>=0 && lv.rangeSelStartNs<mouseTimeBestTimeNs) {
            float y1 = lv.rangeSelStartY-fontHeight;
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
            snprintf(tmpStr, sizeof(tmpStr), "{ %s }", getNiceDuration(mouseTimeBestTimeNs-lv.rangeSelStartNs));
            ImVec2 tb = ImGui::CalcTextSize(tmpStr);
            float x3 = mouseX-0.5f*tb.x;
            DRAWLIST->AddRectFilled(ImVec2(x3-5.f, mouseY-tb.y-5.f), ImVec2(x3+tb.x+5.f, mouseY-5.f), IM_COL32(255,255,255,192));
            DRAWLIST->AddText(ImVec2(x3, mouseY-tb.y-5.f), vwConst::uBlack, tmpStr);
        }
    }
    // Drag ended: set the selected range view
    else if(isWindowHovered && lv.rangeSelStartNs>=0) {
        if(lv.rangeSelStartNs<mouseTimeBestTimeNs) {
            s64 newRangeNs = mouseTimeBestTimeNs-lv.rangeSelStartNs;
            synchronizeNewRange(lv.syncMode, lv.rangeSelStartNs-(newRangeNs>>4), newRangeNs+(newRangeNs>>3)); // ~12% wider range
        }
        lv.rangeSelStartNs = -1;
    }

    // Display and update the mouse time
    if(mouseTimeBestY>=0) {
        DRAWLIST->AddLine(ImVec2(winX, mouseTimeBestY), ImVec2(winX+curScrollPosX+winWidth, mouseTimeBestY), vwConst::uYellow);
    }
    if(newMouseTimeNs>=0) _mouseTimeNs = newMouseTimeNs;
    if(!ImGui::IsMouseDragging(2)) lv.isDragging = false;


    // Contextual menu
    if(ImGui::BeginPopup("log menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5.f;
        ImGui::TextColored(vwConst::grey, "Log [%s]", _record->getString(lv.ctxNameIdx).value.toChar());
        ImGui::Separator();
        ImGui::Separator();

        // Plot & histogram menu
        if(!_plotMenuItems.empty()) {
            if(!displayPlotContextualMenu(lv.ctxThreadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
            ImGui::Separator();
            if(!displayHistoContextualMenu(headerWidth)) ImGui::CloseCurrentPopup();
            ImGui::Separator();
        }

        // Export
        if(ImGui::BeginMenu("Export in a text file...")) {
            if(ImGui::MenuItem("the content of this window")) {
                initiateExportLog(lv.logElemIdxArray, lv.startTimeNs, -1, bsMax(1, (int)(winHeight/fontHeight)));
            }
            if(lv.syncMode!=0 && ImGui::MenuItem("the time range of the group")) {
                s64 startTimeNs, timeRangeNs;
                vwMain::getSynchronizedRange(lv.syncMode, startTimeNs, timeRangeNs);
                initiateExportLog(lv.logElemIdxArray, startTimeNs, startTimeNs+timeRangeNs, -1);
            }
            if(ImGui::MenuItem("the content of the full thread")) {
                initiateExportLog(lv.logElemIdxArray, 0, -1, -1);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    // Help
    displayHelpTooltip(lv.uniqueId, "Help Log",
                       "##Log view\n"
                       "===\n"
                       "Displays the global list of logs with filters on categories, levels and threads.\n"
                       "\n"
                       "##Actions:\n"
                       "-#H key#| This help\n"
                       "-#F key#| Full screen view\n"
                       "-#Right mouse button dragging#| Scroll text\n"
                       "-#Up/Down key#| Scroll text\n"
                       "-#Mouse wheel#| Scroll text faster\n"
                       "-#Ctrl-Mouse wheel#| Time zoom views of the same group\n"
                       "-#Left mouse click#| Time synchronize views of the same group\n"
                       "-#Middle button mouse dragging#| Measure/select a time range\n"
                       "-#Right mouse click#| Open menu for plot/histogram on log parameters\n"
                       "\n"
                       );

    // Mark the virtual total size
    lv.lastScrollPos = ImGui::GetScrollY();
    ImGui::SetCursorPos(ImVec2(maxOffsetX+curScrollPosX-winX, normalizedScrollHeight));
    plgData(LOG, "Current scroll pos", lv.lastScrollPos);

    ImGui::EndChild();
}
