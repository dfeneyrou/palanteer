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

// This file implements the memory timeline view

// System
#include <math.h>
#include <algorithm>
#include <inttypes.h>

// Internal
#include "cmRecord.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwReplayAlloc.h"
#include "vwConfig.h"


#ifndef PL_GROUP_MEM
#define PL_GROUP_MEM 0
#endif


// @#TODO    [MEMORY] P90 Have a way to get some statistics on any time range visible window (summed alloc & dealloc qty at least, and same for byte flow)
// @#FEATURE [MEMORY] P90 Add an histogram of the memory with logarithmic scale of allocation size + grouped per big locations (with selected threads from config)
// @#FEATURE [MEMORY] In the text table, have a way to agglomerate per type (profile table does not give these stats on all present alloc). Default?
//                    There shall be a checkbox. And also the syncMode selector for the table view (which seems another complete kind of view, not a satellite viewlet from memory timeline)


constexpr double CALL_BIN_PIX      = 10.;
constexpr int    CALL_BIN_MARGIN   = 2;
constexpr double BLOCK_MIN_ROW_PIX = 4.;
constexpr int    SMALL_BLOCK_PATTERN_WIDTH = 20;


bsString
vwMain::MemoryTimeline::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "memtimeline %d", syncMode);
    return tmpStr;
}


// Helpers
// =======

struct MemoryDrawHelper {
    vwMain*   main;
    cmRecord* record;
    double winX;
    double winY;
    double winWidth;
    double winHeight;
    double fontHeight;
    double fontHeightNoSpacing;
    double fontSpacing;
    double callBarHeight;
    double fullHeaderHeight;
    double threadTitleMargin;
    double drawableHeight;
    bool   isWindowHovered;
    double mouseX;
    double mouseY;
    // Draw helpers
    u32  drawMemoryCurves    (vwMain::MemoryTimeline& mw);
    void drawDetailedBlocks  (vwMain::MemoryTimeline& mw);
    void drawAllocCallTopBars(vwMain::MemoryTimeline& mw);
    // Conversion helpers
    double viewByteMaxLimit;
    double yFactor;
    void computeLayout     (vwMain::MemoryTimeline& mw);
    void getIvalueFromValue(vwMain::MemoryTimeline& mw, double value, int& threadId, double& threadValue);
    void getValueFromIValue(vwMain::MemoryTimeline& mw, int threadId, double threadValue, double yFactor,
                            double& newValueUnderMouse, double& newValueMaxLimit);
};


// "IValue" is "view Independent Calue", which means a tuple (threadId, byte position)
// Its need is due to the varying pixel (of headers) scale versus the byte range
void
MemoryDrawHelper::getIvalueFromValue(vwMain::MemoryTimeline& mw, double value, int& threadId, double& threadValue)
{
    threadId              = 0;
    threadValue           = value;
    int lastGroupNameIdx  = -1;
    float vSpacing = main->getConfig().getTimelineVSpacing()*fontHeight;
    for(const vwConfig::ThreadLayout& ti : main->getConfig().getLayout()) {
        if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue;                   // Skip "special threads" in the layout (cores, locks etc...)
        if(mw.cachedThreadData[ti.threadId].maxAllocSizeValue==0) continue;  // Ignore threads without memory information
        threadId = ti.threadId;
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        bool isGroupExpanded   = ti.groupNameIdx<0 || main->getConfig().getGroupExpanded(ti.groupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) continue; // Belong to a hidden group

        // Update the quantities
        double newValue = threadValue - (main->getTimelineHeaderHeight(doDrawGroupHeader, isGroupExpanded)+threadTitleMargin)/yFactor;
        if(newValue<0) { threadValue = 0; break; }
        threadValue = newValue;
        if(isGroupExpanded && ti.isExpanded) newValue  -= mw.cachedThreadData[ti.threadId].maxAllocSizeValue + vSpacing/yFactor;
        if(newValue<0) break;
        threadValue = newValue;
    }
}


void
MemoryDrawHelper::getValueFromIValue(vwMain::MemoryTimeline& mw, int threadId, double threadValue, double yFactorExt,
                                     double& newValueUnderMouse, double& newValueMaxLimit)
{
    newValueUnderMouse   = newValueMaxLimit = 0.;
    int lastGroupNameIdx = -1;
    float vSpacing       = main->getConfig().getTimelineVSpacing()*fontHeight;
    for(const vwConfig::ThreadLayout& ti : main->getConfig().getLayout()) {
        if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue;
        if(mw.cachedThreadData[ti.threadId].maxAllocSizeValue==0) continue;  // Ignore threads without memory information
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        bool isGroupExpanded   = ti.groupNameIdx<0 || main->getConfig().getGroupExpanded(ti.groupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) continue; // Belong to a hidden group

        // Update the quantities
        newValueMaxLimit += (main->getTimelineHeaderHeight(doDrawGroupHeader, isGroupExpanded)+threadTitleMargin)/yFactorExt;
        if(ti.threadId==threadId) newValueUnderMouse = newValueMaxLimit+threadValue; // Snapshot the thread base value
        if(isGroupExpanded && ti.isExpanded) newValueMaxLimit += mw.cachedThreadData[ti.threadId].maxAllocSizeValue + vSpacing/yFactorExt;
    }
}


void
MemoryDrawHelper::computeLayout(vwMain::MemoryTimeline& mw)
{
    // Compute the total height of group&thread headers
    int    lastGroupNameIdx  = -1;
    double totalHeaderHeight = 0., totalBytes = 0.;
    float  vSpacing = main->getConfig().getTimelineVSpacing()*fontHeight;
    for(const vwConfig::ThreadLayout& ti : main->getConfig().getLayout()) {
        // Get expansion state
        if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue;                  // Skip "special threads" in the layout (cores, locks etc...)
        if(mw.cachedThreadData[ti.threadId].maxAllocSizeValue==0) continue; // Ignore threads without memory information
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        bool isGroupExpanded   = ti.groupNameIdx<0 || main->getConfig().getGroupExpanded(ti.groupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) continue; // Belong to a hidden group

        // Update the quantities
        totalHeaderHeight += main->getTimelineHeaderHeight(doDrawGroupHeader, isGroupExpanded) + threadTitleMargin;
        if(isGroupExpanded && ti.isExpanded) {
            totalBytes        += mw.cachedThreadData[ti.threadId].maxAllocSizeValue;
            totalHeaderHeight += vSpacing;
        }
    }
    if(totalBytes==0.) totalBytes = 1.; // Prevent division by zero

    // Stabilize the min/max values
    double c1         = totalHeaderHeight/drawableHeight;
    double rangeLimit = totalBytes*drawableHeight;
    viewByteMaxLimit  = totalBytes + c1*bsMinMax(mw.viewByteMax-mw.viewByteMin, 1., rangeLimit);
    if(mw.viewByteMin>=mw.viewByteMax) {
        mw.viewByteMin = 0.; mw.viewByteMax = viewByteMaxLimit;
    }
    if(mw.viewByteMin<0.) {
        mw.viewByteMin = 0.;
    }
    viewByteMaxLimit = totalBytes + c1*bsMinMax(mw.viewByteMax-mw.viewByteMin, 1., rangeLimit);

    if(mw.isPreviousRangeEmpty) { // Reset the range in this case, else it would be very small
        mw.viewByteMin = 0;;
        mw.viewByteMax = viewByteMaxLimit;
    }
    if(mw.viewByteMax>1.05*viewByteMaxLimit) {  // Slight overshoot
        mw.viewByteMin = bsMax(mw.viewByteMin+viewByteMaxLimit-mw.viewByteMax, 0);
        mw.viewByteMax = viewByteMaxLimit = totalBytes + c1*bsMinMax(mw.viewByteMax-mw.viewByteMin, 1., rangeLimit);
    }
    if(mw.viewByteMax-mw.viewByteMin>rangeLimit || mw.viewByteMin>=mw.viewByteMax) {
        mw.viewByteMin = bsMax(mw.viewByteMax-rangeLimit, 0.);
    }

    // Finalize
    mw.viewByteMin = bsMax(mw.viewByteMin, 0.);
    mw.viewByteMax = bsMin(mw.viewByteMax, viewByteMaxLimit);
    yFactor = drawableHeight/(mw.viewByteMax-mw.viewByteMin);
    mw.isPreviousRangeEmpty = (totalBytes==1);
}


