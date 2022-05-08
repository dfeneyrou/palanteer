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

// This file implements the text view

// System
#include <cinttypes>

// Internal
#include "bsKeycode.h"
#include "cmRecord.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


#ifndef PL_GROUP_TEXT
#define PL_GROUP_TEXT 0
#endif


bsString
vwMain::Text::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "text %d %" PRIX64, syncMode, threadUniqueHash);
    return tmpStr;
}


bool
vwMain::addText(int id, int threadId, u64 threadUniqueHash, int startNestingLevel, u32 startLIdx)
{
    // Either threadId<0 and the hash shall be known (for live case, the threadId can be discovered later)
    // Either threadId>=0 and a null hash can be deduced
    if(threadId<0) {
        for(int i=0; i<_record->threads.size(); ++i) {
            if(_record->threads[i].threadUniqueHash!=threadUniqueHash) continue;
            threadId = i;
            break;
        }
        if(threadId<0) return false;  // No text view added
    }
    if(threadUniqueHash==0) {
        plAssert(threadId>=0, threadId);
        threadUniqueHash = _record->threads[threadId].threadUniqueHash;
    }
    _texts.push_back( { id, threadId, threadUniqueHash, startNestingLevel, startLIdx } );
    setFullScreenView(-1);
    plLogInfo("user", "Add a text view");
    return true;
}


void
vwMain::prepareText(Text& t)
{
    // Check if the cache is still valid
    const float winHeight = ImGui::GetWindowSize().y; // Approximated and bigger anyway
    if(!t.isCacheDirty && winHeight<=t.lastWinHeight) return;

    // Worth working
    plgScope(TEXT, "prepareText");
    t.lastWinHeight = winHeight;
    t.isCacheDirty  = false;
    t.cachedItems.clear();

    // Shall we discover the thread id (init and live)?
    if(t.threadId<0 && (t.isFirstRun || _liveRecordUpdated)) {
        t.isFirstRun = false;
        for(int i=0; i<_record->threads.size(); ++i) {
            if(_record->threads[i].threadUniqueHash!=t.threadUniqueHash) continue;
            t.threadId = i;
            break;
        }
    }
    if(t.threadId<0) return; // Target thread not seen yet

    // Get information on the context of the start item (global position, nesting parents, ...)
    cmRecordIteratorHierarchy it(_record, t.threadId, t.startNLevel, t.startLIdx);
    auto& csp = t.cachedStartParents;
    it.getParents(csp);
    if(csp.empty()) return;

    // Manage the scrollbar and its virtual position
    // Provide the start date to highlight in the timeline. If the current item has no date, we keep the previous one
    int eType = csp[0].evt.flags&PL_FLAG_TYPE_MASK;
    if(eType==PL_FLAG_TYPE_DATA_TIMESTAMP || (eType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && eType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST)) {
        t.firstTimeNs = t.lastTimeNs = csp[0].evt.vS64;
    }
    t.cachedScrollRatio = (float)bsMinMax((double)t.firstTimeNs/(double)bsMax(_record->durationNs, 1), 0., 1.);

    // Compute the hash chain to get the Elem and eventually the color
    u64 hashPathPerLevel[cmConst::MAX_LEVEL_QTY+1];
    int level = 0;
    hashPathPerLevel[level] = bsHashStep(cmConst::SCOPE_NAMEIDX);
    for(int i=csp.size()-1; i>=0; --i) { // Loop from root to deepest element
        const cmRecord::Evt& pEvt = t.cachedStartParents[i].evt;
        if((i>0 && (pEvt.flags&PL_FLAG_SCOPE_MASK)==PL_FLAG_SCOPE_BEGIN) || (i==0 && (pEvt.flags&PL_FLAG_SCOPE_MASK)==PL_FLAG_SCOPE_END)) {
            hashPathPerLevel[level+1] = bsHashStep(_record->getString(pEvt.nameIdx).hash, hashPathPerLevel[level]);
            ++level;
        }
    }

    // Compute items to display
    const float fontHeight = ImGui::GetTextLineHeightWithSpacing();
    float y = 0.f;
    while(y<winHeight) {
        // Get the next item
        int nestingLevel; u32 lIdx; cmRecord::Evt evt; s64 scopeEndTimeNs;
        if(!it.getItem(nestingLevel, lIdx, evt, scopeEndTimeNs)) break;
        int flags = evt.flags;

        // End of scope: update level and the scope end time, not set in this case
        if((flags&PL_FLAG_SCOPE_MASK)==PL_FLAG_SCOPE_END) {
            if(level>0) --level;
            scopeEndTimeNs = evt.vS64;
        }

        // Compute the elemIdx
        u64 hashPath     = bsHashStep(_record->getString(evt.nameIdx).hash, hashPathPerLevel[level]);
        int hashFlags    = (flags&PL_FLAG_SCOPE_END)? ((flags&PL_FLAG_TYPE_MASK)|PL_FLAG_SCOPE_BEGIN) : flags; // Replace END scope with BEGIN scope (1 plot for both)
        u64 itemHashPath = bsHashStep(hashFlags, hashPath);
        itemHashPath     = bsHashStep(_record->threads[t.threadId].threadHash, itemHashPath);
        int* elemIdx     = _record->elemPathToId.find(itemHashPath, evt.nameIdx);
        if((flags&PL_FLAG_SCOPE_MASK)==PL_FLAG_SCOPE_BEGIN) {
            plAssert(level<cmConst::MAX_LEVEL_QTY);
            hashPathPerLevel[++level] = hashPath;
        }

        // Store
        if(elemIdx) {  // The element may not exist in case of live display if a block is not finished
            t.cachedItems.push_back( { evt, scopeEndTimeNs, nestingLevel, lIdx, elemIdx? *elemIdx : -1 } );
        }

        // Update the "last date", used to display the time footprint in the timelines
        eType = (flags&PL_FLAG_TYPE_MASK);
        if((flags&PL_FLAG_SCOPE_MASK) || (eType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && eType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST)) t.lastTimeNs = evt.vS64;

        y += fontHeight;
    }
}


