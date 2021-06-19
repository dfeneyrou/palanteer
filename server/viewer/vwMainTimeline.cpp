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
#include "bsOs.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


// Debug configuration
#ifndef PL_GROUP_TML
#define PL_GROUP_TML 0
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

static constexpr double MIN_SCOPE_PIX = 3.;

struct SmallItem {
    bool   isInit   = false;
    bool   hasEvt   = false;
    u32    scopeLIdx = PL_INVALID;
    double startPix = -1.;
    double endPix   = -1.;
    double endPixExact = -1.; // endPix may be altered for visual reasons
    cmRecord::Evt evt;
    s64    evtDurationNs = 0;
};


struct TimelineDrawHelper {
    // Local state
    vwMain*   main;
    cmRecord* record;
    vwMain::Timeline* tl;
    ImFont*    font;
    double winX;
    double winY;
    double winWidth;
    double winHeight;
    double fontHeight;
    double fontSpacing;
    double textPixMargin;
    double threadTitleHeight;
    bool   isWindowHovered;
    double startTimeNs;
    double timeRangeNs;
    double nsToPix;
    double mouseX;
    double mouseY;

    ImU32 colorText;
    ImU32 colorTextH;
    ImU32 colorFillH;
    ImU32 colorFill1;
    ImU32 colorFill2;
    ImU32 colorFillS;
    ImU32 colorOutline;
    ImU32 colorGap;

    double forceRangeNs = 0.;
    double forceStartNs = 0.;

    // Functions
    void highlightGapIfHovered(double lastScopeEndTimeNs, double pixStartRect, double y);
    void displaySmallScope(const SmallItem& si, int level, int levelQty, double y, double lastScopeEndTimeNs);
    void displayScope(int threadId, int nestingLevel, u32 scopeLIdx, const cmRecord::Evt& evt,
                     double pixStartRect, double pixEndRect, double y, s64 durationNs, double lastScopeEndTimeNs, double yThread);
    void drawCoreTimeline(double& yThread);
    void drawLocks       (double& yThread);
    void drawScopes       (double& yThread, int threadId);
};


void
TimelineDrawHelper::highlightGapIfHovered(double lastScopeEndTimeNs, double pixStartRect, double y)
{
    double lastPixEndTime = nsToPix*(lastScopeEndTimeNs-startTimeNs);
    // Is previous gap hovered?
    if(isWindowHovered && lastScopeEndTimeNs!=0. && (mouseX-winX)>lastPixEndTime &&
       (mouseX-winX)<pixStartRect && mouseY>bsMax(y,winY+threadTitleHeight) && mouseY<bsMin(y+fontHeight,winY+winHeight)) {
        // Yes: Highlight the gap
        DRAWLIST->AddRectFilled(ImVec2(lastPixEndTime+winX, y), ImVec2(pixStartRect+winX, y+fontHeight), colorGap);
        DRAWLIST->AddRect      (ImVec2(lastPixEndTime+winX, y), ImVec2(pixStartRect+winX, y+fontHeight), colorOutline);

        // Add a tooltip
        s64 durationNs = (pixStartRect-lastPixEndTime)/nsToPix;
        ImGui::SetTooltip("Gap duration: %s", main->getNiceDuration(durationNs));

        // Double click adjusts the view to it
        if(ImGui::IsMouseDoubleClicked(0) && !tl->isAnimating()) {
            forceRangeNs = vwConst::DCLICK_RANGE_FACTOR*durationNs;
            forceStartNs = bsMax(0., startTimeNs+(lastScopeEndTimeNs-startTimeNs)/timeRangeNs*(timeRangeNs-forceRangeNs));
        }
    }
};