u32
MemoryDrawHelper::drawMemoryCurves(vwMain::MemoryTimeline& mw)
{
    plgScope(MEM, "drawMemoryCurves");

    vwConfig& cfg = main->getConfig();
    const double pointSize      = 3.;
    const double xFactor        = winWidth/bsMax(1., mw.timeRangeNs);
    const double mouseTimeToPix = winX+(main->_mouseTimeNs-mw.startTimeNs)*xFactor;
    struct ClosePoint {
        vwMain::MemCachedPoint point;
        double distanceX  = 1e300;
        double deltaValue = 0.;
        double x, y;
    };
    ClosePoint closestPoint    = { { 0, 0., 0, 0, PL_INVALID, PL_INVALID } };
    int        closestPointTId = -1;
    double baseValue = 0.;
    char   tmpStr[128];
    u32    totalUsedBytes = 0;
    bool   areMemoryDetailsComputed = false;

    // Loop on the layout
    int  lastGroupNameIdx = -1;
    int  hoveredThreadId  = -1;
    bool isHeaderHovered  = false;
    struct VerticalBarData { int threadId; double viewByteStart; };
    VerticalBarData* vBarData = (VerticalBarData*)alloca(cfg.getLayout().size()*sizeof(VerticalBarData));
    int   vBarUsedLayoutQty = 0;
    float vSpacing = main->getConfig().getTimelineVSpacing()*fontHeight;
    for(int layoutIdx=0; layoutIdx<cfg.getLayout().size(); ++layoutIdx) {
        const vwConfig::ThreadLayout& ti = cfg.getLayout()[layoutIdx];
        if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue; // Skip "special threads" in the layout (cores, locks etc...)
        int tId = ti.threadId;
        const vwMain::MemCachedThread& mct = mw.cachedThreadData[tId];
        if(mct.maxAllocSizeValue==0) continue; // Ignore threads without memory information
        vBarData[vBarUsedLayoutQty++] = { tId, baseValue };

        // Get expansion state
        bool doDrawGroupHeader = (ti.groupNameIdx>=0 && ti.groupNameIdx!=lastGroupNameIdx);
        bool isGroupExpanded   = ti.groupNameIdx<0 || cfg.getGroupExpanded(ti.groupNameIdx);
        lastGroupNameIdx       = ti.groupNameIdx;
        if(ti.groupNameIdx>=0 && !doDrawGroupHeader && !isGroupExpanded) {
            mw.valuePerThread[tId] = baseValue;
            continue; // Belong to a hidden group
        }

        // Reserve the header space
        double headerBaseValue = baseValue;
        baseValue += (main->getTimelineHeaderHeight(doDrawGroupHeader, isGroupExpanded)+threadTitleMargin)/yFactor;
        mw.valuePerThread[tId] = baseValue;

        // Skip threads outside the visible window
        if(headerBaseValue>mw.viewByteMax || baseValue+mct.maxAllocSizeValue<mw.viewByteMin) {
            if(isGroupExpanded && ti.isExpanded) baseValue += mct.maxAllocSizeValue +  vSpacing/yFactor;
            continue;
        }

        // Draw only if the thread is expanded
        if(isGroupExpanded && ti.isExpanded) {
            baseValue += mct.maxAllocSizeValue;
            double baseY = winY+fullHeaderHeight+yFactor*(baseValue-mw.viewByteMin);
            baseValue += vSpacing/yFactor;
            const ImColor colorPoint   = cfg.getThreadColor(tId, true);
            const ImVec4  colorBase    = cfg.getThreadColor(tId);
            const ImColor colorFill    = ImColor(0.6f*colorBase.x, 0.6f*colorBase.y, 0.6f*colorBase.z);
            const ImColor colorOutline = colorPoint;

            // Draw the filled outline
            bool isFirst = true; double lastX=0., lastY=0.;
            for(const vwMain::MemCachedPoint& point : mct.points) {
                double x = winX+xFactor*(point.timeNs-mw.startTimeNs);
                double y = baseY-yFactor*point.value;
                if(isFirst) { lastX = x; lastY = y; isFirst = false; continue; }
                DRAWLIST->AddRectFilled(ImVec2(lastX-1., lastY-1.), ImVec2(x+1, baseY+1), colorOutline);
                lastX = x; lastY = y;
            }

            // Draw the filled curves
            isFirst = true; lastX=0., lastY=0.; double lastValue=0.;
            for(const vwMain::MemCachedPoint& point : mct.points) {
                const float   dimCoef    = 0.6+0.4*lastValue/mct.maxAllocSizeValue;
                const ImColor colorFill1 = ImColor(dimCoef*colorBase.x, dimCoef*colorBase.y, dimCoef*colorBase.z);
                double x = winX+xFactor*(point.timeNs-mw.startTimeNs);
                double y = baseY-yFactor*point.value;
                if(isFirst) { lastX = x; lastY = y; isFirst = false; lastValue = point.value; continue; }
                DRAWLIST->AddRectFilledMultiColor(ImVec2(lastX, lastY), ImVec2(x, baseY), colorFill1, colorFill1, colorFill, colorFill);
                lastX = x; lastY = y; lastValue = point.value;
            }

            // Draw the points (after the polygon, not to be covered)
            ClosePoint cp = { { 0, 0., 0, 0 } }; lastValue = lastX = 0.;
            for(const vwMain::MemCachedPoint& point : mct.points) {
                double x = winX+xFactor*(point.timeNs-mw.startTimeNs);
                double y = baseY-yFactor*point.value;

                // Update closest point per curve (using the mouse time, not the mouse position which may be in another window)
                if(mouseTimeToPix>x-pointSize && bsAbs(x-mouseTimeToPix)<cp.distanceX) cp = { point, bsAbs(x-mouseTimeToPix), point.value-lastValue, x, y };

                // Point to highlight? (only one per thread)
                if(isWindowHovered && closestPointTId<0 && mouseX>x-pointSize && mouseX<x+pointSize && mouseY>y-pointSize && mouseY<y+pointSize) {
                    closestPoint = { point, 0., point.value-lastValue, x, y} ;
                    closestPointTId = tId;
                    main->setScopeHighlight(tId, point.timeNs, -1, point.level, point.parentNameIdx);
                }

                // Display the point
                bool isHighlighted = main->isScopeHighlighted(tId, point.timeNs, -1, point.level, point.parentNameIdx);
                DRAWLIST->AddRectFilled(ImVec2(x-pointSize, y-pointSize), ImVec2(x+pointSize, y+pointSize), isHighlighted? vwConst::uWhite : (ImU32)colorPoint);

                // Double click on the shape?
                if(isWindowHovered && mouseX>=lastX && mouseX<x && mouseY<baseY && mouseY>baseY-yFactor*lastValue && ImGui::IsMouseDoubleClicked(0)) {
                    main->collectMemoryBlocks(mw, tId, main->_mouseTimeNs, main->_mouseTimeNs, "", false);
                    areMemoryDetailsComputed = true;
                }

                lastX     = x;
                lastValue = point.value;
            }

            // Draw the permanent tooltip (small colored box with the value)
            if(cp.distanceX<winWidth || cp.point.timeNs<=mw.startTimeNs) {
                totalUsedBytes += cp.point.value;
                double x = winX+bsMax(xFactor*(cp.point.timeNs-mw.startTimeNs), 0.);
                double y = baseY-yFactor*cp.point.value;
                snprintf(tmpStr, sizeof(tmpStr), "%s bytes (%s%s)", main->getNiceBigPositiveNumber((s64)cp.point.value),
                         (cp.deltaValue>=0.)?"+":"-", main->getNiceBigPositiveNumber((s64)bsAbs(cp.deltaValue), 1));
                double sWidth = ImGui::CalcTextSize(tmpStr).x;
                const ImColor color  = cfg.getThreadColor(tId);
                DRAWLIST->AddRectFilled(ImVec2(x+5, y), ImVec2(x+5+sWidth, y+fontHeightNoSpacing), color);
                DRAWLIST->AddText(ImVec2(x+5, y), vwConst::uWhite, tmpStr);
            }

        } // End of expanded thread memory drawing

        // Draw the group&thread headers afterwards (for transparency effects)
        bool isThreadHovered=false, isGroupHovered=false;
        double yHeader = winY+fullHeaderHeight+yFactor*(headerBaseValue-mw.viewByteMin);
        double yBottom = winY+fullHeaderHeight+yFactor*(baseValue-mw.viewByteMin);
        if(main->displayTimelineHeader(yHeader, yBottom, ti.threadId, doDrawGroupHeader, false, isThreadHovered, isGroupHovered)) {
            if(mw.allocBlockThreadId==ti.threadId) mw.allocBlockThreadId = -1; // Invalidate the detailed memory blocks
            main->synchronizeThreadLayout();
        }
        isHeaderHovered = isHeaderHovered || isThreadHovered || isGroupHovered;

        // Open contextual menu
        if((isThreadHovered || isGroupHovered) && !mw.ctxDoOpenContextMenu && !mw.isDragging && ImGui::IsMouseReleased(2)) {
            mw.ctxScopeLIdx         = PL_INVALID; // Scope-less
            mw.ctxDoOpenContextMenu = true;
        }
        // Start dragging
        if((isThreadHovered || isGroupHovered) && mw.ctxDraggedId<0 && mw.dragMode==vwMain::NONE && ImGui::GetIO().KeyCtrl && ImGui::IsMouseDragging(0)) {
            mw.ctxDraggedId      = ti.threadId;
            mw.ctxDraggedIsGroup = isGroupHovered;
        }

        main->displayTimelineHeaderPopup(mw, ti.threadId, isGroupHovered);

        // Get the hovered thread
        if(hoveredThreadId<0 && mouseY<yBottom) hoveredThreadId = ti.threadId;
    } // End of loop on threads

    if(hoveredThreadId<0 && isWindowHovered && !cfg.getLayout().empty()) {
        hoveredThreadId = cfg.getLayout().back().threadId;
    }

    // Thread dragging
    if(mw.ctxDraggedId>=0) {
        if(ImGui::IsMouseDragging(0)) {// Drag on-going: print preview
            bool isThreadHovered=false, isGroupHovered=false;
            main->displayTimelineHeader(mouseY, mouseY, mw.ctxDraggedId, mw.ctxDraggedIsGroup, true, isThreadHovered, isGroupHovered);
        }
        else { // End of drag: apply the change in group/thread order
            cfg.moveDragThreadId(mw.ctxDraggedIsGroup, mw.ctxDraggedId, hoveredThreadId);
            mw.ctxDraggedId = -1; // Stop drag automata
        }
    }

    // Draw the vertical overview bar
    double viewByteEnd = baseValue;
    double vBarCoef    = winHeight/bsMax(1., viewByteEnd);
    for(int layoutIdx=0; layoutIdx<vBarUsedLayoutQty; ++layoutIdx) {
        bool isLast = (layoutIdx==vBarUsedLayoutQty-1);
        DRAWLIST->AddRectFilled(ImVec2(winX+winWidth, winY+vBarCoef*vBarData[layoutIdx].viewByteStart),
                                ImVec2(winX+winWidth+vwConst::OVERVIEW_VBAR_WIDTH, winY+vBarCoef*(isLast? viewByteEnd : vBarData[layoutIdx+1].viewByteStart)),
                                ImColor(cfg.getThreadColor(vBarData[layoutIdx].threadId)));
    }
    DRAWLIST->AddRectFilled(ImVec2(winX+winWidth, winY), ImVec2(winX+winWidth+4., winY+winHeight), vwConst::uGreyDark);

    // Tooltip
    if(isWindowHovered && closestPointTId>=0) {
        // Draw the highlighted point
        DRAWLIST->AddRectFilled(ImVec2(closestPoint.x-pointSize, closestPoint.y-pointSize),
                                ImVec2(closestPoint.x+pointSize, closestPoint.y+pointSize), vwConst::uWhite);

        // Draw the tooltip
        ImGui::BeginTooltip();
        ImGui::TextColored(vwConst::grey, "%s%s", (closestPoint.deltaValue>=0.)?"+":"-",
                           main->getNiceBigPositiveNumber((s64)bsAbs(closestPoint.deltaValue), 1)); ImGui::SameLine();
        ImGui::Text("bytes in"); ImGui::SameLine();
        bool hasDetailedName = !record->getString(closestPoint.point.detailNameIdx).value.empty();
        snprintf(tmpStr, sizeof(tmpStr), "%s%s%s",
                 (closestPoint.point.parentNameIdx!=PL_INVALID)? record->getString(closestPoint.point.parentNameIdx).value.toChar() : "<root>",
                 hasDetailedName? "/" : "",
                 hasDetailedName? record->getString(closestPoint.point.detailNameIdx).value.toChar() : "");
        ImGui::TextColored(vwConst::grey, "%s", tmpStr);
        ImGui::EndTooltip();
    }

    // Double click outside any scopes clears details
    if(isWindowHovered && !areMemoryDetailsComputed && ImGui::IsMouseDoubleClicked(0)) {
        mw.allocBlockThreadId = -1;
    }

    return totalUsedBytes;
}


