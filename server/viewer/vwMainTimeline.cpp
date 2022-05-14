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

// This file implements the timeline view

// System
#include <algorithm>

// External
#include "imgui.h"

// Internal
#include "bsKeycode.h"
#include "cmPrintf.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


// Debug configuration
#ifndef PL_GROUP_TML
#define PL_GROUP_TML 1
#endif



bsString
vwMain::Timeline::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "timeline %d", syncMode);
    return tmpStr;
}


// Helpers
// =======

static constexpr float MIN_SCOPE_PIX = 3.f;

struct SmallItem {
    bool   isInit   = false;
    bool   hasEvt   = false;
    u32    scopeLIdx = PL_INVALID;
    float startPix = -1.f;
    float endPix   = -1.f;
    float endPixExact = -1.f; // endPix may be altered for visual reasons
    cmRecord::Evt evt;
    s64    evtDurationNs = 0;
};


struct TimelineDrawHelper {
    // Local state
    vwMain*   main;
    cmRecord* record;
    vwMain::Timeline* tl;
    ImFont*    font;
    float winX;
    float winY;
    float winWidth;
    float winHeight;
    float fontHeight;
    float fontSpacing;
    float textPixMargin;
    float threadTitleHeight;
    bool  isWindowHovered;
    s64   startTimeNs;
    s64   timeRangeNs;
    double nsToPix;
    float mouseX;
    float mouseY;

    ImU32 colorText;
    ImU32 colorTextH;
    ImU32 colorFillH;
    ImU32 colorFill1;
    ImU32 colorFill2;
    ImU32 colorFillS;
    ImU32 colorOutline;
    ImU32 colorGap;

    s64 forceRangeNs = 0;
    s64 forceStartNs = 0;

    // Functions
    void highlightGapIfHovered(s64 lastScopeEndTimeNs, float pixStartRect, float y);
    void displaySmallScope(const SmallItem& si, int level, int levelQty, float y, s64 lastScopeEndTimeNs);
    void displayScope(int threadId, int nestingLevel, u32 scopeLIdx, const cmRecord::Evt& evt,
                      float pixStartRect, float pixEndRect, float y, s64 durationNs, s64 lastScopeEndTimeNs, float yThread);
    void drawCoreTimeline(float& yThread);
    void drawLocks       (float& yThread);
    void drawScopes      (float& yThread, int threadId);
};


void
TimelineDrawHelper::highlightGapIfHovered(s64 lastScopeEndTimeNs, float pixStartRect, float y)
{
    float lastPixEndTime = (float)(nsToPix*(lastScopeEndTimeNs-startTimeNs));
    // Is previous gap hovered?
    if(isWindowHovered && lastScopeEndTimeNs!=0 && (mouseX-winX)>lastPixEndTime &&
       (mouseX-winX)<pixStartRect && mouseY>bsMax(y,winY+threadTitleHeight) && mouseY<bsMin(y+fontHeight,winY+winHeight)) {
        // Yes: Highlight the gap
        DRAWLIST->AddRectFilled(ImVec2(lastPixEndTime+winX, y), ImVec2(pixStartRect+winX, y+fontHeight), colorGap);
        DRAWLIST->AddRect      (ImVec2(lastPixEndTime+winX, y), ImVec2(pixStartRect+winX, y+fontHeight), colorOutline);

        // Add a tooltip
        s64 durationNs = (s64)((pixStartRect-lastPixEndTime)/nsToPix);
        ImGui::SetTooltip("Gap duration: %s", main->getNiceDuration(durationNs));

        // Double click adjusts the view to it
        if(ImGui::IsMouseDoubleClicked(0) && !tl->isAnimating()) {
            forceRangeNs = vwConst::DCLICK_RANGE_FACTOR*durationNs;
            forceStartNs = bsMax(startTimeNs+(s64)((double)(lastScopeEndTimeNs-startTimeNs)/(double)timeRangeNs*(double)(timeRangeNs-forceRangeNs)), 0LL);
        }
    }
};


void
TimelineDrawHelper::displaySmallScope(const SmallItem& si, int level, int levelQty, float y, s64 lastScopeEndTimeNs)
{
    highlightGapIfHovered(lastScopeEndTimeNs, si.startPix, y);
    DRAWLIST->AddRectFilled(ImVec2(winX+si.startPix, y), ImVec2(winX+si.endPix, y+fontHeight), colorFillS);
    if(level==0)          DRAWLIST->AddLine(ImVec2(winX+si.startPix, y           ), ImVec2(winX+si.endPix, y           ), colorOutline);
    if(level==levelQty-1) DRAWLIST->AddLine(ImVec2(winX+si.startPix, y+fontHeight), ImVec2(winX+si.endPix, y+fontHeight), colorOutline);
    DRAWLIST->AddLine(ImVec2(winX+si.startPix, y), ImVec2(winX+si.startPix, y+fontHeight), colorOutline);
    DRAWLIST->AddLine(ImVec2(winX+si.endPix  , y), ImVec2(winX+si.endPix  , y+fontHeight), colorOutline);
}


void
TimelineDrawHelper::displayScope(int threadId, int nestingLevel, u32 scopeLIdx,
                                const cmRecord::Evt& evt, float pixStartRect, float pixEndRect,
                                float y, s64 durationNs, s64 lastScopeEndTimeNs, float yThread)
{
    highlightGapIfHovered(lastScopeEndTimeNs, pixStartRect, y);
    ImGui::PushID( (void*)((u64)threadId | (((u64)nestingLevel)<<8) | (((u64)scopeLIdx)<<16)) );

    // Get information on this event
    bool  isHovered = (isWindowHovered && (mouseX-winX)>pixStartRect && (mouseX-winX)<pixEndRect &&
                       mouseY>bsMax(y,winY) && mouseY<bsMin(y+fontHeight,winY+winHeight));
    char  titleStr[256];
    int   evtType = evt.flags&PL_FLAG_TYPE_MASK;
    const char* qualifier = (evtType==PL_FLAG_TYPE_LOCK_WAIT)? "<lock wait> " : "";
    snprintf(titleStr, sizeof(titleStr), "%s%s { %s }", qualifier, record->getString(evt.nameIdx).value.toChar(), main->getNiceDuration(durationNs));

    if(isHovered) {
        // Highlight
        main->setScopeHighlight(threadId, evt.vS64, evt.vS64+durationNs, evt.flags, nestingLevel, evt.nameIdx);

        // Query the children
        cmRecordIteratorScope it(record, threadId, nestingLevel, scopeLIdx);
        it.getChildren(evt.linkLIdx, scopeLIdx, false, false, true, main->_workDataChildren, main->_workLIdxChildren);

        // Display the tooltip
        main->displayScopeTooltip(titleStr, main->_workDataChildren, evt, durationNs);

        // Single right click: open a contextual menu
        if(tl->dragMode==vwMain::NONE && ImGui::IsMouseReleased(2)) {
            tl->ctxNestingLevel = nestingLevel;
            tl->ctxScopeLIdx    = scopeLIdx;
            tl->ctxScopeNameIdx = evt.nameIdx;
            tl->ctxDoOpenContextMenu = true;
        }

        // Simple click sets the text scope start date
        if(tl->dragMode==vwMain::NONE && ImGui::IsMouseReleased(0)) {
            main->synchronizeText(tl->syncMode, threadId, nestingLevel, scopeLIdx, evt.vS64, tl->uniqueId);
        }

        // Double click on a scope adjusts the time range to it, and triggers the memory detailed display
        if(ImGui::IsMouseDoubleClicked(0) && !tl->isAnimating()) {
            // Force the time range (will be processed later, as we are in the middle of current display)
            forceRangeNs = vwConst::DCLICK_RANGE_FACTOR*durationNs;
            forceStartNs = bsMax(startTimeNs+(s64)((double)(evt.vS64-startTimeNs)/(double)timeRangeNs*(double)(timeRangeNs-forceRangeNs)), 0LL);
            // Synchronize thread visibility
            main->ensureThreadVisibility(tl->syncMode, threadId);
            // Show memory details
            for(auto& mtl: main->_memTimelines) {
                if(tl->syncMode!=0 && mtl.syncMode==tl->syncMode) {;
                    main->collectMemoryBlocks(mtl, threadId, evt.vS64, evt.vS64+durationNs, record->getString(evt.nameIdx).value, true, true);
                }
            }
        } // End of double click
    }

    // Draw the filled rectangle
    ImU32 scopeColor     = (evt.level&1)? colorFill2:colorFill1;
    bool  isHighlighted = main->isScopeHighlighted(threadId, evt.vS64, evt.vS64+durationNs, evt.flags, nestingLevel, evt.nameIdx);
    if(evtType==PL_FLAG_TYPE_LOCK_WAIT) scopeColor = vwConst::uBrightRed;
    if(isHighlighted) scopeColor = (evtType==PL_FLAG_TYPE_LOCK_WAIT)? vwConst::uYellow : colorFillH;
    DRAWLIST->AddRectFilled(ImVec2(pixStartRect+winX, y), ImVec2(pixEndRect+winX, y+fontHeight), scopeColor);
    DRAWLIST->AddRect(ImVec2(pixStartRect+winX, y), ImVec2(pixEndRect+winX, y+fontHeight), colorOutline);

    // Transparent yellow highlight on the full thread height in case of wait lock
    if(isHighlighted && evtType==PL_FLAG_TYPE_LOCK_WAIT) {
        DRAWLIST->AddRectFilled(ImVec2(pixStartRect+winX, y), ImVec2(pixEndRect+winX, yThread), IM_COL32(255, 192, 64, 96));
    }

    // Draw the text which fits in the space
    constexpr float minCharWidth = 8.;
    float pixTextStart = bsMax(0.f, pixStartRect);
    if(pixEndRect-pixTextStart-textPixMargin*2.f>=minCharWidth) { // Else no need to work...
        const char* remaining = 0;
        font->CalcTextSizeA(ImGui::GetFontSize(), pixEndRect-pixTextStart-textPixMargin*2.f, 0.0f, titleStr, NULL, &remaining);
        if(titleStr!=remaining) {
            DRAWLIST->AddText(ImVec2(winX+pixTextStart+textPixMargin, y+fontSpacing), isHighlighted? colorTextH : colorText, titleStr, remaining);
        }
    }
    ImGui::PopID();
}