void
TimelineDrawHelper::displaySmallScope(const SmallItem& si, int level, int levelQty, double y, double lastScopeEndTimeNs)
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
                                const cmRecord::Evt& evt, double pixStartRect, double pixEndRect,
                                double y, s64 durationNs, double lastScopeEndTimeNs, double yThread)
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
            forceStartNs = bsMax(0., startTimeNs+(evt.vS64-startTimeNs)/timeRangeNs*(timeRangeNs-forceRangeNs));
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
    constexpr double minCharWidth = 8.;
    double pixTextStart = bsMax(0., pixStartRect);
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
TimelineDrawHelper::drawCoreTimeline(double& yThread)
{
    constexpr double coreNamePosX = 50.;
    constexpr double heightMargin = 2.;
    constexpr double minCharWidth = 8.;
    constexpr double coarseFactor = 0.08;
    char   tmpStr[128];
    double widthCoreXX = ImGui::CalcTextSize("CoreXX").x;

    // Skip the drawing if not visible
    plgScope (TML, "Display cores timeline");
    if(yThread>winY+ImGui::GetWindowHeight() || yThread+record->coreQty*fontHeight<=winY) {
        plgText(TML, "State", "Skipped because hidden");
        yThread += record->coreQty*fontHeight;
        return;
    }

    // Draw the filled CPU curve (step)
    vwMain::TlCachedCpuPoint prevPt = { -1., 0. };
    constexpr float thres0 = 0.2, thres1=0.33, thres2=0.66, thres3=0.8, alphaCpu=0.6;
    for(const vwMain::TlCachedCpuPoint& cl : tl->cachedCpuCurve) {
        float x1    = winX+prevPt.timePix, x2 = winX+bsMax(prevPt.timePix+1., cl.timePix);
        float prevY = yThread;
        float value = prevPt.cpuUsageRatio;
        ImU32 prevColor = ImColor(thres0, 0.f, 0.f, alphaCpu);
        ImU32 colorUp;

        // Draw the gradient curve
#define DRAW_LAYERED_CURVE(thresMin, thresMax, colorCode)               \
        if(value>thresMin) {                                            \
            float tValue = bsMin(value, thresMax), tY = yThread-tValue*fontHeight; \
            colorUp = colorCode;                                        \
            DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, prevY), ImVec2(x2, tY), prevColor, prevColor, colorUp, colorUp); \
            prevColor = colorUp;                                        \
            prevY     = tY;                                             \
        }
        DRAW_LAYERED_CURVE(0.,     thres1, ImColor(bsMax(tValue, thres0)/thres1, 0.f, 0.f, alphaCpu));             // black (0,0,0) -> red   (1,0,0)
        DRAW_LAYERED_CURVE(thres1, thres2, ImColor(1.0f, (tValue-thres1)/(thres2-thres1), 0.f,  alphaCpu));        // red   (1,0,0) -> yellow(1,1,0)
        DRAW_LAYERED_CURVE(thres2,    1.0, ImColor(1.0f, 1.0f, bsMin(1.f, (tValue-thres2)/(thres3-thres2)), 1.f)); // yellow(1,1,0) -> white (1,1,1)
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
            double x2 = winX+bsMax(cl.startTimePix+3., cl.endTimePix);
            bool  isHovered = (!cl.isCoarse && isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 &&
                               mouseY>yThread && mouseY<yThread+fontHeight);
            double cHeightMargin = heightMargin + (cl.isCoarse? coarseFactor*fontHeight : 0.);

            // Draw the box
            ImU32 color        = cl.isCoarse? vwConst::uGrey64 : vwConst::uGrey96;
            ImU32 colorOutline = cl.isCoarse? vwConst::uGrey48 : vwConst::uGrey64;
            if(cl.threadId!=cmConst::MAX_THREAD_QTY && !cl.isCoarse) {
                const float  dimO      = 0.5;
                const ImVec4 colorBase = main->getConfig().getThreadColor(cl.threadId);
                color        = ImColor(colorBase);
                colorOutline = ImColor(dimO*colorBase.x, dimO*colorBase.y, dimO*colorBase.z);
            }
            DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread+cHeightMargin), ImVec2(x2, yThread+fontHeight-cHeightMargin), color);
            DRAWLIST->AddRect      (ImVec2(winX+cl.startTimePix, yThread+cHeightMargin), ImVec2(x2, yThread+fontHeight-cHeightMargin), colorOutline);

            // Add the text
            double clWidth = cl.endTimePix-cl.startTimePix;
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
TimelineDrawHelper::drawLocks(double& yThread)
{
    constexpr double threadNamePosX = 50.;
    constexpr double minCharWidth   = 8.;
    constexpr float  dim2 = 0.8;
    plAssert(tl->cachedLockUse.size()<=record->locks.size());

    // Skip the drawing if not visible
    plgScope (TML, "Display locks timeline");
    double yThreadEnd = yThread; // Compute the end of the lock section (depends on content)
    for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
        bsVec<int>& waitingThreadIds = record->locks[lockIdx].waitingThreadIds;
        double threadBarHeight = bsMinMax(fontHeight/bsMax(1,waitingThreadIds.size()), 3., 0.5*fontHeight);
        yThreadEnd += waitingThreadIds.size()*threadBarHeight + 1.5*fontHeight;
    }
    if(yThread>winY+ImGui::GetWindowHeight() || yThreadEnd<=winY) {
        plgText(TML, "State", "Skipped because hidden");
        yThread = yThreadEnd;
        return;
    }

    double maxLockNameWidth = 1.;
    for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
        maxLockNameWidth = bsMax(maxLockNameWidth, ImGui::CalcTextSize(record->getString(record->locks[lockIdx].nameIdx).value.toChar()).x);
    }

    // Loop on locks
    for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
        const vwMain::TlCachedLockUse&  clu  = tl->cachedLockUse[lockIdx];
        bsVec<int>& waitingThreadIds = record->locks[lockIdx].waitingThreadIds;
        double      threadBarHeight  = bsMinMax(fontHeight/bsMax(1,waitingThreadIds.size()), 3., 0.5*fontHeight);
        const double yUsed = yThread+waitingThreadIds.size()*threadBarHeight;

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
                float thickness = bsMax(bsMin(threadBarHeight, cl.endTimePix-cl.startTimePix), 2.);
                double x2 = winX+bsMax(cl.startTimePix+2., cl.endTimePix-thickness);
                bool isHighlighted = main->isScopeHighlighted(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_WAIT|PL_FLAG_SCOPE_BEGIN, -1, cl.e.nameIdx);
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yBar), ImVec2(x2, yBar+threadBarHeight), isHighlighted? vwConst::uYellow : colorThread);
                // Draw the vertical-slightly-diagonal line toward the lock use scope
                DRAWLIST->AddQuadFilled(ImVec2(x2, yBar), ImVec2(x2, yBar+threadBarHeight-0.5), ImVec2(x2+thickness, yUsed), ImVec2(x2+0.5*thickness, yBar),
                                        isHighlighted? vwConst::uYellow : colorThread);

                // Hovered
                if(isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 && mouseY>=yBar && mouseY<=yBar+threadBarHeight) {
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
                    ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0));
                    ImGui::EndTooltip();
                }

                // Popup
                if(ImGui::BeginPopup("lock wait menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                    double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                    ImGui::TextColored(vwConst::grey, "<lock wait> [%s]", record->getString(cl.e.nameIdx).value.toChar());
                    // Plot & histogram
                    if(!main->_plotMenuItems.empty()) {
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
            double x2 = winX+bsMax(cl.startTimePix+2., cl.endTimePix);
            bool  isHovered = (!cl.isCoarse && isWindowHovered && mouseX>winX+cl.startTimePix && mouseX<x2 &&
                               mouseY>yThread && mouseY<yThread+fontHeight);
            bool isHighlighted = !cl.isCoarse &&  main->isScopeHighlighted(cl.e.threadId, cl.e.vS64, cl.e.vS64+cl.durationNs, PL_FLAG_TYPE_LOCK_ACQUIRED, -1, cl.e.nameIdx);

            // Draw the box
            ImU32 color        = cl.isCoarse? vwConst::uGrey64 : vwConst::uGrey96;
            ImU32 colorOutline = cl.isCoarse? vwConst::uGrey48 : vwConst::uGrey64;
            if(cl.e.threadId!=cmConst::MAX_THREAD_QTY && !cl.isCoarse) {
                constexpr float  dimO  = 0.5;
                const ImVec4 colorBase = main->getConfig().getThreadColor(cl.e.threadId);
                color        = ImColor(colorBase);
                colorOutline = ImColor(dimO*colorBase.x, dimO*colorBase.y, dimO*colorBase.z);
            }
            if(isHighlighted) color = vwConst::uWhite;
            DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread), ImVec2(x2, yThread+fontHeight), color);
            DRAWLIST->AddRect      (ImVec2(winX+cl.startTimePix, yThread), ImVec2(x2, yThread+fontHeight), colorOutline);

            // Draw the wait lock line if required (red line at the bottom)
            if(!isHighlighted && !cl.isCoarse && cl.overlappedThreadIds[0]!=0xFF) {
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread+fontHeight-2), ImVec2(x2, yThread+fontHeight), vwConst::uRed);
            }

            // Add the text
            double clWidth = cl.endTimePix-cl.startTimePix;
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
                    u64 itemHashPath = bsHashStepChain(record->threads[cl.e.threadId].threadHash , record->getString(cl.e.nameIdx).hash, cmConst::LOCK_USE_NAMEIDX); // Element lock notified for this thread and with this name
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
                ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0));
                ImGui::EndTooltip();
            }

            // Popup
            if(ImGui::BeginPopup("lock use menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                ImGui::TextColored(vwConst::grey, "<lock use> [%s]", record->getString(cl.e.nameIdx).value.toChar());
                // Plot & histogram
                if(!main->_plotMenuItems.empty()) {
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
        const double notifHalfWidthPix = 3;
        const double notifHeightPix = 0.6*fontHeight;
        const double yNtf = yThread+fontHeight;
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
                color = ImColor(colorBase.x, colorBase.y, colorBase.z, 0.7);
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
                ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(ntf.e.vS64, 0));
                ImGui::EndTooltip();
            }

            // Popup
            if(ImGui::BeginPopup("lock ntf menu", ImGuiWindowFlags_AlwaysAutoResize)) {
                double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                ImGui::TextColored(vwConst::grey, "<lock notified> [%s]", record->getString(ntf.e.nameIdx).value.toChar());
                // Plot & histogram
                if(!main->_plotMenuItems.empty()) {
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
        DRAWLIST->AddRectFilled(ImVec2(winX+threadNamePosX, yThread+2.), ImVec2(winX+threadNamePosX+maxLockNameWidth+2*textPixMargin, yThread+fontHeight-2.),
                                isLockNameHovered? IM_COL32(0, 0, 0, 32) : vwConst::uBlack);
        DRAWLIST->AddText(ImVec2(winX+50+textPixMargin, yThread+fontSpacing), isLockNameHovered? IM_COL32(255, 255, 255, 64) : vwConst::uWhite,
                          record->getString(record->locks[lockIdx].nameIdx).value.toChar());

        // Next lock
        yThread += 1.5*fontHeight;
    } // End of loop on used locks
}


void
TimelineDrawHelper::drawScopes(double& yThread, int tId)
{
    char tmpStr[128];
    constexpr double coreFontRatio   = 0.8;
    double widthCoreXX = font->CalcTextSizeA(coreFontRatio*ImGui::GetFontSize(), 1000., 0., "CoreX").x; // Display "Core%d" if enough space
    double widthCoreX  = font->CalcTextSizeA(coreFontRatio*ImGui::GetFontSize(), 1000., 0., "X").x;     // Second choice is displaying "%d", else nothing
    int nestingLevelQty = tl->cachedScopesPerThreadPerNLevel[tId].size();

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
        double firstTimeNs = bsMinMax(tw.firstTimeNs, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs);
        double lastTimeNs  = bsMinMax(tw.lastTimeNs,  tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs);
        if(firstTimeNs==lastTimeNs && (firstTimeNs==tl->startTimeNs || firstTimeNs==tl->startTimeNs+tl->timeRangeNs)) continue;
        const ImVec4  tmp = main->getConfig().getThreadColor(tw.threadId);
        const ImColor colorThread = ImColor(tmp.x, tmp.y, tmp.z, vwConst::TEXT_BG_FOOTPRINT_ALPHA);
        double x1 = winX+(firstTimeNs-tl->startTimeNs)*nsToPix;
        double x2 = bsMax(x1+2., winX+(lastTimeNs -tl->startTimeNs)*nsToPix);
        DRAWLIST->AddRectFilled(ImVec2(x1, yThread - threadTitleHeight+2.*fontSpacing),
                                ImVec2(x2, yThread + nestingLevelQty*fontHeight), colorThread);
    }

    // Draw the context switches
    const bsVec<vwMain::TlCachedSwitch>& cachedSwitches = tl->cachedSwitchPerThread[tId];
    const double switchHeight = 0.7*fontHeight;
    const double ySwitch = yThread-switchHeight;
    for(const vwMain::TlCachedSwitch& cs : cachedSwitches) {
        if(cs.coreId==PL_CSWITCH_CORE_NONE && !cs.isCoarse) continue;

        // Draw the box, with a start line (visually better to indicate the wake up)
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+bsMax(cs.startTimePix+2., cs.endTimePix), ySwitch+switchHeight), vwConst::uGrey64);
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+cs.startTimePix+1.5, ySwitch+switchHeight), vwConst::uGrey128, 2.);

        // Add the text
        if(!cs.isCoarse) {
            const double csWidth = cs.endTimePix-cs.startTimePix;
            const double scaledFontSize = coreFontRatio*ImGui::GetFontSize();
            if(csWidth>=widthCoreXX) {
                snprintf(tmpStr, sizeof(tmpStr), "Core%d", cs.coreId);
                DRAWLIST->AddText(font, scaledFontSize, ImVec2(winX+cs.startTimePix+0.5*(csWidth-widthCoreXX), ySwitch+0.5*(ImGui::GetFontSize()-scaledFontSize)), vwConst::uWhite, tmpStr);
            } else if(csWidth>=widthCoreX) {
                snprintf(tmpStr, sizeof(tmpStr), "%d", cs.coreId);
                DRAWLIST->AddText(font, scaledFontSize, ImVec2(winX+cs.startTimePix+0.5*(csWidth-widthCoreX), ySwitch+0.5*(ImGui::GetFontSize()-scaledFontSize)), vwConst::uWhite, tmpStr);
            }
        }

        // Tooltip
        if(isWindowHovered && !cs.isCoarse && mouseX>=winX+cs.startTimePix &&
           mouseX<=winX+bsMax(cs.startTimePix+1., cs.endTimePix) && mouseY>=ySwitch && mouseY<ySwitch+switchHeight) {
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
        double x2 = winX+bsMax(cl.startTimePix+2., cl.endTimePix);
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
            ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cl.e.vS64, 0));
            ImGui::EndTooltip();
        }
    }

    // Draw the markers
    const double yMarker = yThread-fontHeight;
    const double markerHalfWidthPix = 4;
    const double markerHeightPix = 0.3*fontHeight;
    const double markerThickness = 2.;
    const bsVec<ImVec4>& colors = main->getConfig().getColorPalette(true);
    float hlTimePix = -1.;
    for(const vwMain::TlCachedMarker& cm : tl->cachedMarkerPerThread[tId]) {
        ImGui::PushID(&cm);
        bool isHovered = (!cm.isCoarse && isWindowHovered && mouseX>=winX+cm.timePix-markerHalfWidthPix &&
                          mouseX<=winX+cm.timePix+markerHalfWidthPix && mouseY>=yMarker && mouseY<=yMarker+markerHeightPix);
        if(isHovered || main->isScopeHighlighted(cm.e.threadId, cm.e.vS64, cm.e.flags, -1, cm.e.nameIdx)) hlTimePix = cm.timePix;

        // Draw the triangles
        DRAWLIST->AddTriangleFilled(ImVec2(winX+cm.timePix-markerHalfWidthPix-markerThickness, yMarker-markerThickness),
                                    ImVec2(winX+cm.timePix+markerHalfWidthPix+markerThickness, yMarker-markerThickness),
                                    ImVec2(winX+cm.timePix, yMarker+markerHeightPix+markerThickness),
                                    (cm.isCoarse || cm.elemIdx<0)? vwConst::uGrey : ImU32(ImColor(main->getConfig().getCurveColor(cm.elemIdx))));
        DRAWLIST->AddTriangleFilled(ImVec2(winX+cm.timePix-markerHalfWidthPix, yMarker), ImVec2(winX+cm.timePix+markerHalfWidthPix, yMarker),
                                    ImVec2(winX+cm.timePix, yMarker+markerHeightPix),
                                    cm.isCoarse? vwConst::uGrey : ImU32(ImColor(colors[cm.e.filenameIdx%colors.size()])));

        // Hovered?
        if(isHovered) {
            main->setScopeHighlight(tId, cm.e.vS64, PL_FLAG_TYPE_MARKER, -1, cm.e.nameIdx);
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
                main->prepareGraphContextualMenu(cm.elemIdx, tl->getStartTimeNs(), tl->getTimeRangeNs(), false, false);
                ImGui::OpenPopup("marker menu");
            }
            // Tooltip
            ImGui::BeginTooltip();
            ImGui::TextColored(vwConst::gold, "[%s] %s", record->getString(cm.e.nameIdx).value.toChar(),
                               record->getString(cm.e.filenameIdx).value.toChar());
            ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(cm.e.vS64, 0));
            ImGui::EndTooltip();
        }

        // Popup
        if(ImGui::BeginPopup("marker menu", ImGuiWindowFlags_AlwaysAutoResize)) {
            double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
            ImGui::TextColored(vwConst::grey, "Marker [%s]", record->getString(cm.e.nameIdx).value.toChar());
            // Plot & histogram
            if(!main->_plotMenuItems.empty()) {
                ImGui::Separator();
                if(!main->displayPlotContextualMenu(tId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                ImGui::Separator();
                if(!main->displayHistoContextualMenu(headerWidth))             ImGui::CloseCurrentPopup();
            }
            // Marker window
            if(main->_markers.empty()) {
                ImGui::Separator();
                if(ImGui::Selectable("Add a marker window")) main->addMarker(main->getId(), cm.e.vS64);
            }

            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    // Highlight is overwritten after full display to avoid display order masking
    if(hlTimePix>=0) {
        DRAWLIST->AddTriangleFilled(ImVec2(winX+hlTimePix-markerHalfWidthPix, yMarker), ImVec2(winX+hlTimePix+markerHalfWidthPix, yMarker),
                                    ImVec2(winX+hlTimePix, yMarker+markerHeightPix), vwConst::uWhite);
    }

    const float dim2 = 0.8; // Alternate level
    const float dimS = 0.6; // Small
    const float dimO = 0.5; // Outline
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
        double lastScopeEndTimeNs = 0.;
        int y = yThread+nestingLevel*fontHeight;

        // Loop on scopes from the cached record
        const bsVec<vwMain::InfTlCachedScope>& cachedScopes = tl->cachedScopesPerThreadPerNLevel[tId][nestingLevel];
        for(const vwMain::InfTlCachedScope& b : cachedScopes) {
            if(b.startTimePix>winWidth) break;
            // Close previous small scope if hole is large enough or current item is not small
            if(si.isInit && (b.startTimePix-si.endPix>=MIN_SCOPE_PIX || (!b.isCoarseScope && b.endTimePix-b.startTimePix>=MIN_SCOPE_PIX))) {
                if(si.endPix-si.startPix<0.75*MIN_SCOPE_PIX) si.endPix = si.startPix+0.75*MIN_SCOPE_PIX; // Ensure a minimum displayed size
                // Display a scope
                if(si.hasEvt) displayScope(tId, nestingLevel, si.scopeLIdx, si.evt,
                                          si.startPix, si.endPix, y, si.evtDurationNs, lastScopeEndTimeNs, yThread); // One event only, so full display
                else          displaySmallScope(si, nestingLevel, nestingLevelQty, y, lastScopeEndTimeNs);   // Agglomerated events, so anonymous
                lastScopeEndTimeNs = si.endPixExact/nsToPix+tl->startTimeNs;
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
                displayScope(tId, nestingLevel, b.scopeLIdx, b.evt, b.startTimePix, b.endTimePix, y, b.durationNs, lastScopeEndTimeNs, yThread);
                lastScopeEndTimeNs = b.scopeEndTimeNs;
            }
        } // End of loop on scopes for this nesting level

        // Finish to draw the small items, if not completed
        if(si.isInit) {
            if(si.endPix-si.startPix<0.75*MIN_SCOPE_PIX) si.endPix = si.startPix+0.75*MIN_SCOPE_PIX; // Ensure a minimum displayed size
            if(si.hasEvt) displayScope(tId, nestingLevel, si.scopeLIdx, si.evt,
                                      si.startPix, si.endPix, y, si.evtDurationNs, lastScopeEndTimeNs, yThread); // One event only, so full display
            else          displaySmallScope(si, nestingLevel, nestingLevelQty, y, lastScopeEndTimeNs);   // Agglomerated events, so anonymous
            lastScopeEndTimeNs = si.endPixExact/nsToPix+tl->startTimeNs;
        }

        // And the gap at the end
        if(!cachedScopes.empty() && cachedScopes.back().startTimePix>winWidth) {
            highlightGapIfHovered(lastScopeEndTimeNs, cachedScopes.back().startTimePix, y);
        }
    } // End of loop on levels for each thread

    // Draw the Soft IRQs
    const bsVec<vwMain::TlCachedSoftIrq>& cachedSoftIrq = tl->cachedSoftIrqPerThread[tId];
    for(const vwMain::TlCachedSoftIrq& cs : cachedSoftIrq) {
        // Small line on top of the core representation to show the IRQ on the global scale (multi-res helps)
        DRAWLIST->AddRectFilled(ImVec2(winX+cs.startTimePix, ySwitch),
                                ImVec2(winX+bsMax(cs.startTimePix+2., cs.endTimePix), ySwitch+2.), vwConst::uLightGrey);
        // Dark shadow to show the frozen thread, if large enough
        if(!cs.isCoarse && cs.endTimePix-cs.startTimePix>2.) {
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
    if(main->isScopeHighlighted(tId, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs, PL_FLAG_TYPE_LOCK_ACQUIRED, -1, -1) ||
       main->isScopeHighlighted(-1, tl->startTimeNs, tl->startTimeNs+tl->timeRangeNs, PL_FLAG_TYPE_LOCK_WAIT| PL_FLAG_SCOPE_BEGIN, -1, -1)) {
        double startScopePix = (main->_hlStartTimeNs-tl->startTimeNs)*nsToPix;
        double endScopePix   = (main->_hlEndTimeNs  -tl->startTimeNs)*nsToPix;
        // Loop on locks
        for(int lockIdx=0; lockIdx<tl->cachedLockUse.size(); ++lockIdx) {
            if(record->locks[lockIdx].nameIdx!=main->_hlNameIdx) continue; // Not the hovered lock
            const vwMain::TlCachedLockUse&  clu  = tl->cachedLockUse[lockIdx];
            // Loop on lock scopes
            for(const vwMain::TlCachedLockScope& cl : clu.scopes) {
                if(cl.isCoarse || cl.e.threadId!=tId || cl.startTimePix>=endScopePix || cl.endTimePix<startScopePix) continue;
                DRAWLIST->AddRectFilled(ImVec2(winX+cl.startTimePix, yThread),
                                        ImVec2(winX+bsMax(cl.startTimePix+2., cl.endTimePix), yThread+nestingLevelQty*fontHeight),
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
        double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
        // Scope title
        ImGui::TextColored(vwConst::grey, "Scope '%s'", record->getString(tl->ctxScopeNameIdx).value.toChar());
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

void
vwMain::addTimeline(int id)
{
    if(!_record) return;
    _timelines.push_back( { } );
    auto& tl    = _timelines.back();
    tl.uniqueId = id;
    getSynchronizedRange(tl.syncMode, tl.startTimeNs, tl.timeRangeNs);
    memset(&tl.valuePerThread[0], 0, sizeof(tl.valuePerThread));
    setFullScreenView(-1);
    plMarker("user", "Add a timeline");
}


void
vwMain::prepareTimeline(Timeline& tl)
{
    // Worth working?
    const double winWidth = bsMax(1.f, ImGui::GetWindowContentRegionMax().x-vwConst::OVERVIEW_VBAR_WIDTH);
    if(!tl.isCacheDirty && tl.lastWinWidth==winWidth) return;
    tl.isCacheDirty = false;
    tl.lastWinWidth = winWidth;

    // Init
    plgScope(TML, "prepareTimeline");
    double nsToPix  = winWidth/tl.timeRangeNs;
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
    tl.cachedMarkerPerThread.clear();
    tl.cachedMarkerPerThread.resize(_record->threads.size());
    tl.cachedScopesPerThreadPerNLevel.clear();
    tl.cachedScopesPerThreadPerNLevel.resize(_record->threads.size());

    // Core usage
    // ==========
    plgBegin(TML, "Cores");

    // Get the CPU usage curve
    double cpuRatioCoef = 1./bsMax(1, _record->coreQty);
    cmRecordIteratorCpuCurve itcpu(_record, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
    s64 ptTimeNs; int usedCoreQty;
    while(itcpu.getNextPoint(ptTimeNs, usedCoreQty)) {
        tl.cachedCpuCurve.push_back( { nsToPix*(ptTimeNs-tl.startTimeNs), cpuRatioCoef*usedCoreQty } );
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
        double prevTimePix = -1.;

        cmRecordIteratorCoreUsage itcu(_record, coreId, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
        while(itcu.getNextSwitch(isCoarseScope, timeNs, endTimeNs, threadId, nameIdx)) {
            plgAssert(TML, nameIdx!=PL_INVALID || threadId<cmConst::MAX_THREAD_QTY, isCoarseScope, nameIdx, threadId);
            // Double event: just replace the "previous" data
            double timePix = nsToPix*((double)timeNs-tl.startTimeNs);
            if(!isCoarseScope && timeNs==prevTimeNs) {
                prevTimeNs   = timeNs;
                prevTimePix  = timePix;
                prevThreadId = threadId;
                prevNameIdx  = nameIdx;
                continue;
            }
            if(isCoarseScope) {
                double endTimePix = bsMin(nsToPix*((double)endTimeNs-tl.startTimeNs), winWidth);
                cachedCore.push_back( { true, 0xFFFF, PL_INVALID, bsMax(0., (prevNameIdx==0xFFFFFFFE)? timePix : prevTimePix), endTimePix, 0 } );
            }
            else if(prevTimeNs>=0 && timePix>=0 && prevNameIdx!=0xFFFFFFFE) {
                cachedCore.push_back( { false, (u16)prevThreadId, prevNameIdx, bsMax(0., prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs } );
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
        double prevTimePix = -1., endTimePix = -1.;
        cmRecord::Evt prevE, e; prevE.nameIdx = PL_INVALID;
        TlCachedLockUse& cachedLockUse = tl.cachedLockUse[lockIdx];
        cachedLockUse.scopes.clear();
        cachedLockUse.scopes.reserve(128);
        int waitingThreadQty = _record->locks[lockIdx].waitingThreadIds.size();
        cachedLockUse.waitingThreadScopes.resize(waitingThreadQty);
        for(int i=0; i<waitingThreadQty; ++i) cachedLockUse.waitingThreadScopes[i].clear();

        cmRecordIteratorLockUse itLockUse(_record, _record->locks[lockIdx].nameIdx, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
        while(itLockUse.getNextLock(isCoarseScope, timeNs, endTimeNs, e)) {
            double timePix = nsToPix*((double)timeNs-tl.startTimeNs);
            if(isCoarseScope) {
                endTimePix = nsToPix*((double)endTimeNs-tl.startTimeNs);
                cachedLockUse.scopes.push_back( { true, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                        bsMax(0., (prevE.flags==PL_FLAG_TYPE_LOCK_RELEASED)? timePix : prevTimePix), bsMin(endTimePix, winWidth), 0 } );
            }
            if(prevTimeNs>=0 && timePix>=0 && e.flags==PL_FLAG_TYPE_LOCK_RELEASED) {
                cachedLockUse.scopes.push_back( { prevIsCoarse, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                        bsMax(0., prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs, prevE } );
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
        cmRecordIteratorLockNtf itLockNtf(_record, _record->locks[lockIdx].nameIdx, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);

        while(itLockNtf.getNextLock(isCoarseScope, e)) {
            double timePix = nsToPix*((double)e.vS64-tl.startTimeNs);
            cachedLockNtf.push_back({ isCoarseScope, timePix, e });
            if(timePix>winWidth) break;
        } // End of loop on lock notification events

    } // End of caching the used locks
    plgEnd(TML, "Used locks");

    // Loop on threads
    // ===============
    int* idxPerUsedLock = (int*) alloca(_record->locks.size()*sizeof(int));
    for(int tId=0; tId<_record->threads.size(); ++tId) {
        plgScope (TML, "Thread scopes");
        plgVar(TML, tId);
        const cmRecord::Thread& rt = _record->threads[tId];
        bool isExpanded = getConfig().getGroupAndThreadExpanded(tId);

        // Cache the context switches
        bsVec<TlCachedSwitch>& cachedSwitches = tl.cachedSwitchPerThread[tId];
        cachedSwitches.clear();
        if(isExpanded) {
            plgScope(TML, "Ctx switches");
            cachedSwitches.reserve(256);
            bool   isCoarseScope = false, prevIsCoarse = false;
            s64    timeNs = 0, prevTimeNs = -1, endTimeNs = 0;
            int    coreId = 0, prevCoreId = -1;
            double prevTimePix = -1., endTimePix = -1.;
            cmRecordIteratorCtxSwitch itcs(_record, tId, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            while(itcs.getNextSwitch(isCoarseScope, timeNs, endTimeNs, coreId)) {
                double timePix = nsToPix*((double)timeNs-tl.startTimeNs);
                if(isCoarseScope) {
                    endTimePix = nsToPix*((double)endTimeNs-tl.startTimeNs);
                    cachedSwitches.push_back( { true, 0, bsMax(0., (prevCoreId==PL_CSWITCH_CORE_NONE)? timePix : prevTimePix),
                            bsMin(endTimePix, winWidth), 0 } );
                }
                if(prevTimeNs>=0 && timePix>=0) {
                    cachedSwitches.push_back( { prevIsCoarse, (u16)((s16)prevCoreId), bsMax(0., prevTimePix),
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
            double prevTimePix = -1., endTimePix = -1.;
            cmRecordIteratorSoftIrq itSoftIrq(_record, tId, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            while(itSoftIrq.getNextSwitch(isCoarseScope, timeNs, endTimeNs, nameIdx)) {
                double timePix = nsToPix*((double)timeNs-tl.startTimeNs);
                if(isCoarseScope) {
                    endTimePix = nsToPix*((double)endTimeNs-tl.startTimeNs);
                    cachedSoftIrq.push_back( { true, 0, bsMax(0., prevNameIdx!=0xFFFFFFFF? prevTimePix : timePix), bsMin(endTimePix, winWidth), 0 } );
                    nameIdx = 0xFFFFFFFF;
                }
                else if(prevTimeNs>=0 && timePix>=0 && prevNameIdx!=0xFFFFFFFF) {
                    cachedSoftIrq.push_back( { prevIsCoarse, prevNameIdx, bsMax(0., prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs } );
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
            double prevTimePix = -1., endTimePix = -1.;
            cmRecord::Evt prevE, e; prevE.flags = 0; prevE.nameIdx = 0xFFFFFFFF;
            if(!_record->locks.empty()) memset(&idxPerUsedLock[0], 0, _record->locks.size()*sizeof(int));
            cmRecordIteratorLockWait itLockWait(_record,  tId, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            s64 WAIT_LOCK_LIMIT_NS = 1000*getConfig().getLockLatencyUs();

            while(itLockWait.getNextLock(isCoarseScope, timeNs, endTimeNs, e)) { // @#BUG Probably last event (coarse at least) is not stored as a scope.
                double timePix = nsToPix*((double)timeNs-tl.startTimeNs);
                bool  prevIsBegin = (prevE.flags&PL_FLAG_SCOPE_BEGIN);
                if(isCoarseScope) {
                    endTimePix = nsToPix*((double)endTimeNs-tl.startTimeNs);
                    cachedLockWaits.push_back( { true, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                            bsMax(0., prevIsBegin? prevTimePix : timePix), bsMin(endTimePix, winWidth), 0 } );
                }
                if(prevTimeNs>=0 && timePix>=0) {
                    cachedLockWaits.push_back( { prevIsCoarse, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                            bsMax(0., prevTimePix), bsMin(timePix, winWidth), timeNs-prevTimeNs, prevE } );
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
                                for(int i=0; i<vwConst::MAX_OVERLAPPED_THREAD; ++i) if(useScopes[ulIdx].overlappedThreadIds[i]==0xFF) { useScopes[ulIdx].overlappedThreadIds[i] = eThreadId; break; }
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

        // Cache the markers
        bsVec<TlCachedMarker>& cachedMarker = tl.cachedMarkerPerThread[tId];
        cachedMarker.clear();
        if(isExpanded) {
            plgScope(TML, "Markers");
            cachedMarker.reserve(128);
            bool isCoarseScope = false;
            cmRecord::Evt e;
            cmRecordIteratorMarker itMarker(_record, tId, -1, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            while(itMarker.getNextMarker(isCoarseScope, e)) {
                int*   elemIdx = _record->elemPathToId.find(bsHashStepChain(_record->threads[tId].threadHash, e.nameIdx, cmConst::MARKER_NAMEIDX), cmConst::MARKER_NAMEIDX);
                double timePix = nsToPix*((double)e.vS64-tl.startTimeNs);
                cachedMarker.push_back( { isCoarseScope, elemIdx? *elemIdx : -1, timePix, e } );
                if(timePix>winWidth) break;
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
            cmRecordIteratorScope it(_record, tId, nestingLevel, (s64)tl.startTimeNs, MIN_SCOPE_PIX/nsToPix);
            double startTimePix = 0.;
            double endTimePix   = 0.;
            double lastScopeEndTimeNs = 0.; // Just for sanity
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
                    startTimePix = nsToPix*((double)scopeStartTimeNs-tl.startTimeNs);
                    endTimePix   = nsToPix*((double)scopeEndTimeNs  -tl.startTimeNs);
                }
                else { // Case full resolution
                    scopeStartTimeNs = evt.vS64;
                    plgData(TML, "Event start time (s)", 0.000000001*scopeStartTimeNs);
                    plgData(TML, "Event duration   (s)", 0.000000001*durationNs);
                    scopeEndTimeNs = scopeStartTimeNs+durationNs;
                    startTimePix  = nsToPix*((double)scopeStartTimeNs-tl.startTimeNs);
                    endTimePix    = nsToPix*((double)scopeEndTimeNs  -tl.startTimeNs);
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
    const double recordDurationNs = _record->durationNs;
    Timeline& tl = _timelines[tlWindowIdx];

    // Handle animation (smooth move, boundaries, and live record view behavior)
    tl.updateAnimation();
    tl.checkTimeBounds(recordDurationNs);

    // Ruler and visible range bar
    double rbWidth, rbStartPix, rbEndPix;
    double rulerHeight = getTimelineHeaderHeight(false, true);
    ImGui::BeginChild("ruler", ImVec2(0, 2.0*ImGui::GetStyle().WindowPadding.y+rulerHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
    const bool isBarHovered  = ImGui::IsWindowHovered();
    drawTimeRuler(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowContentRegionMax().x, rulerHeight,
                  tl.startTimeNs, tl.timeRangeNs, tl.syncMode, rbWidth, rbStartPix, rbEndPix);
    ImGui::EndChild();

    // Background color is the one of the titles
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.153, 0.157, 0.13, 1.0));
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
    ctx.fontSpacing       = 0.5*style.ItemSpacing.y;
    ctx.textPixMargin     = 2.*ctx.fontSpacing;
    ctx.threadTitleHeight = getTimelineHeaderHeight(false, true);
    ctx.isWindowHovered   = ImGui::IsWindowHovered();
    ctx.nsToPix     = ctx.winWidth/tl.timeRangeNs;
    ctx.startTimeNs = tl.startTimeNs;
    ctx.timeRangeNs = tl.timeRangeNs;
    ctx.mouseX      = ImGui::GetMousePos().x;
    ctx.mouseY      = ImGui::GetMousePos().y;
    ctx.colorText   = vwConst::uWhite;
    ctx.colorTextH  = vwConst::uBlack;
    ctx.colorFillH  = vwConst::uWhite;
    ctx.colorGap    = vwConst::uLightGrey;

    double scrollbarY = ImGui::GetScrollY();
    double yThread = ctx.winY-scrollbarY;
    plgData(TML, "Start time (s)", 0.000000001*tl.startTimeNs);
    plgData(TML, "Time range (s)", 0.000000001*tl.timeRangeNs);

    // Force scrolling to see a particular thread
    if(tl.viewThreadId>=0) {
        int nestingLevelQty = _record->threads[tl.viewThreadId].levels.size();
        double y = tl.valuePerThread[tl.viewThreadId]-scrollbarY;
        // Only if the thread is not fully visible
        if(y+ctx.threadTitleHeight+nestingLevelQty*ctx.fontHeight>ImGui::GetWindowHeight() || y<=ctx.winY) {
            ImGui::SetScrollY(tl.valuePerThread[tl.viewThreadId]);
        }
        tl.viewThreadId = -1;
    }


    // Draw the timelines
    // ==================
    int  lastGroupNameIdx = -1;
    int  hoveredThreadId  = -1;
    bool isHeaderHovered  = false;
    struct VerticalBarData { int threadId; double yStart; };
    VerticalBarData* vBarData = (VerticalBarData*)alloca(getConfig().getLayout().size()*sizeof(VerticalBarData));

    for(int layoutIdx=0; layoutIdx<getConfig().getLayout().size(); ++layoutIdx) {
        // Store the thread start Y
        const vwConfig::ThreadLayout& ti = getConfig().getLayout()[layoutIdx];
        tl.valuePerThread[ti.threadId] = yThread-(ctx.winY-ImGui::GetScrollY());
        vBarData[layoutIdx] = { ti.threadId, tl.valuePerThread[ti.threadId] };

        // Get expansion state
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        bool isGroupExpanded = ti.groupNameIdx<0 || getConfig().getGroupExpanded(ti.groupNameIdx);
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) continue; // Belong to a hidden group

        // Reserve the header space
        int yHeader = yThread;
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
    if(hoveredThreadId<0 && ctx.isWindowHovered && !getConfig().getLayout().empty()) {
        hoveredThreadId = getConfig().getLayout().back().threadId;
    }

    // Thread dragging, to reorder them
    if(tl.ctxDraggedId>=0) {
        if(ImGui::IsMouseDragging(0)) {// Drag on-going: print preview
            bool isThreadHovered=false, isGroupHovered=false;
            displayTimelineHeader(ctx.mouseY, ctx.mouseY, tl.ctxDraggedId, tl.ctxDraggedIsGroup, true, isThreadHovered, isGroupHovered);
        }
        else { // End of drag: apply the change in group/thread order
            getConfig().moveDragThreadId(tl.ctxDraggedIsGroup, tl.ctxDraggedId, hoveredThreadId);
            tl.ctxDraggedId = -1; // Stop drag automata
        }
    }

    // Draw the vertical overview bar
    double yEnd     = yThread-(ctx.winY-ImGui::GetScrollY());
    double vBarCoef = ctx.winHeight/bsMax(1., yEnd);
    for(int layoutIdx=0; layoutIdx<getConfig().getLayout().size(); ++layoutIdx) {
        bool isLast = (layoutIdx==getConfig().getLayout().size()-1);
        DRAWLIST->AddRectFilled(ImVec2(ctx.winX+ctx.winWidth, ctx.winY+vBarCoef*vBarData[layoutIdx].yStart),
                                ImVec2(ctx.winX+ctx.winWidth+vwConst::OVERVIEW_VBAR_WIDTH, ctx.winY+vBarCoef*(isLast? yEnd : vBarData[layoutIdx+1].yStart)),
                                ImColor(getConfig().getThreadColor(vBarData[layoutIdx].threadId)));
    }
    DRAWLIST->AddRectFilled(ImVec2(ctx.winX+ctx.winWidth, ctx.winY), ImVec2(ctx.winX+ctx.winWidth+4., ctx.winY+ctx.winHeight), vwConst::uGreyDark);


    // Navigation
    // ==========
    bool hasKeyboardFocus = ctx.isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // On data scopes (real dragging)
    bool changedNavigation = false;
    if(tl.dragMode==DATA || (ctx.isWindowHovered && !isHeaderHovered && !io.KeyCtrl && tl.ctxDraggedId<0 && tl.dragMode!=BAR)) {
        if(ImGui::IsMouseDragging(2)) { // Data dragging
            if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1. || bsAbs(ImGui::GetMouseDragDelta(2).y)>1) {
                tl.setView(tl.getStartTimeNs()-ImGui::GetMouseDragDelta(2).x/ctx.nsToPix, tl.getTimeRangeNs());
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
            if(ImGui::IsKeyPressed(KC_Up  ))  ImGui::SetScrollY(ImGui::GetScrollY()-0.25*ctx.winHeight);
            if(ImGui::IsKeyPressed(KC_Down))  ImGui::SetScrollY(ImGui::GetScrollY()+0.25*ctx.winHeight);
            if(ImGui::IsKeyPressed(KC_Left))  { tl.setView(tl.getStartTimeNs()-0.25*tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_Right)) { tl.setView(tl.getStartTimeNs()+0.25*tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_H)) openHelpTooltip(tl.uniqueId, "Help Timeline");
        }
        else { // Ctrl+up/down is handled by the mouse wheel code
            if(ImGui::IsKeyPressed(KC_Left))  { tl.setView(tl.getStartTimeNs()-1.0*tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
            if(ImGui::IsKeyPressed(KC_Right)) { tl.setView(tl.getStartTimeNs()+1.0*tl.getTimeRangeNs(), tl.getTimeRangeNs()); changedNavigation = true; }
        }
    }

    // Update the time of the mouse
    if(ctx.isWindowHovered) _mouseTimeNs = tl.startTimeNs + (ctx.mouseX-ctx.winX)/ctx.nsToPix;

    // Draw visor, handle middle button drag (range selection) and timeline top bar drag
    if(manageVisorAndRangeSelectionAndBarDrag(tl, ctx.isWindowHovered, ctx.mouseX, ctx.mouseY, ctx.winX, ctx.winY, ctx.winWidth, ctx.winHeight,
                                              isBarHovered, rbWidth, rbStartPix, rbEndPix)) {
        ctx.nsToPix = ctx.winWidth/tl.timeRangeNs;
        changedNavigation = true;
    }

    // Double click: range focus on an item (detected above at drawing time)
    if(ctx.forceRangeNs!=0.) {
        tl.setView(ctx.forceStartNs, bsMax(ctx.forceRangeNs, 1000.));
        ctx.nsToPix = ctx.winWidth/tl.timeRangeNs;
        changedNavigation = true;
    }

    // Wheel input
    constexpr double vScrollPixPerTick = 50.;
    int deltaWheel = (int)io.MouseWheel; // Wheel or Ctrl+up/down keys control the zoom
    if(hasKeyboardFocus && ImGui::GetIO().KeyCtrl) {
        if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
        if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
    }
    if((ctx.isWindowHovered || isBarHovered) && deltaWheel!=0) {
        // Ctrl: (Horizontal) range zoom
        if(io.KeyCtrl) {
            deltaWheel *= getConfig().getHWheelInversion();
            double newTimeRangeNs = getUpdatedRange(deltaWheel, tl.getTimeRangeNs());
            tl.setView(tl.getStartTimeNs()+(ctx.mouseX-ctx.winX)/ctx.winWidth*(tl.getTimeRangeNs()-newTimeRangeNs), newTimeRangeNs);
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