void
MemoryDrawHelper::drawAllocCallTopBars(vwMain::MemoryTimeline& mw)
{
    plgScope(MEM, "drawAllocCallTopBars");

    int    binQty       =  mw.cachedCallBins[0].size(); // Both have same size
    double binPixOffset = -mw.binTimeOffset*winWidth/mw.timeRangeNs;

    // Background (we use transparency)
    DRAWLIST->AddRectFilled(ImVec2(winX, winY), ImVec2(winX+winWidth, winY+2.*callBarHeight), vwConst::uBlack);

    // Data
    for(int callKind=0; callKind<2; ++callKind) { // 0=alloc, 1=dealloc
        double y             = winY+callKind*callBarHeight;
        double valueNormCoef = 1./bsMax(1., mw.maxCallQty);
        ImU32  colorPrev     = IM_COL32(0,0,0,255);
        for(int i=0; i<binQty; ++i) {
            // Heat colors: black->red->yellow->white
            constexpr float thres1=0.4, thres2=0.80;
            float value = valueNormCoef*mw.cachedCallBins[callKind][i]; // In [0.; 1.]
            ImU32  color = 0.;
            if     (value>thres2) color = ImColor(1.0f, 1.0f, (value-thres2)/(1.f-thres2),   1.f); // yellow(1,1,0) -> white (1,1,1)
            else if(value>thres1) color = ImColor(1.0f, (value-thres1)/(thres2-thres1), 0.f, 1.f); // red   (1,0,0) -> yellow(1,1,0)
            else if(value>0.f)    color = ImColor(bsMax(value,0.5f*thres1)/thres1, 0.f, 0.f, 1.f); // black (0,0,0) -> red   (1,0,0)

            // Draw the colored chunk
            double x1  = winX+bsMin(winWidth, binPixOffset+(i-1)*CALL_BIN_PIX);
            double x2  = winX+bsMin(winWidth, binPixOffset+i*CALL_BIN_PIX);
            DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, y), ImVec2(x2, y+callBarHeight), colorPrev, color, color, colorPrev);
            colorPrev = color;
        }
    }

    // Some framing
    DRAWLIST->AddRect(ImVec2(winX, winY), ImVec2(winX+winWidth, winY+callBarHeight),
                      vwConst::uGrey64, 0., ImDrawCornerFlags_All, 2.);
    DRAWLIST->AddRect(ImVec2(winX, winY+callBarHeight), ImVec2(winX+winWidth, winY+2*callBarHeight),
                      vwConst::uGrey64, 0., ImDrawCornerFlags_All, 2.);
}


void
MemoryDrawHelper::drawDetailedBlocks(vwMain::MemoryTimeline& mw)
{
    if(mw.allocBlockThreadId<0) return; // Nothing to draw
    plgScope(MEM, "drawDetailedBlocks");

    // Get some values
    constexpr float blockBorder = 1;
    const double  textMargin  = 0.5*ImGui::GetStyle().ItemSpacing.x;
    const ImColor colorBlock1 = ImColor(1.f, 1.f, 1.f, 0.3f); // Transparent
    const ImColor colorBlock2 = ImColor(1.f, 1.f, 1.f, 0.5f);
    const ImColor colorThin1   = ImColor(1.f, 1.f, 1.f, 0.3f);
    const ImColor colorThin2  = ImColor(1.f, 1.f, 1.f, 0.4f);
    const double  yMin        = winY+2*callBarHeight;
    const double  xFactor     = winWidth/bsMax(1., mw.timeRangeNs);
    const double  yBlockFactor = bsMin(mw.cachedThreadData[mw.allocBlockThreadId].maxAllocSizeValue/(mw.startTimeVPtr+mw.maxVPtr), 1.)*yFactor; // Correction for higher vPtr due to packing holes
    const double  bottomValue = mw.valuePerThread[mw.allocBlockThreadId]+mw.cachedThreadData[mw.allocBlockThreadId].maxAllocSizeValue;;
    const double  baseY       = winY+fullHeaderHeight+yFactor*(bottomValue-mw.viewByteMin - yBlockFactor/yFactor*mw.startTimeVPtr);
    const double minCharWidth = 2.*8.+textMargin;

    char tmpStr[128];
    mw.workLkupFusionedBlocks.clear();

    // Loop on memory scopes to display
    for(int blockIdx : mw.rawAllocBlockOrder) {
        vwMain::MemAlloc& ma = mw.rawAllocBlocks[blockIdx];
        // Filter out invalid blocks and those outside the time range
        if(ma.vPtr==PL_INVALID) continue;
        if(ma.endTimeNs>=0 && ma.endTimeNs<mw.startTimeNs) continue;
        if(ma.startTimeNs>=mw.startTimeNs+mw.timeRangeNs)  continue;

        double y1 = baseY-yBlockFactor* ma.vPtr;
        double y2 = baseY-yBlockFactor*(ma.vPtr+ma.size);
        if(y1<winY+fullHeaderHeight || y2>winY+winHeight) continue;
        double x1 = winX+xFactor*(ma.startTimeNs-mw.startTimeNs);
        double x2 = winX+xFactor*(((ma.endTimeNs>=0)? ma.endTimeNs : record->durationNs)-mw.startTimeNs);
        bool isAllocSide   = (ma.endThreadId==0xFFFF || main->_mouseTimeNs-ma.startTimeNs<ma.endTimeNs-main->_mouseTimeNs);
        bool isHighlighted = (main->isScopeHighlighted(mw.allocBlockThreadId, ma.startTimeNs, -1, ma.startLevel-1, ma.startParentNameIdx) ||
                              main->isScopeHighlighted(ma.endThreadId, ma.endTimeNs, -1, ma.endLevel-1, ma.endParentNameIdx));

        // Very thin scope?
        if(y1-y2<BLOCK_MIN_ROW_PIX) {
            int rowNumber = (int)(y1/BLOCK_MIN_ROW_PIX);
            u64 hashRowIdx = bsHashStep(rowNumber); // Computed once
            vwMain::MemFusioned* fusion = mw.workLkupFusionedBlocks.find(hashRowIdx, rowNumber);;
            if(!fusion) { // Insert the small line. Y is quantized
                mw.workLkupFusionedBlocks.insert(hashRowIdx, rowNumber, { (int)bsMax(x1, winX), (int)bsMin(x2, winX+winWidth),
                                                                          rowNumber*(int)BLOCK_MIN_ROW_PIX });
            } else if(x1<=2.+fusion->x2) { // Simple fusion and relatively efficient as x1 are sorted
                mw.workLkupFusionedBlocks.insert(hashRowIdx, rowNumber, { fusion->x1, bsMax(fusion->x2, (int)bsMin(x2, winX+winWidth)), fusion->y }); // Update the end of the small line
            } else {
                // Draw the previous & non-overlapping thin block per strip (blocks are sorted by x)
                int x      = fusion->x1;
                int colIdx = fusion->y/BLOCK_MIN_ROW_PIX + x/SMALL_BLOCK_PATTERN_WIDTH;
                while(x<fusion->x2) {
                    int nextX = ((x/SMALL_BLOCK_PATTERN_WIDTH)+1)*SMALL_BLOCK_PATTERN_WIDTH;
                    DRAWLIST->AddRectFilled(ImVec2(x, fusion->y), ImVec2(nextX, fusion->y-BLOCK_MIN_ROW_PIX), (colIdx&1)? colorThin1 : colorThin2);
                    x = nextX; ++colIdx;
                }
                // Insert the new incoming thin scope at the place of the displayed line just above
                mw.workLkupFusionedBlocks.insert(hashRowIdx, rowNumber, { (int)bsMax(x1, winX), (int)bsMin(x2, winX+winWidth),
                                                                          rowNumber*(int)BLOCK_MIN_ROW_PIX });
            }
            continue;
        }

        // Memory scope hovered?
        if(isWindowHovered && mouseX>x1 && mouseX<x2 && mouseY>y2 && mouseY<y1) {
            // Highlight the scope everywhere
            isHighlighted       = true;
            if(isAllocSide) { // Closer to alloc than dealloc
                main->setScopeHighlight(mw.allocBlockThreadId, ma.startTimeNs, -1, ma.startLevel-1, ma.startParentNameIdx);
            } else {
                main->setScopeHighlight(ma.endThreadId, ma.endTimeNs, -1, ma.endLevel-1, ma.endParentNameIdx);
            }

            // Single click: synchronize timeline and text
            if(mw.syncMode>0 && ImGui::IsMouseReleased(0) && mw.dragMode==vwMain::NONE) {
                // Ensure that the thread is visible in the (synchronized) timeline
                int threadId = isAllocSide? mw.allocBlockThreadId:ma.endThreadId;
                if(threadId!=0xFFFF) { // Leaked memory block have no valid thread
                    main->ensureThreadVisibility(mw.syncMode, threadId);
                }
                mw.viewThreadId = -1; // Cancel for current window, as we do not want jumps to beginning of thread
            }

            // Tooltip
            ImGui::BeginTooltip();
            s64 timeRangeNs = mw.allocBlockEndTimeNs-mw.allocBlockStartTimeNs;
            if(timeRangeNs<=0) {
                ImGui::TextColored(vwConst::gold, "All allocations present at time %s", main->getNiceTime(mw.allocBlockStartTimeNs, timeRangeNs, 0));
            } else {
                ImGui::TextColored(vwConst::gold, "Allocations from scope '%s' (%s -> %s)", mw.allocScopeName.toChar(),
                                   main->getNiceTime(mw.allocBlockStartTimeNs, timeRangeNs, 0), main->getNiceTime(mw.allocBlockEndTimeNs, timeRangeNs, 1));
            }
            ImGui::Separator();
            ImGui::TextColored(vwConst::grey, "%s", main->getNiceBigPositiveNumber(ma.size)); ImGui::SameLine();
            ImGui::TextColored(vwConst::white, "bytes"); ImGui::SameLine();
            if(ma.endTimeNs>=0) {
                ImGui::TextColored(vwConst::white, "{"); ImGui::SameLine();
                ImGui::TextColored(vwConst::grey, "%s", main->getNiceDuration(ma.endTimeNs-ma.startTimeNs)); ImGui::SameLine();
                ImGui::TextColored(vwConst::white, "lifetime }");
            } else {
                ImGui::TextColored(vwConst::red, "Leaked");
            }
            ImGui::Separator();
            ImGui::TextColored(vwConst::white, "Allocated     in"); ImGui::SameLine();
            bool hasDetailedName = !record->getString(ma.startNameIdx).value.empty();
            ImGui::TextColored(isAllocSide? ImColor(vwConst::grey) : colorThin1, "[%s] '%s%s%s'", record->getString(record->threads[mw.allocBlockThreadId].nameIdx).value.toChar(),
                               (ma.startParentNameIdx!=PL_INVALID)? record->getString(ma.startParentNameIdx).value.toChar() : "<root>",
                               hasDetailedName? "/":"", hasDetailedName? record->getString(ma.startNameIdx).value.toChar():"");
            ImGui::SameLine();
            ImGui::TextColored(vwConst::white, "at time"); ImGui::SameLine();
            ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(ma.startTimeNs, 0.1*mw.timeRangeNs)); // Time precision is ~10% if the range
            if(ma.endTimeNs>=0) {
                ImGui::TextColored(vwConst::white, "Deallocated in"); ImGui::SameLine();
                hasDetailedName = !record->getString(ma.endNameIdx).value.empty();
                ImGui::TextColored(isAllocSide? colorThin1 : ImColor(vwConst::grey), "[%s] '%s%s%s'",
                                   record->getString(record->threads[ma.endThreadId].nameIdx).value.toChar(),
                                   (ma.endParentNameIdx!=PL_INVALID)? record->getString(ma.endParentNameIdx).value.toChar() : "<root>",
                                   hasDetailedName? "/":"", hasDetailedName? record->getString(ma.endNameIdx).value.toChar():"");
                ImGui::SameLine();
                ImGui::TextColored(vwConst::white, "at time"); ImGui::SameLine();
                ImGui::TextColored(vwConst::grey, "%s", main->getNiceTime(ma.endTimeNs, 0.1*mw.timeRangeNs));
            }
            ImGui::EndTooltip();
        }

        // Draw the rectangle
        DRAWLIST->AddRectFilled(ImVec2(x1-blockBorder, y1+blockBorder), ImVec2(x2+blockBorder, bsMin(y1-1.,y2)-blockBorder), colorThin1); // Outlook
        if(isHighlighted) {
            if(isAllocSide) DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, bsMin(y1-1., y2)),
                                                              vwConst::uYellow, vwConst::uWhite, vwConst::uWhite, vwConst::uYellow);
            else            DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, bsMin(y1-1., y2)),
                                                              vwConst::uWhite, vwConst::uYellow, vwConst::uYellow, vwConst::uWhite);
        }
        else                DRAWLIST->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, bsMin(y1-1., y2)),
                                                              colorBlock1, colorBlock2, colorBlock1, colorBlock1);

        // Draw the text, if enough space
        x1 = bsMinMax(x1, winX, winX+winWidth);
        x2 = bsMinMax(x2, winX, winX+winWidth);
        y1 = bsMinMax(y1, yMin, winY+winHeight);
        y2 = bsMinMax(y2, yMin, winY+winHeight);
        if(y1-y2>fontHeightNoSpacing && x2-x1>minCharWidth) {
            bool hasDetailedName = !record->getString(ma.startNameIdx).value.empty();
            // Write on 2 lines
            if(hasDetailedName && y1-y2>2*fontHeightNoSpacing) {
                for(int i=0; i<2; ++i) {
                    u32 idx = i? ma.startNameIdx:ma.startParentNameIdx;
                    const char* s         = (idx!=PL_INVALID)? record->getString(idx).value.toChar() : "<root>";
                    const char* remaining = 0;
                    double      textWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), x2-x1-textMargin, 0.0f, s, NULL, &remaining).x;
                    if(s!=remaining) DRAWLIST->AddText(ImVec2(0.5*(x1+x2-textWidth), 0.5*(y1+y2+(i-1)*2*fontHeightNoSpacing)), vwConst::uGrey64, s, remaining);
                }
            }
            // Write on 1 line
            else {
                const char* s = 0;
                if(hasDetailedName) {
                    snprintf(tmpStr, sizeof(tmpStr),
                             "%s / %s", (ma.startParentNameIdx!=PL_INVALID)? record->getString(ma.startParentNameIdx).value.toChar() : "<root>",
                             record->getString(ma.startNameIdx).value.toChar());
                    s = tmpStr;
                } else s = (ma.startParentNameIdx!=PL_INVALID)? record->getString(ma.startParentNameIdx).value.toChar() : "<root>";
                const char* remaining = 0;
                double      textWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), x2-x1-textMargin, 0.0f, s, NULL, &remaining).x;
                if(s!=remaining) DRAWLIST->AddText(ImVec2(0.5*(x1+x2-textWidth), 0.5*(y1+y2-fontHeightNoSpacing)), vwConst::uGrey64, s, remaining);
            }
        }

    } // End of loop on memory blocks

    // Display the remaining thin blocks
    bsVec<vwMain::MemFusioned> remainingThinBlocks;
    mw.workLkupFusionedBlocks.exportData(remainingThinBlocks);
    for(auto& fusion : remainingThinBlocks) {
        int x      = fusion.x1;
        int colIdx = fusion.y/BLOCK_MIN_ROW_PIX + x/SMALL_BLOCK_PATTERN_WIDTH;
        while(x<fusion.x2) {
            int nextX = ((x/SMALL_BLOCK_PATTERN_WIDTH)+1)*SMALL_BLOCK_PATTERN_WIDTH;
            DRAWLIST->AddRectFilled(ImVec2(x, fusion.y), ImVec2(nextX, fusion.y-BLOCK_MIN_ROW_PIX), (colIdx&1)? colorThin1 : colorThin2);
            x = nextX; ++colIdx;
        }
    }
}