void
TimelineDrawHelper::drawCoreTimeline(float& yThread)
{
    constexpr float coreNamePosX = 50.f;
    constexpr float heightMargin = 2.f;
    constexpr float minCharWidth = 8.f;
    constexpr float coarseFactor = 0.08f;
    char   tmpStr[128];
    float widthCoreXX = ImGui::CalcTextSize("CoreXX").x;

    // Skip the drawing if not visible
    plgScope (TML, "Display cores timeline");
    if(yThread>winY+ImGui::GetWindowHeight() || yThread+record->coreQty*fontHeight<=winY) {
        plgText(TML, "State", "Skipped because hidden");
        yThread += record->coreQty*fontHeight;
        return;
    }

    // Draw the filled CPU curve (step)
    vwMain::TlCachedCpuPoint prevPt = { -1., 0. };
    constexpr float thres0 = 0.2f, thres1=0.33f, thres2=0.66f, thres3=0.8f, alphaCpu=0.6f;
    for(const vwMain::TlCachedCpuPoint& cl : tl->cachedCpuCurve) {
        float x1    = winX+prevPt.timePix, x2 = winX+bsMax(prevPt.timePix+1.f, cl.timePix);
        float prevY = yThread;
        float value = (float)prevPt.cpuUsageRatio;
        ImU32 prevColor = ImColor(thres0, 0.f, 0.f, alphaCpu);
        ImU32 colorUp;

        // Draw the gradient curve
#define DRAW_LAYERED_CURVE(thresMin, thresMax, colorCode)               \
        if(value>(thresMin)) {                                          \
            float tValue = bsMin(value, thresMax), tY = yThread-tValue*fontHeight; \
            colorUp = colorCode;                                        \
            DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, prevY), ImVec2(x2, tY), prevColor, prevColor, colorUp, colorUp); \
            prevColor = colorUp;                                        \
            prevY     = tY;                                             \
        }
        DRAW_LAYERED_CURVE(0.f,     thres1, ImColor(bsMax(tValue, thres0)/thres1, 0.f, 0.f, alphaCpu));             // black (0,0,0) -> red   (1,0,0)
        DRAW_LAYERED_CURVE(thres1, thres2, ImColor(1.0f, (tValue-thres1)/(thres2-thres1), 0.f,  alphaCpu));        // red   (1,0,0) -> yellow(1,1,0)
        DRAW_LAYERED_CURVE(thres2,    1.0f, ImColor(1.0f, 1.0f, bsMin(1.f, (tValue-thres2)/(thres3-thres2)), 1.f)); // yellow(1,1,0) -> white (1,1,1)
        prevPt = cl;

        // Tooltip
        if(isWindowHovered && value>0. && mouseX>x1 && mouseX<x2 && mouseY>yThread-fontHeight && mouseY<yThread) {
            ImGui::SetTooltip("CPU at %d %%", (int)(100.*value+0.5));
        }
    }

    bool areCoreNamesHovered = (isWindowHovered && mouseY>yThread && mouseY<yThread+record->coreQty*fontHeight &&
                                mouseX>winX+coreNamePosX && mouseX<winX+coreNamePosX+widthCoreXX+2*textPixMargin);

    for(int coreId=0; coreId<record->coreQty; ++coreId) {
        // Darker background
        DRAWLIST->AddRectFilled(ImVec2(winX, yThread+heightMargin), ImVec2(winX+winWidth, yThread+fontHeight-heightMargin), vwConst::uGreyDark);

        // Timeline
        for(const vwMain::TlCachedCore& cl : tl->cachedUsagePerCore[coreId]) {
            if(cl.startTimePix>winWidth) continue;
            float x2 = winX+bsMax(cl.startTimePix+3.f, cl.endTimePix);
            bool  isHovered = (!cl.isCoarse && isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 &&
                               mouseY>yThread && mouseY<yThread+fontHeight);
            float cHeightMargin = heightMargin + (cl.isCoarse? coarseFactor*fontHeight : 0.f);

            // Draw the box
            ImU32 color        = cl.isCoarse? vwConst::uGrey64 : vwConst::uGrey96;
            ImU32 colorBoxOutline = cl.isCoarse? vwConst::uGrey48 : vwConst::uGrey64;
            if(cl.threadId!=cmConst::MAX_THREAD_QTY && !cl.isCoarse) {
                const float  dimO      = 0.5f;
                const ImVec4 colorBase = main->getConfig().getThreadColor(cl.threadId);
                color        = ImColor(colorBase);
                colorBoxOutline = ImColor(dimO*colorBase.x, dimO*colorBase.y, dimO*colorBase.z);
            }
            DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread+cHeightMargin), ImVec2(x2, yThread+fontHeight-cHeightMargin), color);
            DRAWLIST->AddRect      (ImVec2(winX+cl.startTimePix, yThread+cHeightMargin), ImVec2(x2, yThread+fontHeight-cHeightMargin), colorBoxOutline);

            // Add the text
            float clWidth = cl.endTimePix-cl.startTimePix;
            if(!cl.isCoarse && clWidth>=minCharWidth) {
                plAssert(cl.threadId!=cmConst::MAX_THREAD_QTY || cl.nameIdx!=PL_INVALID, cl.threadId, cl.nameIdx);
                if(cl.threadId<cmConst::MAX_THREAD_QTY)
                    snprintf(tmpStr, sizeof(tmpStr), "[%s]", main->getFullThreadName(cl.threadId));
                else snprintf(tmpStr, sizeof(tmpStr), "%s",   record->getString(cl.nameIdx).value.toChar());
                const char* remaining = 0;
                font->CalcTextSizeA(ImGui::GetFontSize(), clWidth-textPixMargin*2.f, 0.0f, &tmpStr[0], NULL, &remaining);
                if(&tmpStr[0]!=remaining) {
                    DRAWLIST->AddText(ImVec2(winX+cl.startTimePix+textPixMargin, yThread+fontSpacing),
                                      vwConst::uWhite, &tmpStr[0], remaining);
                }
            }

            // Tooltip
            if(isHovered && !cl.isCoarse) {
                if(cl.threadId<cmConst::MAX_THREAD_QTY)
                    snprintf(tmpStr, sizeof(tmpStr), "Thread [%s]", main->getFullThreadName(cl.threadId));
                else snprintf(tmpStr, sizeof(tmpStr), "External process '%s'", record->getString(cl.nameIdx).value.toChar());
                ImGui::SetTooltip("%s { %s }", &tmpStr[0], main->getNiceDuration(cl.durationNs));
            }
        }

        // Thread name overlay
        snprintf(tmpStr, sizeof(tmpStr), "Core %d", coreId);
        DRAWLIST->AddRectFilled(ImVec2(winX+coreNamePosX, yThread+heightMargin),
                                ImVec2(winX+coreNamePosX+widthCoreXX+2*textPixMargin, yThread+fontHeight-heightMargin),
                                areCoreNamesHovered? IM_COL32(0, 0, 0, 32) : vwConst::uBlack);
        DRAWLIST->AddText(ImVec2(winX+50+textPixMargin, yThread+fontSpacing), areCoreNamesHovered? IM_COL32(255, 255, 255, 128) : vwConst::uWhite, tmpStr);

        yThread += fontHeight;
    } // End of loop on thread locks

}