void
vwMain::drawTexts(void)
{
    if(!_record || _texts.empty()) return;
    plScope("drawTexts");
    int itemToRemoveIdx = -1;

    for(int textIdx=0; textIdx<_texts.size(); ++textIdx) {
        auto& text = _texts[textIdx];
        if(_liveRecordUpdated) text.isCacheDirty = true;
        if(_uniqueIdFullScreen>=0 && text.uniqueId!=_uniqueIdFullScreen) continue;

        // Display complete tabs
        char tmpStr[256];
        snprintf(tmpStr, sizeof(tmpStr), "Text [%s]###%d", (text.threadId>=0)? getFullThreadName(text.threadId) : "(Not present)", text.uniqueId);
        bool isOpen = true;

        // Configure the tab with the thread color
        bool hasColoredTab =(text.threadId>=0);
        if(hasColoredTab) {
            const ImVec4 c = getConfig().getThreadColor(text.threadId);
            float a;
            a = 1.1f; ImGui::PushStyleColor(ImGuiCol_TabActive,          ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 1.4f; ImGui::PushStyleColor(ImGuiCol_TabHovered,         ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 0.4f; ImGui::PushStyleColor(ImGuiCol_Tab,                ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 0.4f; ImGui::PushStyleColor(ImGuiCol_TabUnfocused,       ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 0.5f; ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 0.4f; ImGui::PushStyleColor(ImGuiCol_TitleBg,            ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
            a = 1.1f; ImGui::PushStyleColor(ImGuiCol_TitleBgActive,      ImVec4(a*c.x, a*c.y, a*c.z, 1.f));
        }

        if(text.isWindowSelected) {
            text.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }
        if(text.isNew) {
            text.isNew = false;
            if(text.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(text.newDockId);
            else selectBestDockLocation(false, true);
        }
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs)) {
            drawText(text);
        }

        // End the window and cleaning
        if(!isOpen) itemToRemoveIdx = textIdx;
        ImGui::End();
        if(hasColoredTab) ImGui::PopStyleColor(7);
    } // End of loop on texts

    // Remove profile if needed
    if(itemToRemoveIdx>=0) {
        releaseId((_texts.begin()+itemToRemoveIdx)->uniqueId);
        _texts.erase(_texts.begin()+itemToRemoveIdx);
        setFullScreenView(-1);
    }
}


void
vwMain::drawText(Text& t)
{
    plgScope(TEXT, "drawText");

    // Display the thread name
    float  comboWidth   = ImGui::CalcTextSize("Isolated XXX").x;
    float   textBgY      = ImGui::GetWindowPos().y+ImGui::GetCursorPos().y;
    float   textbgHeight = textBgY+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetStyle().FramePadding.y;
    float   comboX       = ImGui::GetWindowContentRegionMax().x-comboWidth;
    DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+ImGui::GetCursorPos().x-2.f, textBgY),
                            ImVec2(ImGui::GetWindowPos().x+comboX, textbgHeight), vwConst::uGrey48);
    ImGui::AlignTextToFramePadding();
    ImGui::Text(" [%s]", (t.threadId>=0)? getFullThreadName(t.threadId): "(Not present)");

    // Sync combo
    ImGui::SameLine(comboX);
    drawSynchroGroupCombo(comboWidth, &t.syncMode);
    ImGui::Separator();

    // Some init
    ImGui::BeginChild("text", ImVec2(0,0), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar |
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);  // Display area is virtual so self-managed
    prepareText(t); // Ensure cache is up to date, even after window creation
    if(t.cachedStartParents.empty()) { ImGui::EndChild(); return; } // Sanity
    const float winX = ImGui::GetWindowPos().x;
    const float winY = ImGui::GetWindowPos().y;
    const float winWidth      = ImGui::GetWindowContentRegionMax().x;
    const float winHeight     = ImGui::GetWindowSize().y;
    const float fontHeight    = ImGui::GetTextLineHeightWithSpacing();
    const float fontHeightIntra = ImGui::GetTextLineHeight();
    const float textPixMargin = ImGui::GetStyle().ItemSpacing.x;
    const float mouseX        = ImGui::GetMousePos().x;
    const float mouseY        = ImGui::GetMousePos().y;
    const double normalizedScrollHeight = 1000000.; // Value does not really matter, it just defines the granularity
    const float  darkCoef = 0.7f;
    const bool isWindowHovered = ImGui::IsWindowHovered();

    const float charWidth = ImGui::CalcTextSize("0").x;
    constexpr int maxMsgSize = 256;
    char timeStr [maxMsgSize];
    char nameStr [maxMsgSize];
    char valueStr[maxMsgSize];

    // Get keyboard focus on window hovering
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow) && !_search.isInputPopupOpen && !ImGui::IsWindowFocused()) {
        ImGui::SetWindowFocus();
    }

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    float curScrollPosX = ImGui::GetScrollX();
    float lastScrollPosY = ImGui::GetScrollY();
    if(!t.didUserChangedScrollPos && !t.didUserChangedScrollPosExt && bsAbs(lastScrollPosY-t.lastScrollPosY)>=1.) {
        plgScope(TEXT, "New user scroll position from ImGui");
        plgData(TEXT, "expected pos", t.lastScrollPosY);
        plgData(TEXT, "new pos", lastScrollPosY);
        int nestingLevel;
        u32 lIdx;
        cmGetRecordPosition(_record, t.threadId, (s64)(lastScrollPosY/normalizedScrollHeight*_record->durationNs), nestingLevel, lIdx);
        t.setStartPosition(nestingLevel, lIdx);
    }

    // Manage keys and mouse inputs
    // ============================
    t.didUserChangedScrollPos = t.didUserChangedScrollPosExt;
    t.didUserChangedScrollPosExt = false;
    int tlWheelCounter = 0;
    if(isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Check mouse input
        int textWheelCounter =  (ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getVWheelInversion()); // No Ctrl key: wheel is for the text
        tlWheelCounter       = (!ImGui::GetIO().KeyCtrl)? 0 :
            (int)(ImGui::GetIO().MouseWheel*getConfig().getHWheelInversion()); // Ctrl key: wheel is for the timeline (processed in highlighted text display)
        cmRecord::Evt nextEvt;
        int dragLineQty = 0;
        if(ImGui::IsMouseDragging(2)) {
            t.isDragging = true;
            if(bsAbs(ImGui::GetMouseDragDelta(2).y)>1.) {
                float tmp = (ImGui::GetMouseDragDelta(2).y+t.dragReminder);
                ImGui::ResetMouseDragDelta(2);
                dragLineQty   = (int)(tmp/fontHeight);
                t.dragReminder = tmp-fontHeight*dragLineQty;
            }
        } else t.dragReminder = 0.f;

        // Move start position depending on keys, wheel or drag
        if(ImGui::IsKeyPressed(KC_Down)) {
            plgText(TEXT, "Key", "Down pressed");
            cmRecordIteratorHierarchy it(_record, t.threadId, t.startNLevel, t.startLIdx);
            int nestingLevel;
            u32 lIdx;
            s64 scopeEndTimeNs;
            it.getItem(nestingLevel, lIdx, nextEvt, scopeEndTimeNs);
            if(it.getItem(nestingLevel, lIdx, nextEvt, scopeEndTimeNs)) {
                t.setStartPosition(it.getNestingLevel(), it.getLIdx());
                t.didUserChangedScrollPos = true;
            }
        }

        if(ImGui::IsKeyPressed(KC_Up)) {
            plgText(TEXT, "Key", " Up pressed");
            cmRecordIteratorHierarchy it(_record, t.threadId, t.startNLevel, t.startLIdx);
            it.rewind();
            t.setStartPosition(it.getNestingLevel(), it.getLIdx());
            t.didUserChangedScrollPos = true;
        }

        if(textWheelCounter<0 || dragLineQty<0 || ImGui::IsKeyPressed(KC_PageDown)) {
            plgText(TEXT, "Key", "Page Down pressed");
            cmRecordIteratorHierarchy it(_record, t.threadId, t.startNLevel, t.startLIdx);
            int nestingLevel;
            u32 lIdx;
            s64 scopeEndTimeNs;
            const int steps = 1+((dragLineQty!=0)?-dragLineQty:10);  // +1 as we need to consume the current one
            for(int i=0; i<steps; ++i) {
                if(!it.getItem(nestingLevel, lIdx, nextEvt, scopeEndTimeNs)) break;
                t.setStartPosition(it.getNestingLevel(), it.getLIdx());
                t.didUserChangedScrollPos = true;
            }
        }

        if(textWheelCounter>0 || dragLineQty>0 || ImGui::IsKeyPressed(KC_PageUp)) {
            plgText(TEXT, "Key", "Page Up pressed");
            cmRecordIteratorHierarchy it(_record, t.threadId, t.startNLevel, t.startLIdx);
            const int steps = (dragLineQty!=0)?dragLineQty:10;
            for(int i=0; i<steps; ++i) it.rewind();
            t.setStartPosition(it.getNestingLevel(), it.getLIdx());
            t.didUserChangedScrollPos = true;
        }

        if(!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
            plgText(TEXT, "Key", "Full screen pressed");
            setFullScreenView(t.uniqueId);
        }

        if(!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_H)) {
            plgText(TEXT, "Key", "Help pressed");
            openHelpTooltip(t.uniqueId, "Help Text");
        }
    }
    else t.dragReminder = 0.;

    // Prepare the drawing
    // ===================
    // Previous navigation may have made dirty the cached data
    prepareText(t);

    // Set the modified scroll position in ImGui, if not changed through imGui
    if(t.didUserChangedScrollPos) {
        plgData(TEXT, "Set new scroll pos from user", t.cachedScrollRatio*normalizedScrollHeight);
        ImGui::SetScrollY((float)(t.cachedScrollRatio*normalizedScrollHeight));
    }

    // Compute initial state for all levels
    const bsVec<ImVec4>& palette = getConfig().getColorPalette(true);

    struct {
        ImU32 color;
        int   yScopeStart = -1;
        int   nameIdx     = -1;
        int   flags       =  0;
        u32   lIdx        =  0;
        s64   scopeStartTimeNs = -1;
        s64   scopeEndTimeNs   = -1;
    } levelElems[cmConst::MAX_LEVEL_QTY];

    for(int i=0; i<t.cachedStartParents.size(); ++i) {
        const cmRecord::Evt& pEvt = t.cachedStartParents[i].evt;
        ImVec4 tmp = palette[pEvt.nameIdx%palette.size()];
        auto& li = levelElems[t.cachedStartParents.size()-1-i];
        li.color = ImColor(darkCoef*tmp.x, darkCoef*tmp.y, darkCoef*tmp.z, 1.f);
        if(pEvt.flags&PL_FLAG_SCOPE_MASK) {
            li.nameIdx     = pEvt.nameIdx;
            li.flags       = (pEvt.flags&PL_FLAG_TYPE_MASK) | PL_FLAG_SCOPE_BEGIN;
            li.lIdx        = t.cachedStartParents[i].lIdx;
            li.scopeStartTimeNs = pEvt.vS64;
        }
    }

    // Draw the text
    // =============
    int timeFormat = getConfig().getTimeFormat();
    float y = winY;
    float mouseTimeBestY      = -1.;
    float maxOffsetX          =  0.;
    int   nestingLevel        =  0;
    s64   mouseTimeBestTimeNs = -1;
    s64   newMouseTimeNs      = -1;

    for(const auto& tci : t.cachedItems) {
        // Get the cached item
        const cmRecord::Evt& evt = tci.evt;
        s64 scopeEndTimeNs = tci.scopeEndTimeNs;
        nestingLevel       = tci.nestingLevel;
        int flags          = evt.flags;
        int flagsType      = flags&PL_FLAG_TYPE_MASK;

        // Build the strings
        timeStr [0] = 0;
        nameStr [0] = 0;
        valueStr[0] = 0;
        const char* name = _record->getString(evt.nameIdx).value.toChar();
        bool      isHexa = _record->getString(evt.nameIdx).isHexa;
        int      lineQty = _record->getString(evt.nameIdx).lineQty; // Maybe overriden
        if(flags&PL_FLAG_SCOPE_BEGIN) {
            if(flagsType==PL_FLAG_TYPE_LOCK_WAIT) {
                snprintf(nameStr, maxMsgSize, "%s", name);
                snprintf(valueStr, maxMsgSize, "[WAIT FOR LOCK]"); flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
            }
            else snprintf(nameStr, maxMsgSize, "> %s", name);
        }
        else if(flags&PL_FLAG_SCOPE_END) {
            if(flagsType==PL_FLAG_TYPE_LOCK_WAIT) {
                snprintf(nameStr, maxMsgSize, "%s", name);
                snprintf(valueStr, maxMsgSize, "[LOCK AVAILABLE]"); flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
            }
            else snprintf(nameStr, maxMsgSize, "< %s", name);
        }
        else if(flagsType==PL_FLAG_TYPE_LOG) {  // Dead code
            // For logs, category is stored instead of evt.name and message instead of evt.filename
            snprintf(nameStr, maxMsgSize, "%s", _record->getString(evt.filenameIdx).value.toChar());
            snprintf(valueStr, maxMsgSize, "[LOG '%s']", name);
            flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
            lineQty = _record->getString(evt.filenameIdx).lineQty; // Update
        }
        else if(flagsType==PL_FLAG_TYPE_THREADNAME) {
            snprintf(nameStr, maxMsgSize, "%s", name);
            snprintf(valueStr, maxMsgSize, "[THREAD NAME]"); flagsType = PL_FLAG_TYPE_DATA_NONE;
        }
        else if(flagsType==PL_FLAG_TYPE_LOCK_ACQUIRED)  {
            snprintf(nameStr, maxMsgSize, "%s", name);
            snprintf(valueStr, maxMsgSize, "[LOCK ACQUIRED]"); flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
        }
        else if(flagsType==PL_FLAG_TYPE_LOCK_RELEASED)  {
            snprintf(nameStr, maxMsgSize, "%s", name);
            snprintf(valueStr, maxMsgSize, "[LOCK RELEASED]"); flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
        }
        else if(flagsType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
            snprintf(nameStr, maxMsgSize, "%s", name);
            snprintf(valueStr, maxMsgSize, "[LOCK NOTIFIED]"); flagsType = PL_FLAG_TYPE_DATA_TIMESTAMP;
        }
        else snprintf(nameStr, maxMsgSize, "%s", name);

        switch(flagsType) {
        case PL_FLAG_TYPE_DATA_NONE:      break;
        case PL_FLAG_TYPE_DATA_TIMESTAMP: snprintf(timeStr,  maxMsgSize, "%s", getFormattedTimeString(evt.vS64, timeFormat)); break;
        case PL_FLAG_TYPE_DATA_S32:       snprintf(valueStr, maxMsgSize, isHexa?"%X":"%d",   evt.vInt); break;
        case PL_FLAG_TYPE_DATA_U32:       snprintf(valueStr, maxMsgSize, isHexa?"%X":"%u",   evt.vU32); break;
        case PL_FLAG_TYPE_DATA_S64:       snprintf(valueStr, maxMsgSize, isHexa?"%lX":"%ld", evt.vS64); break;
        case PL_FLAG_TYPE_DATA_U64:       snprintf(valueStr, maxMsgSize, isHexa?"%lX":"%lu", evt.vU64); break;
        case PL_FLAG_TYPE_DATA_FLOAT:     snprintf(valueStr, maxMsgSize, "%f",  evt.vFloat); break;
        case PL_FLAG_TYPE_DATA_DOUBLE:    snprintf(valueStr, maxMsgSize, "%lf", evt.vDouble); break;
        case PL_FLAG_TYPE_DATA_STRING:
            snprintf(valueStr, maxMsgSize, "%s",  _record->getString(evt.vStringIdx).value.toChar());
            lineQty = bsMax(lineQty, _record->getString(evt.vStringIdx).lineQty);
            break;
        default:                          snprintf(valueStr, maxMsgSize, "<BAD TYPE %d>", flagsType);
        };
        float heightPix = fontHeight+fontHeightIntra*(lineQty-1);

        // Update the level info
        flagsType = (flags&PL_FLAG_TYPE_MASK); // Put back the original value, which may have been modified for display's needs
        if(flags&PL_FLAG_SCOPE_BEGIN) {
            auto& li = levelElems[nestingLevel];
            li.nameIdx        = evt.nameIdx;
            li.flags          = flagsType | PL_FLAG_SCOPE_BEGIN;
            li.lIdx           = tci.lIdx;
            li.scopeStartTimeNs = evt.vS64;
            li.scopeEndTimeNs   = scopeEndTimeNs;
        }
        if(flags&PL_FLAG_SCOPE_END) {
            levelElems[nestingLevel].scopeEndTimeNs = scopeEndTimeNs;
        }

        if((flags&PL_FLAG_SCOPE_MASK) ||
           (flagsType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && flagsType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST)) {
            auto& li = levelElems[nestingLevel];
            // Update the mouse time
            if(isWindowHovered && mouseY>y) {
                if     (flags&PL_FLAG_SCOPE_BEGIN) newMouseTimeNs = li.scopeStartTimeNs;
                else if(flags&PL_FLAG_SCOPE_END  ) newMouseTimeNs = li.scopeEndTimeNs;
                else newMouseTimeNs = evt.vS64;
            }

            // Update the best fit for the mouse time display (yellow horizontal line)
            if((flags&PL_FLAG_SCOPE_BEGIN) && _mouseTimeNs>=li.scopeStartTimeNs && li.scopeStartTimeNs>=mouseTimeBestTimeNs) {
                mouseTimeBestTimeNs = li.scopeStartTimeNs;
                mouseTimeBestY      = y+heightPix;
            }
            else if((flags&PL_FLAG_SCOPE_END) && _mouseTimeNs>=li.scopeEndTimeNs && li.scopeEndTimeNs>=mouseTimeBestTimeNs) {
                mouseTimeBestTimeNs = li.scopeEndTimeNs;
                mouseTimeBestY      = y+heightPix;
            }
            else if((flagsType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && flagsType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST) && _mouseTimeNs>=evt.vS64) {
                mouseTimeBestTimeNs = _mouseTimeNs;
                mouseTimeBestY      = y+heightPix;
            }
        }

        // Manage hovering: highlight and clicks
        int hlLevel = (flags&PL_FLAG_SCOPE_MASK)? nestingLevel : nestingLevel-1; // Non-scope have no date, so we take the time of the parent
        if(hlLevel>=0 && isWindowHovered && mouseY>=y && mouseY<y+heightPix) {
            auto& li = levelElems[hlLevel];

            // This section shall be highlighted
            int hlFlags = (flags&PL_FLAG_SCOPE_MASK)? flags : li.flags;
            if(hlFlags&PL_FLAG_SCOPE_MASK) hlFlags = (hlFlags&PL_FLAG_TYPE_MASK) | PL_FLAG_SCOPE_BEGIN;

            setScopeHighlight(t.threadId, li.scopeStartTimeNs, hlFlags, hlLevel, li.nameIdx);

            // Synchronized navigation
            if(t.syncMode>0) { // No synchronized navigation for isolated windows
                s64 syncStartTimeNs, syncTimeRangeNs;
                getSynchronizedRange(t.syncMode, syncStartTimeNs, syncTimeRangeNs);

                // Click: set timeline position at middle screen only if outside the center third of screen
                s64 targetTimeNs = li.scopeStartTimeNs;
                if((flags&PL_FLAG_SCOPE_END) && li.scopeEndTimeNs>=0.) targetTimeNs = li.scopeEndTimeNs;
                else if((flags&PL_FLAG_TYPE_MASK)>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && (flags&PL_FLAG_TYPE_MASK)<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST) {
                    targetTimeNs = evt.vS64;
                }
                if((ImGui::IsMouseReleased(0) && ImGui::GetMousePos().x<winX+winWidth) || tlWheelCounter) {
                    synchronizeNewRange(t.syncMode, bsMax(targetTimeNs-(s64)(0.5*syncTimeRangeNs), 0LL), syncTimeRangeNs);
                    ensureThreadVisibility(t.syncMode, t.threadId);
                    synchronizeText(t.syncMode, t.threadId, hlLevel, li.lIdx, li.scopeStartTimeNs, t.uniqueId);
                }
                // Double click: adapt also the scale to have the scope at 10% of the screen
                if(ImGui::IsMouseDoubleClicked(0) && li.scopeEndTimeNs>=0.) {
                    s64 newTimeRangeNs =  vwConst::DCLICK_RANGE_FACTOR*(li.scopeEndTimeNs-li.scopeStartTimeNs);
                    synchronizeNewRange(t.syncMode, syncStartTimeNs+(s64)((double)(targetTimeNs-syncStartTimeNs)/(double)syncTimeRangeNs*(double)(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(t.syncMode, t.threadId);
                }
                // Zoom the timeline
                if(tlWheelCounter!=0) {
                    s64 newTimeRangeNs = getUpdatedRange(tlWheelCounter, syncTimeRangeNs);
                    synchronizeNewRange(t.syncMode, syncStartTimeNs+(s64)((double)(targetTimeNs-syncStartTimeNs)/(double)syncTimeRangeNs*(double)(syncTimeRangeNs-newTimeRangeNs)),
                                        newTimeRangeNs);
                    ensureThreadVisibility(t.syncMode, t.threadId);
                }
            }
            // Right click: contextual menu, only on scope start
            if(!t.isDragging && ImGui::IsMouseReleased(2)) {
                t.ctxNestingLevel = nestingLevel;
                t.ctxScopeLIdx    = levelElems[hlLevel].lIdx;
                t.ctxNameIdx      = evt.nameIdx;
                t.ctxFlags        = flags;
                ImGui::OpenPopup("Text menu");
                _plotMenuItems.clear(); // Reset the popup menu state
                prepareGraphContextualMenu(t.threadId, t.ctxNestingLevel, (flags&PL_FLAG_SCOPE_END)? (levelElems[hlLevel].lIdx&(~1)) : tci.lIdx,
                                           0, _record->durationNs);
            }

            // Tooltip
            if(mouseX<winX+textPixMargin+charWidth*14) {
                ImGui::SetTooltip("%s", getNiceTime(_mouseTimeNs, 0));
            }
            else if((flags&PL_FLAG_SCOPE_BEGIN) && scopeEndTimeNs>=0) {
                ImGui::SetTooltip("Duration: %s", getNiceDuration(scopeEndTimeNs-evt.vS64));
            }
        }
        bool doHighlight = (hlLevel>=0 && isScopeHighlighted(t.threadId, levelElems[hlLevel].scopeStartTimeNs,
                                                             levelElems[hlLevel].scopeEndTimeNs, levelElems[hlLevel].flags,
                                                             hlLevel, levelElems[hlLevel].nameIdx));

        // Compute colors
        ImU32  color1;
        ImU32  color2;
        if(flags&PL_FLAG_SCOPE_MASK) {
            color1 = getConfig().getCurveColor(tci.elemIdx, true);
            color2 = levelElems[nestingLevel].color;
            t.lastTimeNs = evt.vS64;
        }
        else {
            color1 = (false && nestingLevel>0)? levelElems[nestingLevel-1].color : getConfig().getCurveColor(tci.elemIdx, true);
            color2 = (false && nestingLevel>0)? levelElems[nestingLevel-1].color : color1;
        }

        // Display the text background if highlighted
        if(doHighlight) {
            DRAWLIST->AddRectFilled(ImVec2(winX, y), ImVec2(winX+curScrollPosX+winWidth, y+heightPix), vwConst::uGrey48);
        }
        // Display the date if any
        float offsetX = winX-curScrollPosX+textPixMargin;
        if(timeStr[0]!=0) {
            DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uWhite, timeStr);
            int changedOffset = 0;
            while(timeStr[changedOffset] && timeStr[changedOffset]==t.lastDateStr[changedOffset]) ++changedOffset;
            DRAWLIST->AddText(ImVec2(offsetX, y), vwConst::uGrey128, timeStr, timeStr+changedOffset);
            snprintf(t.lastDateStr, sizeof(t.lastDateStr),"%s", timeStr);
        }
        offsetX += charWidth*(getFormattedTimeStringCharQty(timeFormat)+nestingLevel*2);

        // Display the name of the item
        DRAWLIST->AddText(ImVec2(offsetX, y), color1, nameStr);
        // Display the value
        float offsetX2 = bsMax(ImGui::CalcTextSize(nameStr).x, 20.f*charWidth)+2.f*charWidth;
        DRAWLIST->AddText(ImVec2(offsetX+offsetX2, y), color1, valueStr);

        // Display the vertical marker for the scope
        if(flags&PL_FLAG_SCOPE_BEGIN) {
            levelElems[nestingLevel].yScopeStart = (int)(y+heightPix); // Bottom of current text
            ImVec4 tmp = ImColor(color1).Value;
            levelElems[nestingLevel].color = ImColor(darkCoef*tmp.x, darkCoef*tmp.y, darkCoef*tmp.z, 1.f);
        }
        if((flags&PL_FLAG_SCOPE_END) && y-levelElems[nestingLevel].yScopeStart>0) {
            DRAWLIST->AddLine(ImVec2(offsetX, y), ImVec2(offsetX, (float)levelElems[nestingLevel].yScopeStart), color2);
            levelElems[nestingLevel].yScopeStart = -1;
        }
        offsetX += offsetX2 + ImGui::CalcTextSize(valueStr).x;

        // Next line
        if(offsetX>maxOffsetX) maxOffsetX = offsetX;
        if(y>winY+winHeight) break;
        y += heightPix;
    } // End of list on cached items

    // Finish the vertical marker for the scope, at the bottom
    for(int i=0; i<nestingLevel; ++i) {
        if(y-levelElems[i].yScopeStart>0) {
            float offsetX = winX-curScrollPosX+textPixMargin+charWidth*(getFormattedTimeStringCharQty(timeFormat)+i*2);
            DRAWLIST->AddLine(ImVec2(offsetX, y), ImVec2(offsetX, (float)levelElems[i].yScopeStart), levelElems[i].color);
        }
    }

    // Drag with middle button
    if(isWindowHovered && ImGui::IsMouseDragging(1)) {
        // Start a range selection
        if(t.rangeSelStartNs<0 && mouseTimeBestTimeNs>=0) {
            t.rangeSelStartNs = mouseTimeBestTimeNs;
            t.rangeSelStartY  = mouseTimeBestY;
        }

        // Drag on-going: display the selection box with transparency and range
        if(t.rangeSelStartNs>=0 && t.rangeSelStartNs<mouseTimeBestTimeNs) {
            char tmpStr[128];
            float y1 = t.rangeSelStartY-fontHeight;
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
            snprintf(tmpStr, sizeof(tmpStr), "{ %s }", getNiceDuration(mouseTimeBestTimeNs-t.rangeSelStartNs));
            ImVec2 tb = ImGui::CalcTextSize(tmpStr);
            float x3 = mouseX-0.5f*tb.x;
            DRAWLIST->AddRectFilled(ImVec2(x3-5.f, mouseY-tb.y-5.f), ImVec2(x3+tb.x+5.f, mouseY-5.f), IM_COL32(255,255,255,192));
            DRAWLIST->AddText(ImVec2(x3, mouseY-tb.y-5.f), vwConst::uBlack, tmpStr);
        }
    }
    // Drag ended: set the selected range view
    else if(isWindowHovered && t.rangeSelStartNs>=0) {
        if(t.rangeSelStartNs<mouseTimeBestTimeNs) {
            ensureThreadVisibility(t.syncMode, t.threadId);
            s64 newRangeNs = mouseTimeBestTimeNs-t.rangeSelStartNs;
            synchronizeNewRange(t.syncMode, t.rangeSelStartNs-(newRangeNs>>4), newRangeNs+(newRangeNs>>3)); // ~12% wider range
        }
        t.rangeSelStartNs = -1;
    }

    // Display and update the mouse time
    if(mouseTimeBestY>=0) {
        DRAWLIST->AddLine(ImVec2(winX, mouseTimeBestY), ImVec2(winX+winWidth, mouseTimeBestY), vwConst::uYellow);
    }
    if(newMouseTimeNs>=0.) _mouseTimeNs = newMouseTimeNs;
    if(!ImGui::IsMouseDragging(2)) t.isDragging = false;

    // Contextual menu
    if(ImGui::BeginPopup("Text menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
        ImGui::TextColored(vwConst::grey, "%s", _record->getString(t.ctxNameIdx).value.toChar());

        ImGui::Separator();
        ImGui::Separator();
        if(!(t.ctxFlags&PL_FLAG_SCOPE_BEGIN) && ImGui::MenuItem("Go to start of scope")) {
            t.setStartPosition((t.ctxFlags&PL_FLAG_SCOPE_MASK)? t.ctxNestingLevel:t.ctxNestingLevel-1, t.ctxScopeLIdx&(~1));
        }
        if(!(t.ctxFlags&PL_FLAG_SCOPE_END) && ImGui::MenuItem("Go to end of scope")) {
            t.setStartPosition((t.ctxFlags&PL_FLAG_SCOPE_MASK)? t.ctxNestingLevel:t.ctxNestingLevel-1, t.ctxScopeLIdx+1); // End of scope is always start+1
        }

        ImGui::Separator();
        // Plot & histogram menu
        if(!displayPlotContextualMenu(t.threadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
        ImGui::Separator();
        if(!displayHistoContextualMenu(headerWidth))                    ImGui::CloseCurrentPopup();

        // Color
        if(!_plotMenuItems.empty()) {
            ImGui::Separator();
            std::function<void(int)> curveSetColor = [this] (int colorIdx) { getConfig().setCurveColorIdx(_plotMenuItems[0].elemIdx, colorIdx); };
            displayColorSelectMenu("Color", getConfig().getCurveColorIdx(_plotMenuItems[0].elemIdx), curveSetColor);
        }

        // Export
        ImGui::Separator();
        if(ImGui::BeginMenu("Export in a text file...")) {
            if(ImGui::MenuItem("the content of this window")) {
                initiateExportText(t.threadId, -1, t.startNLevel, t.startLIdx, -1, bsMax(1, (int)(winHeight/fontHeight)));
            }
            if(t.syncMode!=0 && ImGui::MenuItem("the time range of the group")) {
                s64 startTimeNs, timeRangeNs;
                vwMain::getSynchronizedRange(t.syncMode, startTimeNs, timeRangeNs);
                initiateExportText(t.threadId, startTimeNs, -1, 0, startTimeNs+timeRangeNs, -1);
            }
            if(ImGui::MenuItem("the content of the full thread")) {
                initiateExportText(t.threadId, 0, -1, 0, -1, -1);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    // Help
    displayHelpTooltip(t.uniqueId, "Help Text",
                       "##Text view\n"
                       "===\n"
                       "Hierarchical data in a scrollable text form, for a given thread.\n"
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
                       "-#Double left mouse click#| Time and range synchronize views of the same group\n"
                       "-#Right mouse click#| Open menu for plot/histogram\n"
                       "\n"
                       );

    // Mark the virtual total size
    t.lastScrollPosY = ImGui::GetScrollY();
    ImGui::SetCursorPos(ImVec2(maxOffsetX+curScrollPosX-winX, normalizedScrollHeight));
    plgData(TEXT, "Current scroll pos", t.lastScrollPosY);

    ImGui::EndChild();
}