// Memory timeline data preparation
// ================================

void
vwMain::addMemoryTimeline(int id)
{
    if(!_record) return;
    // Add the memory timeline window entry
    _memTimelines.push_back( { } );
    auto& mw    = _memTimelines.back();
    mw.uniqueId = id;
    getSynchronizedRange(mw.syncMode, mw.startTimeNs, mw.timeRangeNs);
    memset(&mw.valuePerThread[0], 0, sizeof(mw.valuePerThread));

    setFullScreenView(-1);
    plMarker("user", "Add a memory timeline");
}


void
vwMain::prepareMemoryTimeline(MemoryTimeline& mw)
{
    // Worth working?
    const double winWidth = bsMax(1.f, ImGui::GetWindowContentRegionMax().x-vwConst::OVERVIEW_VBAR_WIDTH);
    if(!mw.isCacheDirty && mw.lastWinWidth==winWidth) return;
    mw.isCacheDirty = false;
    mw.lastWinWidth = winWidth;

    // Create the empty cached thread data
    while(mw.cachedThreadData.size()<_record->threads.size()) mw.cachedThreadData.push_back({});

    // Some init for the top call bands
    plgScope(MEM, "prepareMemoryTimeline");
    int    binQty        = bsDivCeil(winWidth, CALL_BIN_PIX);
    double timeToBinCoef = ((double)binQty)/mw.timeRangeNs;
    // Margin required to prevent bad display on bar extremities
    // In order to avoid flickering, we phase the bin. Also we add a bin margin on borders to enforce good values there
    mw.binTimeOffset = (double)CALL_BIN_MARGIN/timeToBinCoef + (timeToBinCoef*mw.startTimeNs-(int)(timeToBinCoef*mw.startTimeNs))/timeToBinCoef;
    mw.maxCallQty = 15;
    // Initialize the bins
    for(int callKind=0; callKind<2; ++callKind) { // 0=alloc, 1=dealloc
        mw.cachedCallBins[callKind].resize(binQty+2*CALL_BIN_MARGIN);
        memset((void*)&mw.cachedCallBins[callKind][0], 0, (binQty+2*CALL_BIN_MARGIN)*sizeof(int));
    }

    // Loop on the threads
    for(int tId=0; tId<_record->threads.size(); ++tId) {

        // Reset the thread structure
        MemCachedThread& mct = mw.cachedThreadData[tId];
        mct.points.clear();
        mct.maxAllocSizeValue = 0.;

        // Get the plot index of memory allocation size for this thread
        int* elemIdx = _record->elemPathToId.find(bsHashStepChain(_record->threads[tId].threadHash, cmConst::MEMORY_ALLOCSIZE_NAMEIDX),
                                                  cmConst::MEMORY_ALLOCSIZE_NAMEIDX);
        if(!elemIdx) continue; // No memory elem for this thread
        bsVec<MemCachedPoint>& curve = mct.points; curve.reserve(1024);
        mct.maxAllocSizeValue = _record->elems[*elemIdx].absYMax;
        if(!getConfig().getGroupAndThreadExpanded(tId)) {
            // 0 would prevent any header display as a fully memory empty thread. Of course, keep zero in the latter case
            if(mct.maxAllocSizeValue>0) mct.maxAllocSizeValue = 1;
            continue;
        }

        // Collect the curve points
        const cmRecord::Evt* e = 0;
        cmRecordIteratorMemStat it(_record, *elemIdx, mw.startTimeNs, BLOCK_MIN_ROW_PIX*mw.timeRangeNs/winWidth);
        while((e=it.getNextMemStat())) {
            curve.push_back( { e->vS64, (double)e->memElemValue, (s16)(e->level), e->flags, e->filenameIdx, e->nameIdx } );
            if(e->vS64>=mw.startTimeNs+mw.timeRangeNs) break; // Time break after storage as we want 1 point past the range
        }
        if(e && e->memElemValue>0. && e->vS64<mw.startTimeNs+mw.timeRangeNs) { // Push a last point if needed
            curve.push_back( { (s64)(mw.startTimeNs+mw.timeRangeNs), (double)e->memElemValue, 0, 0, 0, 0 } );
        }

        // Update the (de-)allocation call bins with this thread
        for(int callKind=0; callKind<2; ++callKind) { // 0=alloc, 1=dealloc
            // Init the data collection
            int callNameIdx = callKind? cmConst::MEMORY_DEALLOCQTY_NAMEIDX : cmConst::MEMORY_ALLOCQTY_NAMEIDX;
            elemIdx = _record->elemPathToId.find(bsHashStepChain(_record->threads[tId].threadHash, callNameIdx), callNameIdx);
            if(!elemIdx) continue; // No memory elem for this thread

            // Collect the alloc and dealloc counts
            double lastPtValue = 0.;
            bool   isFirst = true;
            cmRecordIteratorMemStat it2(_record, *elemIdx, mw.startTimeNs-mw.binTimeOffset, mw.timeRangeNs/winWidth);
            while((e=it2.getNextMemStat()) && e->vS64<=mw.startTimeNs+mw.timeRangeNs+mw.binTimeOffset) {
                // Get the bin index
                double ptValue = e->memElemValue;
                if(isFirst) { lastPtValue = ptValue; isFirst = false; }
                if(ptValue==lastPtValue) continue;
                int binIdx = int(timeToBinCoef*(e->vS64-mw.startTimeNs+mw.binTimeOffset)+CALL_BIN_MARGIN+0.5)-CALL_BIN_MARGIN; // Such rounding works only when positive, hence the +CALL_BIN_MARGIN+0.5
                if(binIdx<0 || binIdx>=binQty+2*CALL_BIN_MARGIN) { lastPtValue = ptValue; continue; }
                // Update the bin
                double deltaCallQty = ptValue-lastPtValue;
                lastPtValue         = ptValue;
                mw.cachedCallBins[callKind][binIdx] += (int)deltaCallQty; // Always positive by design of the call curves
                if(mw.cachedCallBins[callKind][binIdx]>mw.maxCallQty) mw.maxCallQty = mw.cachedCallBins[callKind][binIdx];
            }
        }
    } // End of loop on threads
}