void
TimelineDrawHelper::drawLocks(float& yThread)
{
    constexpr float threadNamePosX = 50.f;
    constexpr float minCharWidth   = 8.f;
    constexpr float  dim2 = 0.8f;
    plAssert(tl->cachedLockUse.size()<=record->locks.size());

    // Skip the drawing if not visible
    plgScope (TML, "Display locks timeline");
    float yThreadEnd = yThread; // Compute the end of the lock section (depends on content)
    for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
        bsVec<int>& waitingThreadIds = record->locks[lockIdx].waitingThreadIds;
        float threadBarHeight = bsMinMax(fontHeight/bsMax(1,waitingThreadIds.size()), 3.f, 0.5f*fontHeight);
        yThreadEnd += waitingThreadIds.size()*threadBarHeight + 1.5f*fontHeight;
    }
    if(yThread>winY+ImGui::GetWindowHeight() || yThreadEnd<=winY) {
        plgText(TML, "State", "Skipped because hidden");
        yThread = yThreadEnd;
        return;
    }

    int timeFormat = main->getConfig().getTimeFormat();
    float maxLockNameWidth = 1.;
    for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
        maxLockNameWidth = bsMax(maxLockNameWidth, ImGui::CalcTextSize(record->getString(record->locks[lockIdx].nameIdx).value.toChar()).x);
    }

    // Loop on locks
    for(int lockIdx : tl->cachedLockOrderedIdx) {
        const vwMain::TlCachedLockUse&  clu  = tl->cachedLockUse[lockIdx];
        bsVec<int>& waitingThreadIds = record->locks[lockIdx].waitingThreadIds;
        float      threadBarHeight  = bsMinMax(fontHeight/bsMax(1,waitingThreadIds.size()), 3.f, 0.5f*fontHeight);
        const float yUsed = yThread+waitingThreadIds.size()*threadBarHeight;

        // Darker background
        DRAWLIST->AddRectFilled(ImVec2(winX, yThread), ImVec2(winX+winWidth, yUsed+fontHeight), vwConst::uGreyDark);

        // Draw the waiting thread scopes
        for(int wti=0; wti<waitingThreadIds.size(); ++wti) {
            float  yBar        = yThread + wti*threadBarHeight;
            ImVec4 colorBase   = main->getConfig().getThreadColor(waitingThreadIds[wti]);
            ImU32  colorThread = ImColor(dim2*colorBase.x, dim2*colorBase.y, dim2*colorBase.z);

            // Loop on the waiting scopes
            for(const vwMain::TlCachedLockScope& cl : clu.waitingThreadScopes[wti]) {
                ImGui::PushID(&cl);
                // Draw the horizontal bar for the lock wait duration
                float thickness = bsMax(bsMin(threadBarHeight, cl.endTimePix-cl.startTimePix), 2.f);
                float x2 = winX+bsMax(cl.startTimePix+2.f, cl.endTimePix-thickness);
                bool isHighlighted = main->isScopeHighlighted(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_WAIT|PL_FLAG_SCOPE_BEGIN, -1, cl.e.nameIdx);
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yBar), ImVec2(x2, yBar+threadBarHeight), isHighlighted? vwConst::uYellow : colorThread);
                // Draw the vertical-slightly-diagonal line toward the lock use scope
                DRAWLIST->AddQuadFilled(ImVec2(x2, yBar), ImVec2(x2, yBar+threadBarHeight-0.5f), ImVec2(x2+thickness, yUsed), ImVec2(x2+0.5f*thickness, yBar),
                                        isHighlighted? vwConst::uYellow : colorThread);

                // Hovered
                if(isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2+thickness && mouseY>=yBar && mouseY<=yBar+threadBarHeight) {
                    // Highlight the corresponding wait scope
                    main->setScopeHighlight(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_WAIT|PL_FLAG_SCOPE_BEGIN, -1, cl.e.nameIdx);
                    // Clicked?
                    if(ImGui::IsMouseReleased(0)) main->ensureThreadVisibility(tl->syncMode, cl.e.threadId);
                    if(ImGui::IsMouseReleased(2)) {
                        // Find the matching elem
                        for(int elemIdx=0; elemIdx<record->elems.size(); ++elemIdx) {
                            const cmRecord::Elem& elem = record->elems[elemIdx];
                            if(elem.isPartOfHStruct && elem.threadId==cl.e.threadId && elem.nameIdx==cl.e.nameIdx && elem.flags==cl.e.flags) {
                                main->_plotMenuItems.clear(); // Reset the popup menu state
                                main->prepareGraphContextualMenu(elemIdx, tl->getStartTimeNs(), tl->getTimeRangeNs(), true, false);
                                ImGui::OpenPopup("lock wait menu");
                                break;
                            }
                        }
                    }

                    // Tooltip
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ImColor(main->getConfig().getThreadColor(cl.e.threadId, true)), "[%s]", main->getFullThreadName(cl.e.threadId)); ImGui::SameLine();
                    if(cl.overlappedThreadIds[0]!=0xFF) {
                        ImGui::TextColored(vwConst::red, "blocked by"); ImGui::SameLine();
                        for(int i=0; i<vwConst::MAX_OVERLAPPED_THREAD && cl.overlappedThreadIds[i]!=0xFF; ++i) {
                            ImGui::TextColored(ImColor(main->getConfig().getThreadColor(cl.overlappedThreadIds[i], true)), "[%s]",
                                               main->getFullThreadName(cl.overlappedThreadIds[i])); ImGui::SameLine();
                        }
                        ImGui::TextColored(vwConst::red, "{ %s }", main->getNiceDuration(cl.durationNs));
                        ImGui::TextColored(vwConst::gold, "Competing for lock '%s'", record->getString(record->locks[lockIdx].nameIdx).value.toChar());
                    } else { // Unusual case, but could happen
                        ImGui::TextColored(vwConst::gold, "waiting for lock '%s' { %s }", record->getString(record->locks[lockIdx].nameIdx).value.toChar(),
                                           main->getNiceDuration(cl.durationNs));
                    }
                    if(cl.e.lineNbr>0) {
                        ImGui::Text("At line"); ImGui::SameLine();
                        ImGui::TextColored(vwConst::grey, "%d", cl.e.lineNbr); ImGui::SameLine();
                        ImGui::Text("in file"); ImGui::SameLine();
                    } else {
                        ImGui::Text("In"); ImGui::SameLine();
                    }
                    ImGui::TextColored(vwConst::grey, "%s", record->getString(cl.e.filenameIdx).value.toChar());
                    ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0, 0, timeFormat));
                    ImGui::EndTooltip();
                }

                // Popup
                if(ImGui::BeginPopup("lock wait menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                    float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                    ImGui::TextColored(vwConst::grey, "<lock wait> [%s]", record->getString(cl.e.nameIdx).value.toChar());
                    // Plot & histogram
                    if(!main->_plotMenuItems.empty()) {
                        ImGui::Separator();
                        ImGui::Separator();
                        if(!main->displayPlotContextualMenu(cl.e.threadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                        ImGui::Separator();
                        if(!main->displayHistoContextualMenu(headerWidth)) ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        } // End of loop on waiting thread scopes

        // Loop on "used" scopes
        yThread = yUsed;
        for(const vwMain::TlCachedLockScope& cl : clu.scopes) {
            if(cl.e.nameIdx==PL_INVALID && !cl.isCoarse) continue;
            if(cl.startTimePix>winWidth) continue;
            ImGui::PushID(&cl);
            float x2 = winX+bsMax(cl.startTimePix+2.f, cl.endTimePix);
            bool  isHovered = (!cl.isCoarse && isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 &&
                               mouseY>yThread && mouseY<yThread+fontHeight);
            bool isHighlighted = !cl.isCoarse &&  main->isScopeHighlighted(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_ACQUIRED, -1, cl.e.nameIdx);

            // Draw the box
            ImU32 color        = cl.isCoarse? vwConst::uGrey64 : vwConst::uGrey96;
            ImU32 colorBoxOutline = cl.isCoarse? vwConst::uGrey48 : vwConst::uGrey64;
            if(cl.e.threadId!=cmConst::MAX_THREAD_QTY && !cl.isCoarse) {
                constexpr float  dimO  = 0.5f;
                const ImVec4 colorBase = main->getConfig().getThreadColor(cl.e.threadId);
                color        = ImColor(colorBase);
                colorBoxOutline = ImColor(dimO*colorBase.x, dimO*colorBase.y, dimO*colorBase.z);
            }
            if(isHighlighted) color = vwConst::uWhite;
            DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread), ImVec2(x2, yThread+fontHeight), color);
            DRAWLIST->AddRect      (ImVec2(winX+cl.startTimePix, yThread), ImVec2(x2, yThread+fontHeight), colorBoxOutline);

            // Draw the wait lock line if required (red line at the bottom)
            if(!isHighlighted && !cl.isCoarse && cl.overlappedThreadIds[0]!=0xFF) {
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread+fontHeight-2), ImVec2(x2, yThread+fontHeight), vwConst::uRed);
            }

            // Add the text
            float clWidth = cl.endTimePix-cl.startTimePix;
            if(!cl.isCoarse && clWidth>=minCharWidth) {
                const char* s = main->getFullThreadName(cl.e.threadId);
                const char* remaining = 0;
                font->CalcTextSizeA(ImGui::GetFontSize(), clWidth-textPixMargin*2.f, 0.0f, s, NULL, &remaining);
                if(s!=remaining) {
                    DRAWLIST->AddText(ImVec2(winX+cl.startTimePix+textPixMargin, yThread+fontSpacing),
                                      isHighlighted? vwConst::uBlack : vwConst::uWhite, s, remaining);
                }
            }

            if(isHovered) {
                // Highlight the corresponding wait scope
                main->setScopeHighlight(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_ACQUIRED, -1, cl.e.nameIdx);
                // Clicked?
                if(ImGui::IsMouseReleased(0)) main->ensureThreadVisibility(tl->syncMode, cl.e.threadId);
                if(ImGui::IsMouseReleased(2)) {
                    // Find the matching elem
                    u64 itemHashPath = bsHashStepChain(record->threads[cl.e.threadId].threadHash, record->getString(cl.e.nameIdx).hash, cmConst::LOCK_USE_NAMEIDX); // Element lock notified for this thread and with this name
                    for(int elemIdx=0; elemIdx<record->elems.size(); ++elemIdx) {
                        if(record->elems[elemIdx].hashPath!=itemHashPath) continue;
                        main->_plotMenuItems.clear(); // Reset the popup menu state
                        main->prepareGraphContextualMenu(elemIdx, tl->getStartTimeNs(), tl->getTimeRangeNs(), false, false);
                        ImGui::OpenPopup("lock use menu");
                        break;
                    }
                }

                // Tooltip
                ImGui::BeginTooltip();
                ImGui::TextColored(ImColor(main->getConfig().getThreadColor(cl.e.threadId, true)), "[%s]", main->getFullThreadName(cl.e.threadId)); ImGui::SameLine();
                ImGui::TextColored(vwConst::white, "using '%s' { %s }", record->getString(record->locks[lockIdx].nameIdx).value.toChar(), main->getNiceDuration(cl.durationNs));
                if(cl.overlappedThreadIds[0]!=0xFF) {
                    for(int i=0; i<vwConst::MAX_OVERLAPPED_THREAD && cl.overlappedThreadIds[i]!=0xFF; ++i) {
                        if((i&3)==0) ImGui::TextColored(vwConst::red, "Blocking"); // 4 names per line
                        ImGui::SameLine();
                        ImGui::TextColored(ImColor(main->getConfig().getThreadColor(cl.overlappedThreadIds[i], true)), "[%s]", main->getFullThreadName(cl.overlappedThreadIds[i]));
                    }
                }
                if(cl.e.lineNbr>0) {
                    ImGui::Text("At line"); ImGui::SameLine();
                    ImGui::TextColored(vwConst::grey, "%d", cl.e.lineNbr); ImGui::SameLine();
                    ImGui::Text("in file"); ImGui::SameLine();
                } else {
                    ImGui::Text("In"); ImGui::SameLine();
                }
                ImGui::TextColored(vwConst::grey, "%s", record->getString(cl.e.filenameIdx).value.toChar());
                ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0, 0, timeFormat));
                ImGui::EndTooltip();
            }

            // Popup
            if(ImGui::BeginPopup("lock use menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                ImGui::TextColored(vwConst::grey, "<lock use> [%s]", record->getString(cl.e.nameIdx).value.toChar());
                // Plot & histogram
                if(!main->_plotMenuItems.empty()) {
                    ImGui::Separator();
                    ImGui::Separator();
                    if(!main->displayPlotContextualMenu(cl.e.threadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                    ImGui::Separator();
                    if(!main->displayHistoContextualMenu(headerWidth)) ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        // Loop on lock notifications
        const float notifHalfWidthPix = 3.f;
        const float notifHeightPix = 0.6f*fontHeight;
        const float yNtf = yThread+fontHeight;
        for(const vwMain::TlCachedLockNtf& ntf : tl->cachedLockNtf[lockIdx]) {
            if(ntf.timePix>winWidth) continue;
            ImGui::PushID(&ntf);
            bool isHovered = (!ntf.isCoarse && isWindowHovered && mouseX>=winX+ntf.timePix-notifHalfWidthPix &&
                              mouseX<=winX+ntf.timePix+notifHalfWidthPix && mouseY>=yNtf-notifHeightPix && mouseY<=yNtf);
            bool isHighlighted = !ntf.isCoarse &&  main->isScopeHighlighted(ntf.e.threadId, ntf.e.vS64, PL_FLAG_TYPE_LOCK_NOTIFIED, -1, ntf.e.nameIdx);

            int ntfTId = ntf.e.threadId;
            ImU32 color = isHovered? vwConst::uWhite : vwConst::uGrey64;
            if(!ntf.isCoarse && !isHovered) {
                const ImVec4 colorBase = main->getConfig().getThreadColor(ntfTId);
                color = ImColor(colorBase.x, colorBase.y, colorBase.z, 0.7f);
            }
            if(isHighlighted) color = vwConst::uWhite;
            DRAWLIST->AddTriangleFilled(ImVec2(winX+ntf.timePix-notifHalfWidthPix, yNtf), ImVec2(winX+ntf.timePix+notifHalfWidthPix, yNtf),
                                        ImVec2(winX+ntf.timePix, yNtf-notifHeightPix), ntf.isCoarse? vwConst::uGrey : color);
            DRAWLIST->AddTriangle      (ImVec2(winX+ntf.timePix-notifHalfWidthPix, yNtf), ImVec2(winX+ntf.timePix+notifHalfWidthPix, yNtf),
                                        ImVec2(winX+ntf.timePix, yNtf-notifHeightPix), vwConst::uGrey64);

            // Hovered?
            if(isHovered) {
                // Highlight
                main->setScopeHighlight(ntf.e.threadId, ntf.e.vS64, PL_FLAG_TYPE_LOCK_NOTIFIED, -1, ntf.e.nameIdx);
                // Clicked?
                if(tl->dragMode==vwMain::NONE && ImGui::IsMouseReleased(0)) {
                    // Synchronize the text (after getting the nesting level and lIdx for this date on this thread)
                    int nestingLevel;
                    u32 lIdx;
                    cmGetRecordPosition(record, ntfTId, ntf.e.vS64, nestingLevel, lIdx);
                    main->synchronizeText(tl->syncMode, ntfTId, nestingLevel, lIdx, ntf.e.vS64, tl->uniqueId);
                }
                if(ImGui::IsMouseDoubleClicked(0)) {
                    // Make the thread visible
                    main->ensureThreadVisibility(tl->syncMode, ntfTId);
                }
                if(ImGui::IsMouseReleased(2)) {
                    // Find the matching elem
                    u64 itemHashPath = bsHashStepChain(record->getString(ntf.e.nameIdx).hash, cmConst::LOCK_NTF_NAMEIDX); // Element lock notified for this thread and with this name
                    for(int elemIdx=0; elemIdx<record->elems.size(); ++elemIdx) {
                        if(record->elems[elemIdx].hashPath!=itemHashPath) continue;
                        main->_plotMenuItems.clear(); // Reset the popup menu state
                        main->prepareGraphContextualMenu(elemIdx, tl->getStartTimeNs(), tl->getTimeRangeNs(), false, false);
                        ImGui::OpenPopup("lock ntf menu");
                        break;
                    }
                }

                // Tooltip
                ImGui::BeginTooltip();
                ImGui::TextColored(ImColor(main->getConfig().getThreadColor(ntfTId, true)), "[%s]", main->getFullThreadName(ntfTId)); ImGui::SameLine();
                ImGui::TextColored(vwConst::gold, " notified '%s'", record->getString(ntf.e.nameIdx).value.toChar());
                if(ntf.e.lineNbr>0) {
                    ImGui::Text("At line"); ImGui::SameLine();
                    ImGui::TextColored(vwConst::grey, "%d", ntf.e.lineNbr); ImGui::SameLine();
                    ImGui::Text("in file"); ImGui::SameLine();
                } else {
                    ImGui::Text("In"); ImGui::SameLine();
                }
                ImGui::TextColored(vwConst::grey, "%s", record->getString(ntf.e.filenameIdx).value.toChar());
                ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(ntf.e.vS64, 0, 0, timeFormat));
                ImGui::EndTooltip();
            }

            // Popup
            if(ImGui::BeginPopup("lock ntf menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                ImGui::TextColored(vwConst::grey, "<lock notified> [%s]", record->getString(ntf.e.nameIdx).value.toChar());
                // Plot & histogram
                if(!main->_plotMenuItems.empty()) {
                    ImGui::Separator();
                    ImGui::Separator();
                    if(!main->displayPlotContextualMenu(ntf.e.threadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                    ImGui::Separator();
                    if(!main->displayHistoContextualMenu(headerWidth)) ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        } // End of loop on lock notifications

        // Draw the lock name overlay
        bool isLockNameHovered = (isWindowHovered && mouseY>yThread && mouseY<yThread+fontHeight && mouseX>winX+threadNamePosX && mouseX<winX+threadNamePosX+maxLockNameWidth+2*textPixMargin);
        DRAWLIST->AddRectFilled(ImVec2(winX+threadNamePosX, yThread+2.f), ImVec2(winX+threadNamePosX+maxLockNameWidth+2*textPixMargin, yThread+fontHeight-2.f),
                                isLockNameHovered? IM_COL32(0, 0, 0, 32) : vwConst::uBlack);
        DRAWLIST->AddText(ImVec2(winX+50+textPixMargin, yThread+fontSpacing), isLockNameHovered? IM_COL32(255, 255, 255, 64) : vwConst::uWhite,
                          record->getString(record->locks[lockIdx].nameIdx).value.toChar());

        // Menu on lock name
        ImGui::PushID(lockIdx);
        if(isLockNameHovered && ImGui::IsMouseReleased(2)) {
            // Find the matching elem = lock used for this name (all threads)
            u64 itemHashPath = bsHashStepChain(record->getString(record->locks[lockIdx].nameIdx).hash, cmConst::LOCK_USE_NAMEIDX);
            for(int elemIdx=0; elemIdx<record->elems.size(); ++elemIdx) {
                if(record->elems[elemIdx].hashPath!=itemHashPath) continue;
                main->_plotMenuItems.clear(); // Reset the popup menu state
                main->prepareGraphContextualMenu(elemIdx, tl->getStartTimeNs(), tl->getTimeRangeNs(), false, false);
                ImGui::OpenPopup("lock all thread use menu");
                break;
            }
        }
        // Popup
        if(ImGui::BeginPopup("lock all thread use menu", ImGuiWindowFlags_AlwaysAutoResize)) {
            float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
            ImGui::TextColored(vwConst::grey, "<lock use> %s (all threads)", record->getString(record->locks[lockIdx].nameIdx).value.toChar());
            // Plot & histogram
            if(!main->_plotMenuItems.empty()) {
                ImGui::Separator();
                ImGui::Separator();
                if(!main->displayPlotContextualMenu(-1, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                ImGui::Separator();
                if(!main->displayHistoContextualMenu(headerWidth)) ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        // Next lock
        yThread += 1.5f*fontHeight;
    } // End of loop on used locks
}


void
TimelineDrawHelper::drawScopes(float& yThread, int tId)
{
    char tmpStr[128];
    constexpr float coreFontRatio = 0.8f;
    float widthCoreXX = font->CalcTextSizeA(coreFontRatio*ImGui::GetFontSize(), 1000.f, 0.f, "CoreX").x; // Display "Core%d" if enough space
    float widthCoreX  = font->CalcTextSizeA(coreFontRatio*ImGui::GetFontSize(), 1000.f, 0.f, "X").x;     // Second choice is displaying "%d", else nothing
    int nestingLevelQty = tl->cachedScopesPerThreadPerNLevel[tId].size();
    int timeFormat = main->getConfig().getTimeFormat();

    plgScope (TML, "Display Thread");
    plgVar(TML, tId, nestingLevelQty);

    // Skip the thread drawing if not visible
    if(yThread-fontHeight>winY+ImGui::GetWindowHeight() || yThread+nestingLevelQty*fontHeight<=winY) {
        plgText(TML, "State", "Skipped because hidden");
        yThread += nestingLevelQty*fontHeight;
        return;
    }

    // Darker background
    DRAWLIST->AddRectFilled(ImVec2(winX, yThread), ImVec2(winX+winWidth, yThread+nestingLevelQty*fontHeight), vwConst::uGreyDark);

    // Draw the text background for this thread
    for(auto& tw: main->_texts) {
        if(tw.threadId!=tId) continue;
        s64 firstTimeNs = bsMinMax(tw.firstTimeNs, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs);
        s64 lastTimeNs  = bsMinMax(tw.lastTimeNs,  tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs);
        if(firstTimeNs==lastTimeNs && (firstTimeNs==tl->startTimeNs || firstTimeNs==tl->startTimeNs+tl->timeRangeNs)) continue;
        const ImVec4  tmp = main->getConfig().getThreadColor(tw.threadId);
        const ImColor colorThread = ImColor(tmp.x, tmp.y, tmp.z, vwConst::TEXT_BG_FOOTPRINT_ALPHA);
        float x1 = winX+(float)((firstTimeNs-tl->startTimeNs)*nsToPix);
        float x2 = bsMax(x1+2.f, winX+(float)((lastTimeNs -tl->startTimeNs)*nsToPix));
        DRAWLIST->AddRectFilled(ImVec2(x1, yThread - threadTitleHeight+2.f*fontSpacing),
                                ImVec2(x2, yThread + nestingLevelQty*fontHeight), colorThread);
    }

    // Draw the context switches
    const bsVec<vwMain::TlCachedSwitch>& cachedSwitches = tl->cachedSwitchPerThread[tId];
    const float switchHeight = 0.7f*fontHeight;
    const float ySwitch = yThread-switchHeight;
    for(const vwMain::TlCachedSwitch& cs : cachedSwitches) {
        if(cs.coreId==PL_CSWITCH_CORE_NONE && !cs.isCoarse) continue;

        // Draw the box, with a start line (visually better to indicate the wake up)
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+bsMax(cs.startTimePix+2.f, cs.endTimePix), ySwitch+switchHeight), vwConst::uGrey64);
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+cs.startTimePix+1.5f, ySwitch+switchHeight), vwConst::uGrey128, 2.f);

        // Add the text
        if(!cs.isCoarse) {
            const float csWidth = cs.endTimePix-cs.startTimePix;
            const float scaledFontSize = coreFontRatio*ImGui::GetFontSize();
            if(csWidth>=widthCoreXX) {
                snprintf(tmpStr, sizeof(tmpStr), "Core%d", cs.coreId);
                DRAWLIST->AddText(font, scaledFontSize, ImVec2(winX+cs.startTimePix+0.5f*(csWidth-widthCoreXX), ySwitch+0.5f*(ImGui::GetFontSize()-scaledFontSize)), vwConst::uWhite, tmpStr);
            } else if(csWidth>=widthCoreX) {
                snprintf(tmpStr, sizeof(tmpStr), "%d", cs.coreId);
                DRAWLIST->AddText(font, scaledFontSize, ImVec2(winX+cs.startTimePix+0.5f*(csWidth-widthCoreX), ySwitch+0.5f*(ImGui::GetFontSize()-scaledFontSize)), vwConst::uWhite, tmpStr);
            }
        }

        // Tooltip
        if(isWindowHovered && !cs.isCoarse && mouseX>=winX+cs.startTimePix &&
           mouseX<=winX+bsMax(cs.startTimePix+1.f, cs.endTimePix) && mouseY>=ySwitch && mouseY<ySwitch+switchHeight) {
            ImGui::SetTooltip("Core %d { %s }", cs.coreId, main->getNiceDuration(cs.durationNs));
        }
    }

    // Draw the waiting locks in red
    s64 WAIT_LOCK_LIMIT_NS = 1000*main->getConfig().getLockLatencyUs();
    for(const vwMain::TlCachedLockScope& cl : tl->cachedLockWaitPerThread[tId]) {
        if((cl.e.flags&PL_FLAG_SCOPE_END) && !cl.isCoarse) continue;
        if(!cl.isCoarse && cl.durationNs<WAIT_LOCK_LIMIT_NS) continue;  // Do not highlight small enough lock waiting

        // Draw the box
        bool isHighlighted = !cl.isCoarse && main->isScopeHighlighted(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_WAIT|PL_FLAG_SCOPE_BEGIN, -1, cl.e.nameIdx);
        float x2 = winX+bsMax(cl.startTimePix+2.f, cl.endTimePix);
        ImU32 barColor = cl.isCoarse? IM_COL32(255, 32, 32, 96) : vwConst::uRed;
        DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, ySwitch+switchHeight-4), ImVec2(x2, ySwitch+switchHeight), isHighlighted? vwConst::uYellow : barColor);

        // Hovered?
        if(!cl.isCoarse && isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 &&
           mouseY>=ySwitch+switchHeight-4 && mouseY<=ySwitch+switchHeight) {
            // Highlight the corresponding wait scope
            main->setScopeHighlight(-1, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_WAIT|PL_FLAG_SCOPE_BEGIN, -1, cl.e.nameIdx);
            // Tooltip
            ImGui::BeginTooltip();
            ImGui::TextColored(vwConst::gold, "Thread waiting for lock '%s' { %s }", record->getString(cl.e.nameIdx).value.toChar(), main->getNiceDuration(cl.durationNs));
            if(cl.e.lineNbr) {
                ImGui::Text("At line"); ImGui::SameLine();
                ImGui::TextColored(vwConst::grey, "%d", cl.e.lineNbr); ImGui::SameLine();
                ImGui::Text("in file"); ImGui::SameLine();
            } else {
                ImGui::Text("In"); ImGui::SameLine();
            }
            ImGui::TextColored(vwConst::grey, "%s", record->getString(cl.e.filenameIdx).value.toChar());
            ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0, 0, timeFormat));
            ImGui::EndTooltip();
        }
    }

    // Draw the logs
    const float yLog = yThread-fontHeight;
    const float logHalfWidthPix = 4.f;
    const float logHeightPix    = 0.3f*fontHeight;
    const float logThickness    = 2.f;
    const bsVec<ImVec4>& colors = main->getConfig().getColorPalette(true);
    float hlTimePix = -1.f;
    for(const vwMain::TlCachedLog& cm : tl->cachedLogPerThread[tId]) {
        ImGui::PushID(&cm);
        bool isHovered = (!cm.isCoarse && isWindowHovered && mouseX>=winX+cm.timePix-logHalfWidthPix-logThickness &&
                          mouseX<=winX+cm.timePix+logHalfWidthPix+logThickness && mouseY>=yLog-logThickness && mouseY<=yLog+logHeightPix+logThickness);
        if(isHovered || main->isScopeHighlighted(cm.e.threadId, cm.e.vS64, cm.e.flags, -1, cm.e.nameIdx)) hlTimePix = cm.timePix;

        // Draw the triangles
        DRAWLIST->AddTriangleFilled(ImVec2(winX+cm.timePix-logHalfWidthPix-logThickness, yLog-logThickness),
                                    ImVec2(winX+cm.timePix+logHalfWidthPix+logThickness, yLog-logThickness),
                                    ImVec2(winX+cm.timePix, yLog+logHeightPix+logThickness),
                                    (cm.isCoarse || cm.elemIdx<0)? vwConst::uGrey : ImU32(ImColor(main->getConfig().getCurveColor(cm.elemIdx))));
        DRAWLIST->AddTriangleFilled(ImVec2(winX+cm.timePix-logHalfWidthPix, yLog), ImVec2(winX+cm.timePix+logHalfWidthPix, yLog),
                                    ImVec2(winX+cm.timePix, yLog+logHeightPix),
                                    cm.isCoarse? vwConst::uGrey : ImU32(ImColor(colors[cm.e.filenameIdx%colors.size()])));

        // Hovered?
        if(isHovered) {
            main->setScopeHighlight(tId, cm.e.vS64, PL_FLAG_TYPE_LOG, -1, cm.e.nameIdx);
            // Clicked?
            if(ImGui::IsMouseReleased(0)) {
                // Synchronize the text (after getting the nesting level and lIdx for this date on this thread)
                int nestingLevel;
                u32 lIdx;
                cmGetRecordPosition(record, cm.e.threadId, cm.e.vS64, nestingLevel, lIdx);
                main->synchronizeText(tl->syncMode, cm.e.threadId, nestingLevel, lIdx, cm.e.vS64, tl->uniqueId);
            }
            if(ImGui::IsMouseReleased(2) && cm.elemIdx>=0) {
                main->_plotMenuItems.clear(); // Reset the popup menu state
                u64 itemHashPath = bsHashStepChain(record->threads[cm.e.threadId].threadHash, record->getString(cm.e.filenameIdx).hash, cmConst::LOG_NAMEIDX);
                int* elemIdxPtr  = record->elemPathToId.find(itemHashPath, cmConst::LOG_NAMEIDX);
                if(elemIdxPtr) {
                    main->prepareGraphLogContextualMenu(*elemIdxPtr, tl->getStartTimeNs(), tl->getTimeRangeNs(), false);
                    ImGui::OpenPopup("log menu");
                }
            }
            // Tooltip
            ImGui::BeginTooltip();
            switch(cm.e.lineNbr&0x7FFF) {
            case 0: ImGui::TextColored(vwConst::grey,       "[debug]"); break;
            case 1: ImGui::TextColored(vwConst::cyan,       "[info]");  break;
            case 2: ImGui::TextColored(vwConst::darkOrange, "[warn]");  break;
            case 3: ImGui::TextColored(vwConst::red,        "[error]"); break;
            };
            ImGui::SameLine();
            ImGui::TextColored(vwConst::gold, "[%s] %s", record->getString(cm.e.nameIdx).value.toChar(), cm.message.toChar());
            ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cm.e.vS64, 0, 0, timeFormat));
            ImGui::EndTooltip();
        }

        // Popup
        if(ImGui::BeginPopup("log menu", ImGuiWindowFlags_AlwaysAutoResize)) {
            float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
            ImGui::TextColored(vwConst::grey, "[%s] %s", record->getString(cm.e.nameIdx).value.toChar(),
                               record->getString(cm.e.filenameIdx).value.toChar());
            // Plot & histogram
            if(!main->_plotMenuItems.empty()) {
                ImGui::Separator();
                ImGui::Separator();
                if(!main->displayPlotContextualMenu(tId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                ImGui::Separator();
                if(!main->displayHistoContextualMenu(headerWidth))             ImGui::CloseCurrentPopup();
            }
            // Log window
            if(main->_logViews.empty()) {
                ImGui::Separator();
                if(ImGui::Selectable("Add a log window")) main->addLog(main->getId(), cm.e.vS64);
            }

            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    // Highlight is overwritten after full display to avoid display order masking
    if(hlTimePix>=0) {
        DRAWLIST->AddTriangleFilled(ImVec2(winX+hlTimePix-logHalfWidthPix, yLog), ImVec2(winX+hlTimePix+logHalfWidthPix, yLog),
                                    ImVec2(winX+hlTimePix, yLog+logHeightPix), vwConst::uWhite);
    }

    const float dim2 = 0.8f; // Alternate level
    const float dimS = 0.6f; // Small
    const float dimO = 0.5f; // Outline
    const ImVec4 colorBase = main->getConfig().getThreadColor(tId);
    colorFill1   = ImColor(colorBase);
    colorFill2   = ImColor(dim2*colorBase.x, dim2*colorBase.y, dim2*colorBase.z);
    colorFillS   = ImColor(dimS*colorBase.x, dimS*colorBase.y, dimS*colorBase.z);
    colorOutline = ImColor(dimO*colorBase.x, dimO*colorBase.y, dimO*colorBase.z);

    // Loop on nesting levels
    for(int nestingLevel=0; nestingLevel<nestingLevelQty; ++nestingLevel) {
        plgScope (TML, "Display nesting level");
        plgVar(TML, nestingLevel);
        SmallItem si;
        s64 lastScopeEndTimeNs = 0;
        int y = (int)(yThread+nestingLevel*fontHeight);

        // Loop on scopes from the cached record
        const bsVec<vwMain::InfTlCachedScope>& cachedScopes = tl->cachedScopesPerThreadPerNLevel[tId][nestingLevel];
        for(const vwMain::InfTlCachedScope& b : cachedScopes) {
            if(b.startTimePix>winWidth) break;
            // Close previous small scope if hole is large enough or current item is not small
            if(si.isInit && (b.startTimePix-si.endPix>=MIN_SCOPE_PIX || (!b.isCoarseScope && b.endTimePix-b.startTimePix>=MIN_SCOPE_PIX))) {
                if(si.endPix-si.startPix<0.75f*MIN_SCOPE_PIX) si.endPix = si.startPix+0.75f*MIN_SCOPE_PIX; // Ensure a minimum displayed size
                // Display a scope
                if(si.hasEvt) displayScope(tId, nestingLevel, si.scopeLIdx, si.evt,
                                           si.startPix, si.endPix, (float)y, si.evtDurationNs, lastScopeEndTimeNs, yThread); // One event only, so full display
                else          displaySmallScope(si, nestingLevel, nestingLevelQty, (float)y, lastScopeEndTimeNs);   // Agglomerated events, so anonymous
                lastScopeEndTimeNs = (s64)(si.endPixExact/nsToPix+tl->startTimeNs);
                si.isInit = false;
            }

            // Display current one
            if(b.isCoarseScope || b.endTimePix-b.startTimePix<MIN_SCOPE_PIX) {
                // This scope is small
                si.endPixExact = si.endPix = b.endTimePix;
                si.hasEvt = false;
                if(!si.isInit) {
                    si.startPix = b.startTimePix; // First of a potential serie
                    si.isInit = true;
                    if(!b.isCoarseScope) {
                        si.evt           = b.evt;
                        si.evtDurationNs = b.durationNs;
                        si.scopeLIdx     = b.scopeLIdx;
                        si.hasEvt        = true; // Can be displayed normally if alone
                    }
                }
                else si.hasEvt = false; // Concatenated blocs become anonymous
            }
            else {
                // Display the normal scope
                displayScope(tId, nestingLevel, b.scopeLIdx, b.evt, b.startTimePix, b.endTimePix, (float)y, b.durationNs, lastScopeEndTimeNs, yThread);
                lastScopeEndTimeNs = b.scopeEndTimeNs;
            }
        } // End of loop on scopes for this nesting level

        // Finish to draw the small items, if not completed
        if(si.isInit) {
            if(si.endPix-si.startPix<0.75f*MIN_SCOPE_PIX) si.endPix = si.startPix+0.75f*MIN_SCOPE_PIX; // Ensure a minimum displayed size
            if(si.hasEvt) displayScope(tId, nestingLevel, si.scopeLIdx, si.evt,
                                       si.startPix, si.endPix, (float)y, si.evtDurationNs, lastScopeEndTimeNs, yThread); // One event only, so full display
            else          displaySmallScope(si, nestingLevel, nestingLevelQty, (float)y, lastScopeEndTimeNs);   // Agglomerated events, so anonymous
            lastScopeEndTimeNs = (s64)(si.endPixExact/nsToPix+tl->startTimeNs);
        }

        // And the gap at the end
        if(!cachedScopes.empty() && cachedScopes.back().startTimePix>winWidth) {
            highlightGapIfHovered(lastScopeEndTimeNs, cachedScopes.back().startTimePix, (float)y);
        }
    } // End of loop on levels for each thread

    // Draw the Soft IRQs
    const bsVec<vwMain::TlCachedSoftIrq>& cachedSoftIrq = tl->cachedSoftIrqPerThread[tId];
    for(const vwMain::TlCachedSoftIrq& cs : cachedSoftIrq) {
        // Small line on top of the core representation to show the IRQ on the global scale (multi-res helps)
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+bsMax(cs.startTimePix+2.f, cs.endTimePix), ySwitch+2.f), vwConst::uLightGrey);
        // Dark shadow to show the frozen thread, if large enough
        if(!cs.isCoarse && cs.endTimePix-cs.startTimePix>2.f) {
            DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch), ImVec2(winX+cs.endTimePix, yThread+nestingLevelQty*fontHeight),
                                    IM_COL32(32, 32, 32, 64));
        }
        // Tooltip
        if(isWindowHovered && !cs.isCoarse && mouseX>=winX+cs.startTimePix && mouseX<=winX+bsMax(cs.startTimePix+1., cs.endTimePix) &&
           mouseY>=ySwitch && mouseY<=ySwitch+switchHeight) {
            ImGui::SetTooltip("SOFTIRQ %s { %s }", record->getString(cs.nameIdx).value.toChar(), main->getNiceDuration(cs.durationNs));
        }
    }

    // Highlight the hovered used lock in transparent white, both if directly hovered or if any thread waits for it
    if(main->isScopeHighlighted(tId, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs, PL_FLAG_TYPE_LOCK_ACQUIRED, -1, PL_INVALID) ||
       main->isScopeHighlighted(-1, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs, PL_FLAG_TYPE_LOCK_WAIT| PL_FLAG_SCOPE_BEGIN, -1, PL_INVALID)) {
        float startScopePix = (float)((main->_hlStartTimeNs-tl->startTimeNs)*nsToPix);
        float endScopePix   = (float)((main->_hlEndTimeNs  -tl->startTimeNs)*nsToPix);
        // Loop on locks
        for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
            if(record->locks[lockIdx].nameIdx!=main->_hlNameIdx) continue; // Not the hovered lock
            const vwMain::TlCachedLockUse&  clu  = tl->cachedLockUse[lockIdx];
            // Loop on lock scopes
            for(const vwMain::TlCachedLockScope& cl : clu.scopes) {
                if(cl.isCoarse || cl.e.threadId!=tId || cl.startTimePix>=endScopePix || cl.endTimePix<startScopePix) continue;
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread),
                                        ImVec2(winX+bsMax(cl.startTimePix+2.f, cl.endTimePix), yThread+nestingLevelQty*fontHeight),
                                        IM_COL32(255, 255, 255, 96));
            }
        }
    }


    // Contextual menu
    // ===============
    ImGui::PushID(tId);
    ImGui::PushID("context menu");

    // Open the popup if asked
    if(tl->ctxDoOpenContextMenu) {
        main->_plotMenuItems.clear(); // Reset the popup menu state
        if(main->prepareGraphContextualMenu(tId, tl->ctxNestingLevel, tl->ctxScopeLIdx, tl->getStartTimeNs(), tl->getTimeRangeNs())) {
            ImGui::OpenPopup("Profile scope menu");
        }
        tl->ctxDoOpenContextMenu = false;
    }

    // Draw the main menu popup
    if(tl->ctxScopeLIdx!=PL_INVALID && ImGui::BeginPopup("Profile scope menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        float headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
        // Scope title
        ImGui::TextColored(vwConst::grey, "Scope '%s'", record->getString(tl->ctxScopeNameIdx).value.toChar());
        ImGui::Separator();
        ImGui::Separator();

        // Plot & histogram
        if(!main->displayPlotContextualMenu(tId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
        ImGui::Separator();
        if(!main->displayHistoContextualMenu(headerWidth))             ImGui::CloseCurrentPopup();

        // Profiles (only if children)
        if(main->_plotMenuHasScopeChildren) {
            ImGui::Separator();
            if(ImGui::MenuItem("Profile timings"))
                { main->addProfileScope(main->getId(), vwMain::TIMINGS, tId, tl->ctxNestingLevel, tl->ctxScopeLIdx); ImGui::CloseCurrentPopup(); }
            bool hasMemInfos = (record->threads[tId].memEventQty>0); // @#TODO [MEMORY] Really look into children, else user may be confused
            if(hasMemInfos && ImGui::MenuItem("Profile allocated memory"))
                { main->addProfileScope(main->getId(), vwMain::MEMORY,      tId, tl->ctxNestingLevel, tl->ctxScopeLIdx); ImGui::CloseCurrentPopup(); }
            if(hasMemInfos && ImGui::MenuItem("Profile allocation calls"))
                { main->addProfileScope(main->getId(), vwMain::MEMORY_CALLS, tId, tl->ctxNestingLevel, tl->ctxScopeLIdx); ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    ImGui::PopID();

    // Next thread
    yThread += nestingLevelQty*fontHeight;
}



// Prepare data
// =============

bool
vwMain::addTimeline(int id)
{
    if(!_record) return false;
    _timelines.push_back( { } );
    auto& tl    = _timelines.back();
    tl.uniqueId = id;
    getSynchronizedRange(tl.syncMode, tl.startTimeNs, tl.timeRangeNs);
    memset(&tl.valuePerThread[0], 0, sizeof(tl.valuePerThread));
    setFullScreenView(-1);
    plLogInfo("user", "Add a timeline");
    return true;
}


void
vwMain::prepareTimeline(Timeline& tl)
{
    // Worth working?
    const float winWidth = bsMax(1.f, ImGui::GetWindowContentRegionMax().x-vwConst::OVERVIEW_VBAR_WIDTH);
    if(!tl.isCacheDirty && tl.lastWinWidth==winWidth) return;
    tl.isCacheDirty = false;
    tl.lastWinWidth = winWidth;

    // Init
    plgScope(TML, "prepareTimeline");
    double nsToPix  = (double)winWidth/(double)tl.timeRangeNs;
    tl.cachedUsagePerCore.clear();
    tl.cachedUsagePerCore.resize(_record->coreQty);
    tl.cachedCpuCurve.clear(); tl.cachedCpuCurve.reserve(256);
    tl.cachedSwitchPerThread.clear();
    tl.cachedSwitchPerThread.resize(_record->threads.size());
    tl.cachedSoftIrqPerThread.clear();
    tl.cachedSoftIrqPerThread.resize(_record->threads.size());
    tl.cachedLockUse.clear();
    tl.cachedLockUse.resize(_record->locks.size());
    tl.cachedLockNtf.clear();
    tl.cachedLockNtf.resize(_record->locks.size());
    tl.cachedLockWaitPerThread.clear();
    tl.cachedLockWaitPerThread.resize(_record->threads.size());
    tl.cachedLogPerThread.clear();
    tl.cachedLogPerThread.resize(_record->threads.size());
    tl.cachedScopesPerThreadPerNLevel.clear();
    tl.cachedScopesPerThreadPerNLevel.resize(_record->threads.size());

    // Core usage
    // ==========
    plgBegin(TML, "Cores");

    // Get the CPU usage curve
    float cpuRatioCoef = 1.f/(float)bsMax(1, _record->coreQty);
    cmRecordIteratorCpuCurve itcpu(_record, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
    s64 ptTimeNs; int usedCoreQty;
    while(itcpu.getNextPoint(ptTimeNs, usedCoreQty)) {
        tl.cachedCpuCurve.push_back( { (float)(nsToPix*(ptTimeNs-tl.startTimeNs)), cpuRatioCoef*usedCoreQty } );
        if(ptTimeNs>tl.startTimeNs+tl.timeRangeNs) break; // Time break at the end, as we want 1 point past the range
    }

    // Loop on cores
    for(int coreId=0; coreId<_record->coreQty; ++coreId) {
        // Cache the core usage
        bsVec<TlCachedCore>& cachedCore = tl.cachedUsagePerCore[coreId];
        cachedCore.clear();
        cachedCore.reserve(256);
        if(!getConfig().getThreadExpanded(vwConst::CORE_USAGE_THREADID)) continue; // No visible, so skip
        bool isCoarseScope = false;
        s64  timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
        int  threadId = -1, prevThreadId = -1;
        u32  nameIdx  = 0xFFFFFFFE, prevNameIdx  = 0xFFFFFFFE;
        float prevTimePix = -1.;

        cmRecordIteratorCoreUsage itcu(_record, coreId, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
        while(itcu.getNextSwitch(isCoarseScope, timeNs, endTimeNs, threadId, nameIdx)) {
            plgAssert(TML, nameIdx!=PL_INVALID || threadId<cmConst::MAX_THREAD_QTY, isCoarseScope, nameIdx, threadId);
            // Double event: just replace the "previous" data
            float timePix = (float)(nsToPix*(timeNs-tl.startTimeNs));
            if(!isCoarseScope && timeNs==prevTimeNs) {
                prevTimeNs   = timeNs;
                prevTimePix  = timePix;
                prevThreadId = threadId;
                prevNameIdx  = nameIdx;
                continue;
            }
            if(isCoarseScope) {
                float endTimePix = (float)bsMin(nsToPix*(endTimeNs-tl.startTimeNs), winWidth);
                cachedCore.push_back( { true, 0xFFFF, PL_INVALID, bsMax(0.f, (prevNameIdx==0xFFFFFFFE)? timePix : prevTimePix), endTimePix, 0 } );
            }
            else if(prevTimeNs>=0 && timePix>=0 && prevNameIdx!=0xFFFFFFFE) {
                cachedCore.push_back( { false, (u16)prevThreadId, prevNameIdx, bsMax(0.f, prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs } );
            }

            // Next switch
            if(timePix>winWidth) break;
            prevTimeNs   = timeNs;
            prevTimePix  = timePix;
            prevThreadId = threadId;
            prevNameIdx  = nameIdx;
        } // End of caching of context switches
    }
    plgEnd(TML, "Cores");

    // Loop on used locks
    // ==================
    // Done whatever the visibility of the lock timeline, as the precomputations are used to highlight in all thread timelines
    static_assert(vwConst::MAX_OVERLAPPED_THREAD==8, "Initialization code below shall be adapted");  // Else the initialization below (with the 0xFF, 0xFF...) shall be adapted
    plgBegin(TML, "Used locks");
    for(int lockIdx=0; lockIdx<_record->locks.size(); ++lockIdx) {
        // Cache the used lock
        bool   isCoarseScope = false, prevIsCoarse = false;
        s64    timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
        float prevTimePix = -1.f, endTimePix = -1.f;
        cmRecord::Evt prevE, e;
        prevE.nameIdx = PL_INVALID; prevE.flags = PL_FLAG_TYPE_LOCK_RELEASED;
        TlCachedLockUse& cachedLockUse = tl.cachedLockUse[lockIdx];
        cachedLockUse.scopes.clear();
        cachedLockUse.scopes.reserve(128);
        int waitingThreadQty = _record->locks[lockIdx].waitingThreadIds.size();
        cachedLockUse.waitingThreadScopes.resize(waitingThreadQty);
        for(int i=0; i<waitingThreadQty; ++i) cachedLockUse.waitingThreadScopes[i].clear();

        cmRecordIteratorLockUse itLockUse(_record, _record->locks[lockIdx].nameIdx, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
        while(itLockUse.getNextLock(isCoarseScope, timeNs, endTimeNs, e)) {
            float timePix = (float)(nsToPix*(timeNs-tl.startTimeNs));
            if(isCoarseScope) {
                endTimePix = (float)(nsToPix*(endTimeNs-tl.startTimeNs));
                cachedLockUse.scopes.push_back( { true, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                        bsMax(0.f, (prevE.flags==PL_FLAG_TYPE_LOCK_RELEASED)? timePix : prevTimePix), bsMin(endTimePix, winWidth), 0 } );
            }
            if(prevTimeNs>=0 && timePix>=0.f && e.flags==PL_FLAG_TYPE_LOCK_RELEASED) {
                cachedLockUse.scopes.push_back( { prevIsCoarse, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                        bsMax(0.f, prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs, prevE } );
            }

            // Next switch
            if(timePix>winWidth) break;
            prevIsCoarse = isCoarseScope;
            prevTimeNs   = isCoarseScope? endTimeNs  : timeNs;
            prevTimePix  = isCoarseScope? endTimePix : timePix;
            prevE        = e;
        } // End of loop on lock usage events

        // Cache the lock notifications
        bsVec<TlCachedLockNtf>& cachedLockNtf = tl.cachedLockNtf[lockIdx];
        cachedLockNtf.clear();
        cachedLockNtf.reserve(128);
        cmRecordIteratorLockNtf itLockNtf(_record, _record->locks[lockIdx].nameIdx, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);

        while(itLockNtf.getNextLock(isCoarseScope, e)) {
            float timePix = (float)(nsToPix*(e.vS64-tl.startTimeNs));
            cachedLockNtf.push_back({ isCoarseScope, timePix, e });
            if(timePix>winWidth) break;
        } // End of loop on lock notification events

    } // End of caching the used locks

    // Create the lock reordering lookup (alphabetical)
    if(tl.cachedLockOrderedIdx.size()!=_record->locks.size()) {
        tl.cachedLockOrderedIdx.clear();
        for(int lockIdx=0; lockIdx<_record->locks.size(); ++lockIdx) {
            tl.cachedLockOrderedIdx.push_back(lockIdx);
        }
        std::sort(tl.cachedLockOrderedIdx.begin(), tl.cachedLockOrderedIdx.end(),
                  [this](int& a, int& b)->bool {
                      return _record->getString(_record->locks[a].nameIdx).alphabeticalOrder<
                          _record->getString(_record->locks[b].nameIdx).alphabeticalOrder;
                  });
    }

    plgEnd(TML, "Used locks");

    // Loop on threads
    // ===============
    int* idxPerUsedLock = (int*) alloca(_record->locks.size()*sizeof(int));
    for(int tId=0; tId<_record->threads.size(); ++tId) {
        plgScope (TML, "Thread scopes");
        plgVar(TML, tId);
        const cmRecord::Thread& rt = _record->threads[tId];
        bool isExpanded = getConfig().getThreadVisible(tId) && getConfig().getGroupAndThreadExpanded(tId);

        // Cache the context switches
        bsVec<TlCachedSwitch>& cachedSwitches = tl.cachedSwitchPerThread[tId];
        cachedSwitches.clear();
        if(isExpanded) {
            plgScope(TML, "Ctx switches");
            cachedSwitches.reserve(256);
            bool   isCoarseScope = false, prevIsCoarse = false;
            s64    timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
            int    coreId = 0, prevCoreId = -1;
            float prevTimePix = -1.f, endTimePix = -1.f;
            cmRecordIteratorCtxSwitch itcs(_record, tId, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            while(itcs.getNextSwitch(isCoarseScope, timeNs, endTimeNs, coreId)) {
                float timePix = (float)(nsToPix*(timeNs-tl.startTimeNs));
                if(isCoarseScope) {
                    endTimePix = (float)(nsToPix*(endTimeNs-tl.startTimeNs));
                    cachedSwitches.push_back( { true, 0, bsMax(0.f, (prevCoreId==PL_CSWITCH_CORE_NONE)? timePix : prevTimePix),
                            bsMin(endTimePix, winWidth), 0 } );
                }
                if(prevTimeNs>=0 && timePix>=0) {
                    cachedSwitches.push_back( { prevIsCoarse, (u16)((s16)prevCoreId), bsMax(0.f, prevTimePix),
                            bsMin(timePix, winWidth), timeNs-prevTimeNs } );
                }
                // Next switch
                if(timePix>winWidth) break;
                prevIsCoarse = isCoarseScope;
                prevTimeNs   = isCoarseScope? endTimeNs  : timeNs;
                prevTimePix  = isCoarseScope? endTimePix : timePix;
                prevCoreId    = coreId; // @#TBC What happens when isCoarseScope, as coreId is not set?
            } // End of caching of context switches
        }

        // Cache the softIrq switches
        bsVec<TlCachedSoftIrq>& cachedSoftIrq = tl.cachedSoftIrqPerThread[tId];
        cachedSoftIrq.clear();
        if(isExpanded) {
            plgScope(TML, "Soft IRQs");
            cachedSoftIrq.reserve(128);
            bool   isCoarseScope = false, prevIsCoarse = false;
            s64    timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
            u32    nameIdx = 0xFFFFFFFF, prevNameIdx = 0xFFFFFFFF;
            float prevTimePix = -1.f, endTimePix = -1.f;
            cmRecordIteratorSoftIrq itSoftIrq(_record, tId, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            while(itSoftIrq.getNextSwitch(isCoarseScope, timeNs, endTimeNs, nameIdx)) {
                float timePix = (float)(nsToPix*(timeNs-tl.startTimeNs));
                if(isCoarseScope) {
                    endTimePix = (float)(nsToPix*(endTimeNs-tl.startTimeNs));
                    cachedSoftIrq.push_back( { true, 0, bsMax(0.f, prevNameIdx!=0xFFFFFFFF? prevTimePix : timePix), bsMin(endTimePix, winWidth), 0 } );
                    nameIdx = 0xFFFFFFFF;
                }
                else if(prevTimeNs>=0 && timePix>=0 && prevNameIdx!=0xFFFFFFFF) {
                    cachedSoftIrq.push_back( { prevIsCoarse, prevNameIdx, bsMax(0.f, prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs } );
                }
                // Next switch
                if(timePix>winWidth) break;
                prevIsCoarse = isCoarseScope;
                prevTimeNs   = isCoarseScope? endTimeNs  : timeNs;
                prevTimePix  = isCoarseScope? endTimePix : timePix;
                prevNameIdx  = nameIdx;
            } // End of caching of context switches
        }


        // Cache the lock waits
        bsVec<TlCachedLockScope>& cachedLockWaits = tl.cachedLockWaitPerThread[tId]; // For drawing the top red lines in the timelines
        cachedLockWaits.clear();
        static_assert(vwConst::MAX_OVERLAPPED_THREAD==8, "Initialization code below shall be adapted");  // Else the initialization below (with the 0xFF, 0xFF...) shall be adapted
        { // Always computed to have the information for the lock timeline
            plgScope(TML, "Lock wait");
            cachedLockWaits.reserve(128);
            bool   isCoarseScope = false, prevIsCoarse = false;
            s64    timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
            float prevTimePix = -1.f, endTimePix = -1.f;
            cmRecord::Evt prevE, e; prevE.flags = 0; prevE.threadId = 0xFF; prevE.nameIdx = 0xFFFFFFFF;
            if(!_record->locks.empty()) memset(&idxPerUsedLock[0], 0, _record->locks.size()*sizeof(int));
            cmRecordIteratorLockWait itLockWait(_record,  tId, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            s64 WAIT_LOCK_LIMIT_NS = 1000*getConfig().getLockLatencyUs();

            while(itLockWait.getNextLock(isCoarseScope, timeNs, endTimeNs, e)) { // @#BUG Probably last event (coarse at least) is not stored as a scope.
                float timePix = (float)(nsToPix*(timeNs-tl.startTimeNs));
                bool  prevIsBegin = (prevE.flags&PL_FLAG_SCOPE_BEGIN);
                if(isCoarseScope) {
                    endTimePix = (float)(nsToPix*(endTimeNs-tl.startTimeNs));
                    cachedLockWaits.push_back( { true, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                            bsMax(0.f, prevIsBegin? prevTimePix : timePix), bsMin(endTimePix, winWidth), 0 } );
                }
                if(prevTimeNs>=0 && timePix>=0.f) {
                    cachedLockWaits.push_back( { prevIsCoarse, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                            bsMax(0.f, prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs, prevE } );
                    // Store in the "lock use" section for this thread
                    if(!prevIsCoarse && prevIsBegin && timeNs-prevTimeNs>=WAIT_LOCK_LIMIT_NS) {
                        int eThreadId = prevE.threadId;
                        int lockIdx   = _record->getString(prevE.nameIdx).lockId;
                        plAssert(lockIdx>=0);
                        bsVec<TlCachedLockScope>& useScopes = tl.cachedLockUse[lockIdx].scopes;
                        const bsVec<int>& waitingThreadIds = _record->locks[lockIdx].waitingThreadIds;
                        for(int tIdx=0; tIdx<waitingThreadIds.size(); ++tIdx) {
                            if(eThreadId!=waitingThreadIds[tIdx]) continue;
                            TlCachedLockScope& lastScope = cachedLockWaits.back();
                            // Update the associated taken lock scope, if overlapped
                            while(1) {
                                int& ulIdx = idxPerUsedLock[lockIdx];
                                if(ulIdx>=useScopes.size()) break;
                                if(useScopes[ulIdx].endTimePix<lastScope.startTimePix) { ++ulIdx; continue; }
                                if(useScopes[ulIdx].startTimePix>=lastScope.endTimePix) break;
                                // Overlap case
                                for(int i=0; i<vwConst::MAX_OVERLAPPED_THREAD; ++i) if(useScopes[ulIdx].overlappedThreadIds[i]==0xFF) { useScopes[ulIdx].overlappedThreadIds[i] = (u8)eThreadId; break; }
                                for(int i=0; i<vwConst::MAX_OVERLAPPED_THREAD; ++i) if(lastScope.overlappedThreadIds[i]==0xFF) { lastScope.overlappedThreadIds[i] = useScopes[ulIdx].e.threadId; break; }
                                if(useScopes[ulIdx].endTimePix<lastScope.endTimePix) ++ulIdx;
                                else break;
                            }
                            // Add this wait scope to the taken lock
                            tl.cachedLockUse[lockIdx].waitingThreadScopes[tIdx].push_back(lastScope);
                            break;
                        }
                    }
                }

                // Next switch
                if(timePix>winWidth) break;
                prevIsCoarse = isCoarseScope;
                prevTimeNs   = isCoarseScope? endTimeNs  : timeNs;
                prevTimePix  = isCoarseScope? endTimePix : timePix;
                prevE        = e;
            } // End of loop on events
        }

        // Cache the logs
        bsVec<TlCachedLog>& cachedLog = tl.cachedLogPerThread[tId];
        cachedLog.clear();
        if(isExpanded) {
            plgScope(TML, "Logs");
            cachedLog.reserve(128);
            char messageStr[512];
            bsVec<cmLogParam> params;
            u64 threadHash = _record->threads[tId].threadHash;
            int* elemIdx  = _record->elemPathToId.find(bsHashStepChain(threadHash, tl.logLevel, cmConst::LOG_NAMEIDX), cmConst::LOG_NAMEIDX);
            if(elemIdx) {
                bool isCoarse; cmRecord::Evt e;
                cmRecordIteratorLog itLog(_record, *elemIdx, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
                while(itLog.getNextLog(isCoarse, e, params)) {
                    float timePix = (float)(nsToPix*(e.vS64-tl.startTimeNs));
                    elemIdx = _record->elemPathToId.find(bsHashStepChain(threadHash, 0, _record->getString(e.nameIdx).hash, cmConst::LOG_NAMEIDX), cmConst::LOG_NAMEIDX);
                    cmVsnprintf(messageStr, sizeof(messageStr), _record->getString(e.filenameIdx).value.toChar(), _record, params);
                    cachedLog.push_back( { isCoarse, elemIdx? *elemIdx : -1, timePix, e, messageStr } );
                    if(timePix>winWidth) break;
                }
            }
        }

        // Loop on nesting levels
        bsVec<bsVec<InfTlCachedScope>>& cachedScopesPerNLevel = tl.cachedScopesPerThreadPerNLevel[tId];
        int nestingLevelQty = rt.levels.size();
        if(nestingLevelQty>0 && rt.levels[nestingLevelQty-1].scopeChunkLocs.empty()) --nestingLevelQty; // Last level is pure non-scope data @#TEMP Review this code
        cachedScopesPerNLevel.resize(nestingLevelQty);
        for(int nestingLevel=0; nestingLevel<nestingLevelQty; ++nestingLevel) {
            bsVec<InfTlCachedScope>& cachedScopes = cachedScopesPerNLevel[nestingLevel];;
            cachedScopes.clear();
            if(!isExpanded) continue;
            cachedScopes.reserve(256);

            plgScope (TML, "Prepare nesting level");
            plgVar(TML, nestingLevel);
            cmRecordIteratorScope it(_record, tId, nestingLevel, tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            float startTimePix = 0.;
            float endTimePix   = 0.;
            s64 lastScopeEndTimeNs = 0; // Just for sanity
            s64  scopeStartTimeNs=0, scopeEndTimeNs=0, durationNs=0;
            cmRecord::Evt evt;
            u32 scopeLIdx = PL_INVALID; // Scope index at this level
            bool isCoarseScope;

            // Cache the generic events
            while((scopeLIdx=it.getNextScope(isCoarseScope, scopeStartTimeNs, scopeEndTimeNs, evt, durationNs))!=PL_INVALID) {
                plAssert(isCoarseScope || (evt.flags&PL_FLAG_SCOPE_BEGIN), isCoarseScope, evt.flags, nestingLevel, nestingLevelQty);
                plgScope(TML, "Found data");
                if(isCoarseScope) { // Case coarse scope
                    plgData(TML, "Scope start time (s)", 0.000000001*scopeStartTimeNs);
                    plgData(TML, "Scope duration   (s)", 0.000000001*(scopeEndTimeNs-scopeStartTimeNs));
                    startTimePix = (float)(nsToPix*(scopeStartTimeNs-tl.startTimeNs));
                    endTimePix   = (float)(nsToPix*(scopeEndTimeNs  -tl.startTimeNs));
                }
                else { // Case full resolution
                    scopeStartTimeNs = evt.vS64;
                    plgData(TML, "Event start time (s)", 0.000000001*scopeStartTimeNs);
                    plgData(TML, "Event duration   (s)", 0.000000001*durationNs);
                    scopeEndTimeNs = scopeStartTimeNs+durationNs;
                    startTimePix  = (float)(nsToPix*(scopeStartTimeNs-tl.startTimeNs));
                    endTimePix    = (float)(nsToPix*(scopeEndTimeNs  -tl.startTimeNs));
                }
                if(endTimePix<0) { plgData(TML, "Negative end time", endTimePix); continue; }
                plAssert(lastScopeEndTimeNs<=scopeStartTimeNs, lastScopeEndTimeNs, scopeStartTimeNs, scopeEndTimeNs);
                lastScopeEndTimeNs = scopeEndTimeNs;

                // Store in the cache
                cachedScopes.push_back( { isCoarseScope, scopeLIdx, scopeEndTimeNs, durationNs, evt, startTimePix, endTimePix } );
                if(startTimePix>winWidth) { plgText(TML, "State", "End of level display"); break; }
            } // End of loop on events
        } // End of loop on nesting levels
    } // End of loop on threads
}


// Draw the timeline
// =================

void
vwMain::drawTimelines(void)
{
    if(!_record) return;
    plgScope(TML, "drawTimelines");
    char tmpStr[128];

    // Loop on memory timelines
    int itemToRemoveIdx = -1;
    for(int tlWindowIdx=0; tlWindowIdx<_timelines.size(); ++tlWindowIdx) {
        Timeline& tl = _timelines[tlWindowIdx];
        if(_liveRecordUpdated) tl.isCacheDirty = true;
        if(_uniqueIdFullScreen>=0 && tl.uniqueId!=_uniqueIdFullScreen) continue;

        if(tl.isNew) {
            tl.isNew = false;
            if(tl.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(tl.newDockId);
            else selectBestDockLocation(true, true);
        }
        if(tl.isWindowSelected) {
            tl.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }

        snprintf(tmpStr, sizeof(tmpStr), "Timeline #%d", tl.uniqueId);
        bool isOpen = true;
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
            drawTimeline(tlWindowIdx);
        }
        ImGui::End();

        if(!isOpen) itemToRemoveIdx = tlWindowIdx;
    }

    // Remove timelines (if asked)
    if(itemToRemoveIdx>=0) {
        releaseId((_timelines.begin()+itemToRemoveIdx)->uniqueId);
        _timelines.erase(_timelines.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::drawTimeline(int tlWindowIdx)
{
    if(!_record) return;
    plgScope(TML, "drawTimeline");
    ImGuiIO& io       = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    const s64 recordDurationNs = _record->durationNs;
    Timeline& tl = _timelines[tlWindowIdx];

    // Handle animation (smooth move, boundaries, and live record view behavior)
    tl.updateAnimation();
    tl.checkTimeBounds(recordDurationNs);

    // Ruler and visible range bar
    float rbWidth, rbStartPix, rbEndPix;
    float rulerHeight = getTimelineHeaderHeight(false, true);
    ImGui::BeginChild("ruler", ImVec2(0, 2.0f*ImGui::GetStyle().WindowPadding.y+rulerHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
    const bool isBarHovered  = ImGui::IsWindowHovered();
    drawTimeRuler(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowContentRegionMax().x, rulerHeight,
                  tl.startTimeNs, tl.timeRangeNs, tl.syncMode, rbWidth, rbStartPix, rbEndPix);
    ImGui::EndChild();

    // Background color is the one of the titles
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.153f, 0.157f, 0.13f, 1.0f));
    ImGui::BeginChild("timeline", ImVec2(0,0), false, ImGuiWindowFlags_NoScrollWithMouse);

    // Ensure cache is up to time with the data from record
    prepareTimeline(tl);

    // Init of the helper context
    TimelineDrawHelper ctx;
    ctx.main       = this;
    ctx.record     = _record;
    ctx.tl         = &tl;
    ctx.font       = ImGui::GetFont();
    ctx.winX       = ImGui::GetWindowPos().x;
    ctx.winY       = ImGui::GetWindowPos().y;
    ctx.winWidth   = ImGui::GetWindowContentRegionMax().x-vwConst::OVERVIEW_VBAR_WIDTH;
    ctx.winHeight  = ImGui::GetWindowSize().y;
    ctx.fontHeight        = ImGui::GetTextLineHeightWithSpacing();
    ctx.fontSpacing       = 0.5f*style.ItemSpacing.y;
    ctx.textPixMargin     = 2.f*ctx.fontSpacing;
    ctx.threadTitleHeight = getTimelineHeaderHeight(false, true);
    ctx.isWindowHovered   = ImGui::IsWindowHovered();
    ctx.nsToPix     = (double)ctx.winWidth/(double)tl.timeRangeNs;
    ctx.startTimeNs = tl.startTimeNs;
    ctx.timeRangeNs = tl.timeRangeNs;
    ctx.mouseX      = ImGui::GetMousePos().x;
    ctx.mouseY      = ImGui::GetMousePos().y;
    ctx.colorText   = vwConst::uWhite;
    ctx.colorTextH  = vwConst::uBlack;
    ctx.colorFillH  = vwConst::uWhite;
    ctx.colorGap    = vwConst::uLightGrey;

    // Get keyboard focus on window hovering
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && !_search.isInputPopupOpen && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGui::SetWindowFocus();
    }

    float scrollbarY = ImGui::GetScrollY();
    float yThread = ctx.winY-scrollbarY;
    plgData(TML, "Start time (s)", 0.000000001*tl.startTimeNs);
    plgData(TML, "Time range (s)", 0.000000001*tl.timeRangeNs);

    // Force scrolling to see a particular thread
    if(tl.viewThreadId>=0) {
        if(tl.viewThreadId==vwConst::LOCKS_THREADID) {
            int lockQty = tl.cachedLockUse.size();
            float y = (float)(tl.valuePerThread[tl.viewThreadId]-scrollbarY);
            // Only if the lock&resource section is not fully visible
            if(y+ctx.threadTitleHeight+lockQty*1.5f*ctx.fontHeight>ImGui::GetWindowHeight() || y<=ctx.winY) {
                ImGui::SetScrollY((float)tl.valuePerThread[tl.viewThreadId]);
            }
        } else {
            int nestingLevelQty = _record->threads[tl.viewThreadId].levels.size();
            float y = (float)(tl.valuePerThread[tl.viewThreadId]-scrollbarY);
            // Only if the thread is not fully visible
            if(y+ctx.threadTitleHeight+nestingLevelQty*ctx.fontHeight>ImGui::GetWindowHeight() || y<=ctx.winY) {
                ImGui::SetScrollY((float)tl.valuePerThread[tl.viewThreadId]);
            }
        }
        tl.viewThreadId = -1;
    }


    // Draw the timelines
    // ==================
    int  lastGroupNameIdx = -1;
    int  hoveredThreadId  = -1;
    bool isHeaderHovered  = false;
    struct VerticalBarData { int threadId; float yStart; };
    const bsVec<vwConfig::ThreadLayout>& layouts = getConfig().getLayout();
    VerticalBarData* vBarData = (VerticalBarData*)alloca(layouts.size()*sizeof(VerticalBarData));

    for(int layoutIdx=0; layoutIdx<layouts.size(); ++layoutIdx) {
        // Store the thread start Y
        const vwConfig::ThreadLayout& ti = layouts[layoutIdx];
        if(!ti.isVisible) continue;
        tl.valuePerThread[ti.threadId] = yThread-(ctx.winY-ImGui::GetScrollY());
        vBarData[layoutIdx] = { ti.threadId, (float)tl.valuePerThread[ti.threadId] };

        // Get expansion state
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        bool isGroupExpanded = ti.groupNameIdx<0 || getConfig().getGroupExpanded(ti.groupNameIdx);
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) continue; // Belong to a hidden group

        // Reserve the header space
        float yHeader = yThread;
        yThread    += getTimelineHeaderHeight(doDrawGroupHeader, isGroupExpanded);

        // Draw the timeline if it is expanded (visibility in window is done inside)
        if(isGroupExpanded && ti.isExpanded) {
            if     (ti.threadId< cmConst::MAX_THREAD_QTY)      ctx.drawScopes(yThread, ti.threadId);
            else if(ti.threadId==vwConst::LOCKS_THREADID)      ctx.drawLocks(yThread);
            else if(ti.threadId==vwConst::CORE_USAGE_THREADID) ctx.drawCoreTimeline(yThread);
        }
        yThread += getConfig().getTimelineVSpacing()*ctx.fontHeight;

        // Draw the group&thread headers afterwards (for transparency effects)
        bool isThreadHovered=false, isGroupHovered=false;
        if(displayTimelineHeader(yHeader, yThread, ti.threadId, doDrawGroupHeader, false, isThreadHovered, isGroupHovered)) {
            synchronizeThreadLayout();
        }
        isHeaderHovered = isHeaderHovered || isThreadHovered || isGroupHovered;

        // Open contextual menu
        if((isThreadHovered || isGroupHovered) && !tl.ctxDoOpenContextMenu && tl.dragMode==NONE && ImGui::IsMouseReleased(2)) {
            tl.ctxScopeLIdx          = PL_INVALID; // Scope-less
            tl.ctxDoOpenContextMenu = true;
        }
        // Start dragging
        if((isThreadHovered || isGroupHovered) && tl.ctxDraggedId<0 && tl.dragMode==NONE && io.KeyCtrl && ImGui::IsMouseDragging(0)) {
            tl.ctxDraggedId      = ti.threadId;
            tl.ctxDraggedIsGroup = isGroupHovered;
        }

        displayTimelineHeaderPopup(tl, ti.threadId, isGroupHovered);

        // Get the hovered thread
        if(hoveredThreadId<0 && ctx.mouseY<yThread) hoveredThreadId = ti.threadId;
    }
    if(hoveredThreadId<0 && ctx.isWindowHovered && !layouts.empty()) {
        hoveredThreadId = layouts.back().threadId;
    }

    // Thread dragging, to reorder them
    bool wasReordered = false;
    if(tl.ctxDraggedId>=0) {
        if(ImGui::IsMouseDragging(0)) {// Drag on-going: print preview
            bool isThreadHovered=false, isGroupHovered=false;
            displayTimelineHeader(ctx.mouseY, ctx.mouseY, tl.ctxDraggedId, tl.ctxDraggedIsGroup, true, isThreadHovered, isGroupHovered);
        }
        else { // End of drag: apply the change in group/thread order
            getConfig().moveDragThreadId(tl.ctxDraggedIsGroup, tl.ctxDraggedId, hoveredThreadId);
            tl.ctxDraggedId = -1; // Stop drag automata
            wasReordered = true;
            tl.isCacheDirty = true;
        }
    }

    // Draw the vertical overview bar
    if(!wasReordered) {
        float yEnd     = yThread-(ctx.winY-ImGui::GetScrollY());
        float vBarCoef = ctx.winHeight/bsMax(1.f, yEnd);
        for(int layoutIdx=0; layoutIdx<layouts.size(); ++layoutIdx) {
            if(!layouts[layoutIdx].isVisible) continue;
            bool isLast = (layoutIdx==layouts.size()-1);
            DRAWLIST->AddRectFilled(ImVec2(ctx.winX+ctx.winWidth, ctx.winY+vBarCoef*vBarData[layoutIdx].yStart),
                                    ImVec2(ctx.winX+ctx.winWidth+vwConst::OVERVIEW_VBAR_WIDTH, ctx.winY+vBarCoef*(isLast? yEnd : vBarData[layoutIdx+1].yStart)),
                                    ImColor(getConfig().getThreadColor(vBarData[layoutIdx].threadId)));
        }
        DRAWLIST->AddRectFilled(ImVec2(ctx.winX+ctx.winWidth, ctx.winY), ImVec2(ctx.winX+ctx.winWidth+4.f, ctx.winY+ctx.winHeight), vwConst::uGreyDark);
    }

    // Navigation
    // ==========
    bool hasKeyboardFocus = ctx.isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // On data scopes (real dragging)
    bool changedNavigation = false;
    if(tl.dragMode==DATA || (ctx.isWindowHovered && !isHeaderHovered && !io.KeyCtrl && tl.ctxDraggedId<0 && tl.dragMode!=BAR)) {
        if(ImGui::IsMouseDragging(2)) { // Data dragging
            if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1. || bsAbs(ImGui::GetMouseDragDelta(2).y)>1) {
                tl.setView(tl.getStartTimeNs()-(s64)(ImGui::GetMouseDragDelta(2).x/ctx.nsToPix), tl.getTimeRangeNs());
                ImGui::SetScrollY(ImGui::GetScrollY()-ImGui::GetMouseDragDelta(2).y);
                ImGui::ResetMouseDragDelta(2);
                tl.dragMode = DATA;
                changedNavigation = true;
            }
        }
        else tl.dragMode = NONE;
    }

    // Keys navigation
    if(hasKeyboardFocus) {
        if(!ImGui::GetIO().KeyCtrl) {
            if(ImGui::IsKeyPressed(KC_Up  ))  ImGui::SetScrollY(ImGui::GetScrollY()-0.25f*ctx.winHeight);
            if(ImGui::IsKeyPressed(KC_Down))  ImGui::SetScrollY(ImGui::GetScrollY()+0.25f*ctx.winHeight);
            if(ImGui::IsKeyPressed(KC_Left))  { tl.setView(tl.getStartTimeNs()-(s64)(0.25*tl.getTimeRangeNs()), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_Right)) { tl.setView(tl.getStartTimeNs()+(s64)(0.25*tl.getTimeRangeNs()), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_H)) openHelpTooltip(tl.uniqueId, "Help Timeline");
        }
        else { // Ctrl+up/down is handled by the mouse wheel code
            if(ImGui::IsKeyPressed(KC_Left))  { tl.setView(tl.getStartTimeNs()-tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_Right)) { tl.setView(tl.getStartTimeNs()+tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
        }
    }

    // Update the time of the mouse
    if(ctx.isWindowHovered) _mouseTimeNs = tl.startTimeNs + (s64)((ctx.mouseX-ctx.winX)/ctx.nsToPix);

    // Draw visor, handle middle button drag (range selection) and timeline top bar drag
    if(manageVisorAndRangeSelectionAndBarDrag(tl, ctx.isWindowHovered, ctx.mouseX, ctx.mouseY, ctx.winX, ctx.winY, ctx.winWidth, ctx.winHeight,
                                              isBarHovered, rbWidth, rbStartPix, rbEndPix)) {
        ctx.nsToPix = ctx.winWidth/tl.timeRangeNs;
        changedNavigation = true;
    }

    // Double click: range focus on an item (detected above at drawing time)
    if(ctx.forceRangeNs!=0.) {
        tl.setView(ctx.forceStartNs, ctx.forceRangeNs);
        ctx.nsToPix = ctx.winWidth/tl.timeRangeNs;
        changedNavigation = true;
    }

    // Wheel input
    constexpr float vScrollPixPerTick = 50.f;
    int deltaWheel = (int)io.MouseWheel; // Wheel or Ctrl+up/down keys control the zoom
    if(hasKeyboardFocus && ImGui::GetIO().KeyCtrl) {
        if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
        if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
    }
    if((ctx.isWindowHovered || isBarHovered) && deltaWheel!=0) {
        // Ctrl: (Horizontal) range zoom
        if(io.KeyCtrl) {
            deltaWheel *= getConfig().getHWheelInversion();
            s64 newTimeRangeNs = getUpdatedRange(deltaWheel, tl.getTimeRangeNs());
            tl.setView(tl.getStartTimeNs()+(s64)((ctx.mouseX-ctx.winX)/ctx.winWidth*(tl.getTimeRangeNs()-newTimeRangeNs)), newTimeRangeNs);
            ctx.nsToPix = ctx.winWidth/newTimeRangeNs;
            changedNavigation = true;
        }
        // No Ctrl: standard vertical scrolling
        else ImGui::SetScrollY(ImGui::GetScrollY()-deltaWheel*getConfig().getVWheelInversion()*vScrollPixPerTick);
    }

    // Full screen
    if(hasKeyboardFocus && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(tl.uniqueId);
    }

    // Mark the end of the scroll region
    tl.checkTimeBounds(recordDurationNs);
    ImGui::SetCursorPosY(yThread-ctx.winY+ImGui::GetScrollY());

    // Synchronize windows
    if(changedNavigation) {
        synchronizeNewRange(tl.syncMode, tl.getStartTimeNs(), tl.getTimeRangeNs());
    }

    // Help
    displayHelpTooltip(tl.uniqueId, "Help Timeline",
                       "##Timeline view\n"
                       "===\n"
                       "Global and comprehensive view of the chronological execution of the program.\n"
                       "Thread scopes, context switches and lock usage are represented simultaneously.\n"
                       "Detailed information is provided on hovering any scope.\n"
                       "\n"
                       "##Actions:\n"
                       "-#H key#| This help\n"
                       "-#F key#| Full screen view\n"
                       "-#Right mouse button dragging#| Move\n"
                       "-#Left/Right key#| Move horizontally\n"
                       "-#Ctrl-Left/Right key#| Move horizontally faster\n"
                       "-#Up/Down key#| Move vertically\n"
                       "-#Mouse wheel#| Move vertically\n"
                       "-#Middle button mouse dragging#| Measure/select a time range\n"
                       "-#Ctrl-Up/Down key#| Time zoom\n"
                       "-#Ctrl-Mouse wheel#| Time zoom\n"
                       "-#Left mouse click on scope#| Time synchronize views of the same group\n"
                       "-#Double left mouse click on scope#| Time and range synchronize views of the same group\n"
                       "-#Right mouse click on scope#| Open menu for plot/histogram/profiling\n"
                       "-#Right mouse click on thread bar#| New thread views, color configuration, expand/collapse threads\n"
                       "-#Ctrl-Left mouse button dragging on thread bar#| Move and reorder the thread/group \n"
                       "\n"
                       );

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