void
vwMain::collectMemoryBlocks(MemoryTimeline& mw, int threadId, s64 startTimeNs, s64 endTimeNs, const bsString& scopeName,
                            bool onlyInRange, bool doAdaptViewValueRange)
{
    if(!getConfig().getGroupAndThreadExpanded(threadId)) return; // Not visible = no computation

    // Some init
    plgScope(MEM, "collectMemoryBlocks");
    mw.workDeallocBlockIndexes.clear(); mw.workDeallocBlockIndexes.reserve(1024);
    mw.workEmptyAllocBlockIndexes.clear(); mw.workEmptyAllocBlockIndexes.reserve(1024);
    mw.workLkupAllocBlockIdx.clear();
    mw.rawAllocBlocks.clear();
    mw.rawAllocBlocks.reserve(1024);
    mw.rawAllocBlockOrder.clear();
    mw.rawAllocBlockOrder.reserve(1024);
    mw.workVAlloc.reset();
    mw.allocBlockStartTimeNs = startTimeNs;
    mw.allocBlockEndTimeNs   = endTimeNs;
    mw.allocScopeName        = scopeName;
    mw.doAdaptViewValueRange = doAdaptViewValueRange;
    mw.allocBlockThreadId    = threadId;
    mw.startTimeVPtr         = 0;
    mw.maxVPtr               = 0;

    // Get the initial state
    bsVec<u32> initAllocMIdxs;
    cmRecord::Evt e, e2;
    cmRecordIteratorMemScope it(_record, threadId, startTimeNs, &initAllocMIdxs);
    for(const u32& allocMIdx: initAllocMIdxs) {
        if(!it.getAllocEvent(allocMIdx, e)) continue; // Weird case
        mw.workLkupAllocBlockIdx.insert(allocMIdx, mw.rawAllocBlocks.size());
        mw.rawAllocBlocks.push_back( { allocMIdx, 0, e.vS64, e.allocSizeOrMIdx, e.filenameIdx, e.nameIdx, (u16)e.level } );
    }

    // Go to the start of the desired range
    u32  recordAllocMIdx  = 0;
    bool isFirstVAllocationDone = false;
    while(it.getNextMemScope(e, recordAllocMIdx)) {
        // Out of desired range?
        if(e.vS64>endTimeNs) break;

        // First vAllocation done?
        if(!isFirstVAllocationDone && e.vS64>=startTimeNs && !(e.vS64==startTimeNs && e.flags==PL_FLAG_TYPE_DEALLOC)) {
            isFirstVAllocationDone = true;
            for(int i=0; i<mw.rawAllocBlocks.size(); ++i) { // No more in chronological allocation order, but ok anyway
                auto& rab = mw.rawAllocBlocks[i];
                if(rab.vPtr==PL_INVALID) continue;
                if(onlyInRange && (!it.getDeallocEvent(rab.allocMIdx, e2) || e2.vS64>endTimeNs)) {
                    bool isOk = mw.workLkupAllocBlockIdx.erase(rab.allocMIdx); plAssert(isOk); // @#BUG [MEMORY] Assert failed at least once when clicking on memory curve...
                    rab.vPtr  = PL_INVALID;
                    mw.startTimeVPtr += rab.size;
                    mw.workEmptyAllocBlockIndexes.push_back(i);
                }
                else {
                    rab.vPtr = mw.workVAlloc.malloc(rab.size);
                }
            }
        }

        // Case allocation
        if(e.flags==PL_FLAG_TYPE_ALLOC) {
            plAssert(e.threadId==threadId);

            // Create the scope and store it (in a recycled location, or a new one if no empty location exists)
            u32 vPtr = isFirstVAllocationDone? mw.workVAlloc.malloc(e.allocSizeOrMIdx) : 0; // Allocate only if this process is activated
            if(mw.workEmptyAllocBlockIndexes.empty()) {
                mw.workLkupAllocBlockIdx.insert(recordAllocMIdx, mw.rawAllocBlocks.size());
                mw.rawAllocBlocks.push_back( { recordAllocMIdx, vPtr, e.vS64, e.allocSizeOrMIdx, e.filenameIdx, e.nameIdx, (u16)e.level } );
            }
            else {
                u32 scopeAllocIdx = mw.workEmptyAllocBlockIndexes.back(); mw.workEmptyAllocBlockIndexes.pop_back();
                mw.workLkupAllocBlockIdx.insert(recordAllocMIdx, scopeAllocIdx);
                mw.rawAllocBlocks[scopeAllocIdx] = { recordAllocMIdx, vPtr, e.vS64, e.allocSizeOrMIdx, e.filenameIdx, e.nameIdx, (u16)e.level };
            }
        }

        // Case deallocation
        else if(e.flags==PL_FLAG_TYPE_DEALLOC) {
            u32* scopeAllocIdxPtr = mw.workLkupAllocBlockIdx.find(recordAllocMIdx);
            if(!scopeAllocIdxPtr) continue; // May happen when asking for range internal activity only
            u32 scopeAllocIdx = *scopeAllocIdxPtr;
            // Deallocate in the virtual allocator
            MemAlloc& rab = mw.rawAllocBlocks[scopeAllocIdx];
            bool isOk = mw.workLkupAllocBlockIdx.erase(recordAllocMIdx); plAssert(isOk);
            if(isFirstVAllocationDone) mw.workVAlloc.free(rab.vPtr); // Deallocate only if this process is activated

            // Update the storage
            if(e.vS64<=startTimeNs) { // alloc+dealloc before the observed range: move alloc scope back to empty list
                rab.vPtr = PL_INVALID;
                mw.workEmptyAllocBlockIndexes.push_back(scopeAllocIdx);
            }
            else { // Dealloc inside the observed range: we update the scope with the dealloc infos
                rab.endTimeNs   = e.vS64;
                rab.endParentNameIdx  = e.filenameIdx;
                rab.endNameIdx  = e.nameIdx;
                rab.endThreadId = e.threadId;
                rab.endLevel    = e.level;
                mw.workDeallocBlockIndexes.push_back(scopeAllocIdx); // Stored in order, used in the second phase below
            }
        }
    }

    // Ensure vAllocation has been done (not the case for punctual range)
    if(!isFirstVAllocationDone) {
        u32 vPtr = 0;
        for(auto& rab : mw.rawAllocBlocks) {
            if(rab.vPtr==PL_INVALID) continue;
            if(onlyInRange && (!it.getDeallocEvent(rab.allocMIdx, e2) || e2.vS64>endTimeNs)) rab.vPtr = PL_INVALID;
            else { rab.vPtr = vPtr; vPtr += rab.size; } // Canonical allocation (equivalent to vMalloc in this case but faster)
        }
        mw.startTimeVPtr = vPtr;
    }
    if(!onlyInRange) mw.startTimeVPtr = 0; // No display bias, as we get the full allocation list

    // Find the remaining dealloc events, for each allocation not yet deallocated
    int validQty = 0;
    for(int i=0; i<mw.rawAllocBlocks.size(); ++i) {
        MemAlloc& rab = mw.rawAllocBlocks[i];
        if(rab.vPtr==PL_INVALID) continue;// Skip invalid
        if(rab.vPtr+rab.size>mw.maxVPtr) mw.maxVPtr = rab.vPtr+rab.size;
        ++validQty;
        mw.rawAllocBlockOrder.push_back(i);
        if(rab.endTimeNs>=0) continue;    // Skip already filled memory scopes
        if(!it.getDeallocEvent(rab.allocMIdx, e)) continue; // No deallocation, so "leaked"
        rab.endTimeNs   = e.vS64;
        rab.endParentNameIdx  = e.filenameIdx;
        rab.endNameIdx  = e.nameIdx;
        rab.endThreadId = e.threadId;
        rab.endLevel    = e.level;
    }

    // Sort it by increasing start time, in order to fusion small blocks more efficiently when displaying
    std::sort(mw.rawAllocBlockOrder.begin(), mw.rawAllocBlockOrder.end(),
              [&mw](int a, int b)->bool { return mw.rawAllocBlocks[a].startTimeNs<mw.rawAllocBlocks[b].startTimeNs; });

    // If no allocation scope have been found, then cancel
    if(validQty==0) {
        mw.allocBlockThreadId     = -1;
        mw.doAdaptViewValueRange = false;
    }
}


// Draw the memory timeline
// ========================

void
vwMain::drawMemoryTimelines(void)
{
    if(!_record) return;
    plgScope(MEM, "drawMemoryTimelines");
    char tmpStr[128];

    // Loop on memory timelines
    int itemToRemoveIdx = -1;
    for(int mwWindowIdx=0; mwWindowIdx<_memTimelines.size(); ++mwWindowIdx) {
        MemoryTimeline& m = _memTimelines[mwWindowIdx];
        if(_liveRecordUpdated) m.isCacheDirty = true;
        if(_uniqueIdFullScreen>=0 && m.uniqueId!=_uniqueIdFullScreen) continue;

        if(m.isNew) {
            m.isNew = false;
             if(m.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(m.newDockId);
            else selectBestDockLocation(true, true);
        }
        if(m.isWindowSelected) {
            m.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }

        snprintf(tmpStr, sizeof(tmpStr), "Memory ###%d", m.uniqueId);
        bool isOpen = true;
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) drawMemoryTimeline(mwWindowIdx);
        ImGui::End();

        if(!isOpen) itemToRemoveIdx = mwWindowIdx;
    }

    // Remove memory timelines (if asked)
    if(itemToRemoveIdx>=0) {
        releaseId((_memTimelines.begin()+itemToRemoveIdx)->uniqueId);
        _memTimelines.erase(_memTimelines.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }

    // Loop on memory detail lists
    itemToRemoveIdx = -1;
    for(int detailWindowIdx=0; detailWindowIdx<_memDetails.size(); ++detailWindowIdx) {
        MemDetailListWindow& m = _memDetails[detailWindowIdx];

        // Title
        s64 timeRangeNs = m.endTimeNs-m.startTimeNs;
        if(timeRangeNs<=0) {
            snprintf(tmpStr, sizeof(tmpStr), "[%s] List of the %d memory allocations present at time %s###list%d", _record->getString(_record->threads[m.threadId].nameIdx).value.toChar(),
                     _memDetails[detailWindowIdx].allocBlocks.size(), getNiceTime(m.startTimeNs, timeRangeNs, 0), m.uniqueId);
        } else {
            snprintf(tmpStr, sizeof(tmpStr), "[%s] List of the %d memory allocations from scope '%s' (%s -> %s)###list%d", _record->getString(_record->threads[m.threadId].nameIdx).value.toChar(),
                     _memDetails[detailWindowIdx].allocBlocks.size(), m.allocScopeName.toChar(),
                     getNiceTime(m.startTimeNs, timeRangeNs, 0), getNiceTime(m.endTimeNs, timeRangeNs, 1), m.uniqueId);
        }

        // Window & content
        bool isOpen = true;
        ImGui::SetNextWindowPos(ImVec2(0.5*getDisplayWidth(),0.5*getDisplayHeight()), ImGuiCond_Once, ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(0.8*getDisplayWidth(),0.8*getDisplayHeight()), ImGuiCond_Once);
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse)) {
            drawMemoryDetailList(detailWindowIdx);
        }
        ImGui::End();

        if(!isOpen) itemToRemoveIdx = detailWindowIdx;
    }

    // Remove memory detailed list (if asked)
    if(itemToRemoveIdx>=0) {
        releaseId((_memDetails.begin()+itemToRemoveIdx)->uniqueId);
        _memDetails.erase(_memDetails.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::drawMemoryTimeline(int curMwWindowIdx)
{
    plgScope(MEM, "drawMemoryTimeline");
    plgData(MEM, "number", curMwWindowIdx);
    MemoryTimeline& mw = _memTimelines[curMwWindowIdx];

    // Ruler and visible range bar
    double rbWidth, rbStartPix, rbEndPix;
    double rulerHeight = getTimelineHeaderHeight(false, true);
    ImGui::BeginChild("ruler", ImVec2(0, 2.0*ImGui::GetStyle().WindowPadding.y+rulerHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
    const bool isBarHovered  = ImGui::IsWindowHovered();
    drawTimeRuler(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowContentRegionMax().x, rulerHeight,
                  mw.startTimeNs, mw.timeRangeNs, mw.syncMode, rbWidth, rbStartPix, rbEndPix);
    ImGui::EndChild();

    // We manage the wheel ourselves as the display area has virtual coordinates
    ImGui::BeginChild("mwArea", ImVec2(0,0), false,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    MemoryDrawHelper ctx;
    const double fontHeight      = ImGui::GetTextLineHeightWithSpacing();
    const bool   isWindowHovered = ImGui::IsWindowHovered();
    const double winX      = ImGui::GetWindowPos().x;
    const double winY      = ImGui::GetWindowPos().y;
    const double winWidth  = ImGui::GetWindowContentRegionMax().x-vwConst::OVERVIEW_VBAR_WIDTH;
    const double winHeight = bsMax(1.f, ImGui::GetWindowSize().y);
    const double mouseX    = ImGui::GetMousePos().x;
    const double mouseY    = ImGui::GetMousePos().y;
    const double vMargin   = ImGui::GetTextLineHeight();  // fontHeight margin to allow overlayed text on top;
    vwConfig& cfg = getConfig();

    ctx.main       = this;
    ctx.record     = _record;
    ctx.winX       = winX;
    ctx.winY       = winY;
    ctx.winWidth   = winWidth;
    ctx.winHeight  = winHeight;
    ctx.fontHeight = fontHeight;
    ctx.fontHeightNoSpacing = ImGui::GetTextLineHeight();
    ctx.fontSpacing         = 0.5*ImGui::GetStyle().ItemSpacing.y;
    ctx.callBarHeight       = 8.;
    ctx.fullHeaderHeight    = 2*ctx.callBarHeight+vMargin;
    ctx.threadTitleMargin   = 2*ctx.fontSpacing;
    ctx.drawableHeight      = bsMax(ctx.winHeight-ctx.fullHeaderHeight, 1);
    ctx.isWindowHovered     = isWindowHovered;
    ctx.mouseX = ImGui::GetMousePos().x;
    ctx.mouseY = ImGui::GetMousePos().y;
    const double fullHeaderHeight = ctx.fullHeaderHeight;

    prepareMemoryTimeline(mw); // Ensure cache is up-to-date, even at window creation
    ctx.computeLayout(mw);

    if(mw.doAdaptViewValueRange && mw.allocBlockThreadId>=0) {
        // Get the "independent" coordinate
        double tmp = mw.cachedThreadData[mw.allocBlockThreadId].maxAllocSizeValue;
        const double yCorrectedRatio = bsMin(1., tmp/(mw.startTimeVPtr+mw.maxVPtr)); // Correction for higher vPtr due to packing holes
        double newThreadValue = yCorrectedRatio*(tmp-mw.startTimeVPtr)-mw.maxVPtr;
        double newValueRange  = yCorrectedRatio*mw.maxVPtr;

        // Convert this Y coordinate to the new zoom
        ctx.getValueFromIValue(mw, mw.allocBlockThreadId, newThreadValue, ctx.drawableHeight/newValueRange,  mw.viewByteMin, ctx.viewByteMaxLimit);
        mw.viewByteMax   = mw.viewByteMin + yCorrectedRatio*mw.maxVPtr;
        mw.lastWinHeight =  0.; // Invalidate the cache for scope fusion
        mw.viewThreadId  = -1;  // Cancel any forcing of position, as we just did one
        mw.doAdaptViewValueRange = false;
        mw.didUserChangedScrollPos = true;
        ctx.computeLayout(mw);
    }

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    float lastScrollPos = ImGui::GetScrollY();
    if(!mw.didUserChangedScrollPos && bsAbs(lastScrollPos-mw.lastScrollPos)>=1.) {
        plgScope(MEM, "New user scroll position from ImGui");
        plgData(MEM, "expected pos", mw.lastScrollPos);
        plgData(MEM, "new pos", lastScrollPos);
        float visibleRatio = (mw.viewByteMax-mw.viewByteMin)/ctx.viewByteMaxLimit;
        float cursorEndY   = fullHeaderHeight+(winHeight-fullHeaderHeight)/visibleRatio;
        double deltaY      = (lastScrollPos/(cursorEndY-fullHeaderHeight))*ctx.viewByteMaxLimit-mw.viewByteMin;
        deltaY = bsMin(deltaY, ctx.viewByteMaxLimit-mw.viewByteMax);
        deltaY = bsMax(deltaY, -mw.viewByteMin);
        mw.viewByteMin += deltaY;
        mw.viewByteMax += deltaY;
    }

    // Handle animation (smooth move)
    mw.updateAnimation();

    // Previous navigation may have made dirty the cached data
    mw.checkTimeBounds(_record->durationNs);
    prepareMemoryTimeline(mw);

    // Force scrolling to see a particular thread (value range is constant)
    if(mw.viewThreadId>=0) {
        double threadValueMin = mw.valuePerThread[mw.viewThreadId];
        double threadValueMax = threadValueMin + mw.cachedThreadData[mw.viewThreadId].maxAllocSizeValue;
        if(threadValueMax<mw.viewByteMin || threadValueMin>mw.viewByteMax) {
            double move = mw.valuePerThread[mw.viewThreadId]-mw.viewByteMin;
            if(mw.viewByteMin+move>ctx.viewByteMaxLimit) move = ctx.viewByteMaxLimit-mw.viewByteMin;
            if(mw.viewByteMin+move<0.) move = -mw.viewByteMin;
            if(mw.viewByteMax+move>ctx.viewByteMaxLimit) move = ctx.viewByteMaxLimit-mw.viewByteMax;
            if(mw.viewByteMax+move<0.) move = -mw.viewByteMax;
            mw.viewByteMin += move;
            mw.viewByteMax += move;
            mw.didUserChangedScrollPos = true;
        }
        mw.viewThreadId = -1;
    }
    ctx.computeLayout(mw);

    const double visibleRatio = (mw.viewByteMax-mw.viewByteMin)/ctx.viewByteMaxLimit;
    const double cursorEndY   = fullHeaderHeight+(winHeight-fullHeaderHeight)/visibleRatio;
    const double xFactor      = winWidth/bsMax(1., mw.timeRangeNs);

    // Set the modified scroll position in ImGui, if not changed through ImGui
    if(mw.didUserChangedScrollPos) {
        float scrollPosY = bsMax((cursorEndY-fullHeaderHeight)*mw.viewByteMin/ctx.viewByteMaxLimit, 0.);
        plgData(MEM, "Set new scroll pos from user", scrollPosY);
        plgData(MEM, "Max possible pos", ImGui::GetScrollMaxY());
        ImGui::SetScrollY(scrollPosY);
    }

    // Mark the virtual total size
    mw.lastScrollPos = ImGui::GetScrollY();
    plgData(MEM, "Current scroll pos", mw.lastScrollPos);
    plgData(MEM, "Max scroll pos", cursorEndY);
    plgData(MEM, "Current max scroll pos", ImGui::GetScrollMaxY());
    ImGui::SetCursorPosY(cursorEndY);

    // Display the window components
    // =============================

    // Filled curves
    u32  totalUsedBytes = ctx.drawMemoryCurves(mw);
    // Detailed blocks, if asked
    ctx.drawDetailedBlocks(mw);
    // Top bar with call counts
    ctx.drawAllocCallTopBars(mw);

    // Display the vertical background stripes marking the detailed memory range, if any
    if(mw.allocBlockThreadId>=0) {
        double firstTimeNs = bsMinMax(mw.allocBlockStartTimeNs, mw.startTimeNs, mw.startTimeNs+mw.timeRangeNs);
        double lastTimeNs  = bsMinMax(mw.allocBlockEndTimeNs,   mw.startTimeNs, mw.startTimeNs+mw.timeRangeNs);
        if(firstTimeNs!=lastTimeNs || !(firstTimeNs==mw.startTimeNs || firstTimeNs==mw.startTimeNs+mw.timeRangeNs)) {
            const ImVec4  tmp         = cfg.getThreadColor(mw.allocBlockThreadId);
            const ImColor colorThread = ImColor(tmp.x, tmp.y, tmp.z, vwConst::MEM_BG_FOOTPRINT_ALPHA);
            double x1 = winX+xFactor*(firstTimeNs-mw.startTimeNs);
            double x2 = bsMax(x1+3., winX+xFactor*(lastTimeNs -mw.startTimeNs));
            DRAWLIST->AddRectFilled(ImVec2(x1, winY+1), ImVec2(x2, winY+winHeight-1), colorThread);
        }
    }

    // Overlay some text: total size and alloc quantity (overlay on previous drawings)
    const double mouseTimeToPix = winX+(_mouseTimeNs-mw.startTimeNs)*xFactor;
    if(mouseTimeToPix>=0. && mouseTimeToPix<winX+winWidth) {

        // Display the total allocated bytes in text on top of the window
        constexpr double xMargin = 8.;
        char tmpStr[64];
        snprintf(tmpStr, sizeof(tmpStr), "Total %s bytes in use", getNiceBigPositiveNumber(totalUsedBytes));
        double sWidth = ImGui::CalcTextSize(tmpStr).x;
        double y      = winY+fullHeaderHeight-vMargin;
        DRAWLIST->AddRectFilled(ImVec2(mouseTimeToPix-xMargin-sWidth, y), ImVec2(mouseTimeToPix-xMargin, y+ctx.fontHeightNoSpacing),
                                IM_COL32(32, 32, 32, 192));
        DRAWLIST->AddText(ImVec2(mouseTimeToPix-xMargin-sWidth, y), vwConst::uYellow, tmpStr);

        // Display the call quantities in text inside the alloc/dealloc top bars
        int callIdx = (int) ((double)bsDivCeil(winWidth, CALL_BIN_PIX)/mw.timeRangeNs*(_mouseTimeNs-mw.startTimeNs+mw.binTimeOffset));
        if(callIdx>=0 && callIdx<mw.cachedCallBins[0].size()) {
            int a = mw.cachedCallBins[0][callIdx], b = mw.cachedCallBins[1][callIdx];
            if(a+b>0) {
                if(a>0 && b>0) snprintf(tmpStr, sizeof(tmpStr),  "%d alloc%s / %d dealloc%s", a, (a>1)?"s":"", b, (b>1)?"s":"");
                else if(a>0)   snprintf(tmpStr, sizeof(tmpStr),  "%d alloc%s",   a, (a>1)?"s":"");
                else           snprintf(tmpStr, sizeof(tmpStr),  "%d dealloc%s", b, (b>1)?"s":"");
                sWidth = ImGui::CalcTextSize(tmpStr).x;
                DRAWLIST->AddRectFilled(ImVec2(mouseTimeToPix+xMargin, y), ImVec2(mouseTimeToPix+xMargin+sWidth, y+ctx.fontHeightNoSpacing),
                                        IM_COL32(32, 32, 32, 192));
                DRAWLIST->AddText(ImVec2(mouseTimeToPix+xMargin, y), vwConst::uYellow, tmpStr);
            }
        }
    }

    // Navigation
    // ==========

    ImGuiIO& io                = ImGui::GetIO();
    bool hasKeyboardFocus      = ctx.isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool changedNavigation     = false;
    mw.didUserChangedScrollPos = false;

    if(isWindowHovered || isBarHovered) {
        // Update the time of the mouse
        _mouseTimeNs = mw.startTimeNs + (mouseX-winX)/winWidth*mw.timeRangeNs;

        // Wheel input
        int deltaWheel = (int)io.MouseWheel;
        if(hasKeyboardFocus && ImGui::GetIO().KeyCtrl) {      // Ctrl-Up/Down keys are equivalent to the wheel
            if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
            if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
        }
        if(deltaWheel!=0) {
            // Ctrl: (Horizontal) range zoom
            if(io.KeyCtrl) {
                deltaWheel *= cfg.getHWheelInversion();
                const double scrollFactor = 1.25;
                double newTimeRangeNs = mw.getTimeRangeNs();
                while(deltaWheel>0) { newTimeRangeNs /= scrollFactor; --deltaWheel; }
                while(deltaWheel<0) { newTimeRangeNs *= scrollFactor; ++deltaWheel; }
                if(newTimeRangeNs<1000.) newTimeRangeNs = 1000.; // No point zooming more than this
                mw.setView(mw.getStartTimeNs()+(mouseX-winX)/winWidth*(mw.getTimeRangeNs()-newTimeRangeNs), newTimeRangeNs);
                changedNavigation = true;
            }

            // No Ctrl: (Vertical) Y scale zoom
            else {
                // Get the independent value (= tuple (thread, value inside thread)) corresponding to mouseY
                const double valueUnderMouse  = bsMinMax(mw.viewByteMin + (mouseY-winY-fullHeaderHeight)/ctx.yFactor, 0., ctx.viewByteMaxLimit);
                int mouseThreadId=0; double mouseThreadValue=0;
                ctx.getIvalueFromValue(mw, valueUnderMouse, mouseThreadId, mouseThreadValue);
                // Compute the new range
                const double scrollFactor = 1.25;
                double alpha = 1.;
                deltaWheel *= cfg.getVWheelInversion();
                while(deltaWheel>0) { alpha /= scrollFactor; --deltaWheel; }
                while(deltaWheel<0) { alpha *= scrollFactor; ++deltaWheel; }
                const double newValueRange   = bsMinMax(alpha*(mw.viewByteMax-mw.viewByteMin), 1., 1.05*ctx.viewByteMaxLimit);  // Slight overshoot

                // Compute the new viewByteMin and viewByteMax
                const double screenRatio = bsMinMax((mouseY-winY-fullHeaderHeight)/ctx.drawableHeight, 0., 1.);
                double newValueUnderMouse, newValueMaxLimit;
                ctx.getValueFromIValue(mw, mouseThreadId, mouseThreadValue, ctx.drawableHeight/newValueRange,
                                       newValueUnderMouse, newValueMaxLimit);

                mw.viewByteMin = newValueUnderMouse-     screenRatio*newValueRange;
                mw.viewByteMax = newValueUnderMouse+(1.-screenRatio)*newValueRange;
                ctx.viewByteMaxLimit = newValueMaxLimit;
                mw.lastWinHeight = 0.; // Invalidate the cache for scope fusion
                mw.didUserChangedScrollPos = true;
            }
        }
    }

    // Keys navigation
    double deltaMoveX=0., deltaMoveY=0.;
    if(hasKeyboardFocus) {
        if(!ImGui::GetIO().KeyCtrl) {
            if(ImGui::IsKeyPressed(KC_Up  ))  deltaMoveY = -0.25*(mw.viewByteMax-mw.viewByteMin);
            if(ImGui::IsKeyPressed(KC_Down))  deltaMoveY = +0.25*(mw.viewByteMax-mw.viewByteMin);
            if(ImGui::IsKeyPressed(KC_Left))  deltaMoveX = -0.25*mw.getTimeRangeNs();
            if(ImGui::IsKeyPressed(KC_Right)) deltaMoveX = +0.25*mw.getTimeRangeNs();
        }
        else { // Ctrl+up/down is handled by the mouse wheel code
            if(ImGui::IsKeyPressed(KC_Left))  deltaMoveX = -mw.getTimeRangeNs();
            if(ImGui::IsKeyPressed(KC_Right)) deltaMoveX = +mw.getTimeRangeNs();
        }
    }

    if(isWindowHovered && ImGui::IsMouseDragging(2) && (bsAbs(ImGui::GetMouseDragDelta(2).x)>1 || bsAbs(ImGui::GetMouseDragDelta(2).y)>1) &&
       !io.KeyCtrl && mw.dragMode==NONE) { // Data dragging (except for the navigation bar, handled in next section)
        mw.isDragging = true;
        deltaMoveX    = -ImGui::GetMouseDragDelta(2).x*mw.getTimeRangeNs()/winWidth;
        deltaMoveY    = -ImGui::GetMouseDragDelta(2).y/winHeight*(mw.viewByteMax-mw.viewByteMin);
        ImGui::ResetMouseDragDelta(2);
    }

    if(deltaMoveX!=0. || deltaMoveY!=0.) {
        // Update X coordinate
        mw.setView(mw.getStartTimeNs()+deltaMoveX, mw.getTimeRangeNs());
        changedNavigation = true;
        // Update Y coordinate
        if(mw.viewByteMin+deltaMoveY<0.) deltaMoveY = -mw.viewByteMin;
        if(mw.viewByteMax+deltaMoveY>ctx.viewByteMaxLimit) deltaMoveY = ctx.viewByteMaxLimit-mw.viewByteMax;
        mw.viewByteMin += deltaMoveY;
        mw.viewByteMax += deltaMoveY;
        mw.didUserChangedScrollPos = true;
    }

    // Draw visor, handle middle button drag (range selection) and timeline top bar drag
    if(manageVisorAndRangeSelectionAndBarDrag(mw, isWindowHovered, mouseX, mouseY, winX, winY, winWidth, winHeight,
                                              isBarHovered, rbWidth, rbStartPix, rbEndPix)) {
        changedNavigation = true;
    }

    // Synchronization
    if(changedNavigation) {
        synchronizeNewRange(mw.syncMode, mw.getStartTimeNs(), mw.getTimeRangeNs());
    }

    // Full screen
    if(hasKeyboardFocus && !ImGui::GetIO().KeyCtrl) {
        if(ImGui::IsKeyPressed(KC_F)) setFullScreenView(mw.uniqueId);
        if(ImGui::IsKeyPressed(KC_H)) openHelpTooltip(mw.uniqueId, "Help Memory");
    }

    // Contextual menu
    // ===============

    // Right click on curve: contextual menu

    if(isWindowHovered && mw.allocBlockThreadId>=0 && !mw.isDragging && ImGui::IsMouseReleased(2)) {
        ImGui::OpenPopup("Detail mem menu");
    }

    // Menu for a detail allocation scope (text list)
    if(ImGui::BeginPopup("Detail mem menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        s64 timeRangeNs = mw.allocBlockEndTimeNs-mw.allocBlockStartTimeNs;
        if(timeRangeNs<=0) {
            ImGui::TextColored(vwConst::gold, "All allocations present at time %s", getNiceTime(mw.allocBlockStartTimeNs, timeRangeNs, 0));
        } else {
            ImGui::TextColored(vwConst::gold, "Allocations from scope '%s' (%s -> %s)", mw.allocScopeName.toChar(),
                               getNiceTime(mw.allocBlockStartTimeNs, timeRangeNs, 0), getNiceTime(mw.allocBlockEndTimeNs, timeRangeNs, 1));
        }
        ImGui::Separator();
        ImGui::Separator();
        if(ImGui::MenuItem("Show allocation table"))  {
            // Create a new window to show the detailed scope list
            _memDetails.push_back( { mw.allocBlockThreadId, getId(), mw.allocBlockStartTimeNs, mw.allocBlockEndTimeNs,
                    mw.allocScopeName, mw.syncMode } );
            bsVec<MemAlloc>& dst = _memDetails.back().allocBlocks; dst.reserve(mw.rawAllocBlocks.size()/4);
            for(auto& rab : mw.rawAllocBlocks) {
                if(rab.vPtr!=PL_INVALID) { dst.push_back(rab); }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Help
    displayHelpTooltip(mw.uniqueId, "Help Memory",
                       "##Memory timeline view\n"
                       "===\n"
                       "Per thread representation of the memory allocations and usage.\n"
                       "A heat map for allocation/deallocation density highlights the hot spots\n"
                       "\n"
                       "##Actions:\n"
                       "-#H key#| This help\n"
                       "-#F key#| Full screen view\n"
                       "-#Right mouse button dragging#| Move\n"
                       "-#Left/Right key#| Move horizontally\n"
                       "-#Ctrl-Left/Right key#| Move horizontally faster\n"
                       "-#Up/Down key#| Move vertically\n"
                       "-#Middle mouse button dragging#| Measure/select a time range\n"
                       "-#Mouse wheel#| Value zoom\n"
                       "-#Ctrl-Up/Down key#| Time zoom\n"
                       "-#Ctrl-Mouse wheel#| Time zoom\n"
                       "-#Double left mouse click on graph#| Display the current allocations at that time\n"
                       "-#Right mouse click on thread bar#| New thread views, color configuration, expand/collapse threads\n"
                       "-#Ctrl-Left mouse button dragging on thread bar#| Move and reorder the thread/group \n"
                       "\n"
                       );

    if(!ImGui::IsMouseDragging(2)) mw.isDragging = false;

    ImGui::EndChild();
}


void
vwMain::drawMemoryDetailList(int detailWindowIdx)
{
    plgScope(MEM, "drawMemoryDetailList");
    plgData(MEM, "number", detailWindowIdx);

    MemDetailListWindow& mdl = _memDetails[detailWindowIdx];
    bsVec<MemAlloc>& data    = mdl.allocBlocks;
    bsVec<int>& lkup         = mdl.listDisplayIdx;

    // First run: populate the lookup and sort it for size (default)
    if(mdl.sortKind==-1) {
        lkup.reserve(data.size());
        for(int i=0; i<data.size(); ++i) lkup.push_back(i);
        mdl.sortKind = 0;
        std::sort(lkup.begin(), lkup.end(), [&data](const int a, const int b)->bool { return (data[a].size>data[b].size); });
    }
    plAssert(lkup.size()==data.size());

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x*3., style.CellPadding.y));
    if(ImGui::BeginTable("##table profile", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollX |
                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
        ImGui::TableSetupColumn("Byte size");
        ImGui::TableSetupColumn("Alloc location");
        ImGui::TableSetupColumn("Dealloc location");
        ImGui::TableSetupColumn("Alloc time");
        ImGui::TableSetupColumn("Dealloc time");
        ImGui::TableHeadersRow();

        // Sort files if required
        if(ImGuiTableSortSpecs* sortsSpecs= ImGui::TableGetSortSpecs()) {
            if(sortsSpecs->SpecsDirty) {
                if(!lkup.empty() && sortsSpecs->SpecsCount>0) {
                    s64 direction = (sortsSpecs->Specs->SortDirection==ImGuiSortDirection_Ascending)? 1 : -1;
#define RST(x) (((x)!=PL_INVALID)? _record->getString(x).alphabeticalOrder : -1)

                    if(sortsSpecs->Specs->ColumnIndex==0) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*((int)data[a].size-(int)data[b].size)<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==1) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, this, &data](const int a, const int b)->bool \
                        { return direction*((RST(data[a].startParentNameIdx)>RST(data[b].startParentNameIdx) ||
                                             (RST(data[a].startParentNameIdx)==RST(data[b].startParentNameIdx) &&
                                              RST(data[a].startNameIdx)>RST(data[b].startNameIdx))) ?1:-1)<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==2) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, this, &data](const int a, const int b)->bool \
                        { return direction*((data[a].endThreadId>data[b].endThreadId ||
                                             (data[a].endThreadId==data[b].endThreadId && data[b].endThreadId!=0xFFFF &&
                                              (RST(data[a].endParentNameIdx)>RST(data[b].endParentNameIdx) ||
                                               (RST(data[a].endParentNameIdx)==RST(data[b].endParentNameIdx) &&
                                                RST(data[a].endNameIdx)>RST(data[b].endNameIdx)))))?1:-1)<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==3) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(data[a].startTimeNs-data[b].startTimeNs)<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==4) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(((data[a].endTimeNs<0. && data[b].endTimeNs>=0.) ||
                                             (data[b].endTimeNs>=0 && data[a].endTimeNs-data[b].endTimeNs<=0))?1:-1)<=0; } );
                    }
                }
                sortsSpecs->SpecsDirty = false;
            }
        }

        // Table content
        char   tmpStr[128];
        ImGuiListClipper clipper; // Dear ImGui helper to handle arrays with large number of rows
        clipper.Begin(lkup.size());
        while(clipper.Step()) {
            for(int i=clipper.DisplayStart; i<clipper.DisplayEnd; ++i) {
                double targetTimeNs   = -1;
                int    targetThreadId = -1;
                // Byte size
                int dataIdx = lkup[i];
                const MemAlloc& d = data[dataIdx];
                ImGui::TableNextColumn();
                ImGui::Text("%s", getNiceBigPositiveNumber(d.size));

                // Alloc location
                bool hasDetailedName = !_record->getString(d.startNameIdx).value.empty();
                snprintf(tmpStr, sizeof(tmpStr), "%s%s%s",
                         (d.startParentNameIdx!=PL_INVALID)? _record->getString(d.startParentNameIdx).value.toChar() : "<root>",
                         hasDetailedName? "/":"", hasDetailedName? _record->getString(d.startNameIdx).value.toChar():"");
                ImGui::TableNextColumn();

                ImGui::Text("%s", tmpStr);
                if(ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tmpStr);
                    setScopeHighlight(mdl.threadId, d.startTimeNs, -1, d.startLevel-1, d.startParentNameIdx);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, vwConst::uDarkOrange);
                    targetThreadId = mdl.threadId;
                    targetTimeNs   = d.startTimeNs;
                }

                // Dealloc location
                ImGui::TableNextColumn();
                if(d.endTimeNs>=0) {
                    hasDetailedName = !_record->getString(d.endNameIdx).value.empty();
                    snprintf(tmpStr, sizeof(tmpStr), "[%s] %s%s%s", _record->getString(_record->threads[d.endThreadId].nameIdx).value.toChar(),
                             (d.endParentNameIdx!=PL_INVALID)? _record->getString(d.endParentNameIdx).value.toChar() : "<root>",
                             hasDetailedName?"/":"", hasDetailedName?_record->getString(d.endNameIdx).value.toChar():"");
                    ImGui::Text("%s", tmpStr);
                    if(ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tmpStr);
                        setScopeHighlight(d.endThreadId, d.endTimeNs, -1, d.endLevel-1, d.endParentNameIdx);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, vwConst::uDarkOrange);
                        targetThreadId = d.endThreadId;
                        targetTimeNs   = d.endTimeNs;
                    }
                }
                else ImGui::TextColored(vwConst::grey, "[Leaked]");

                // Alloc time
                ImGui::TableNextColumn();
                ImGui::Text("%s", getNiceTime(d.startTimeNs, 0));

                // Dealloc time
                ImGui::TableNextColumn();
                if(d.endTimeNs>=0) ImGui::Text("%s", getNiceTime(d.endTimeNs, 0));
                else               ImGui::TextColored(vwConst::grey, "[Leaked]");

                // Synchronized navigation
                if(targetTimeNs>=0 && mdl.syncMode>0) {
                    double syncStartTimeNs, syncTimeRangeNs;
                    getSynchronizedRange(mdl.syncMode, syncStartTimeNs, syncTimeRangeNs);
                    int tlWheelCounter = (!ImGui::GetIO().KeyCtrl)? 0 : (ImGui::GetIO().MouseWheel*getConfig().getHWheelInversion()); // Ctrl key: wheel is for the timeline

                    // Click: set timeline position at middle screen only if outside the center third of screen
                    if(ImGui::IsMouseReleased(0) || tlWheelCounter) {
                        synchronizeNewRange(mdl.syncMode, bsMax(0., targetTimeNs-0.5*syncTimeRangeNs), syncTimeRangeNs);
                        ensureThreadVisibility(mdl.syncMode, targetThreadId);
                        //synchronizeText(mdl.syncMode, targetThreadId, hlLevel, li.lIdx, li.scopeStartTimeNs, t.uniqueId);
                    }
                    // Zoom the timeline
                   if(tlWheelCounter!=0) {
                        double newTimeRangeNs = getUpdatedRange(tlWheelCounter, syncTimeRangeNs);
                        synchronizeNewRange(mdl.syncMode, syncStartTimeNs+(targetTimeNs-syncStartTimeNs)/syncTimeRangeNs*(syncTimeRangeNs-newTimeRangeNs),
                                            newTimeRangeNs);
                        ensureThreadVisibility(mdl.syncMode, targetThreadId);
                    }
                }


            } // End of clipper loop
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}
