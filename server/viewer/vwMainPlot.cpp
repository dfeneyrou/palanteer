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

// This file implements the plot view

// System
#include <math.h>
#include <inttypes.h>

// Internal
#include "cmRecord.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwConfig.h"


#ifndef PL_GROUP_PLOT
#define PL_GROUP_PLOT 0
#endif



constexpr double MIN_PIX_PER_POINT = 3.;

bsString
vwMain::PlotWindow::getDescr(void) const
{
    char tmpStr[512];
    int offset = snprintf(tmpStr, sizeof(tmpStr), "plot %d", syncMode);
    for(const PlotCurve& c : curves) offset += snprintf(tmpStr+offset, sizeof(tmpStr)-offset,
                                                        " %" PRIX64 " %" PRIX64, c.threadUniqueHash, c.hashPath);
    return tmpStr;
}


void
vwMain::preparePlot(PlotWindow& p)
{
    // Worth working?
    const double winWidth = bsMax(100.f, ImGui::GetWindowContentRegionMax().x);
    if(!p.isCacheDirty && p.lastWinWidth==winWidth) return;
    p.isCacheDirty = false;
    p.lastWinWidth = winWidth;
    plgScope(PLOT, "preparePlot");
    p.cachedItems.clear();
    p.curveNames.clear();
    p.curveThreadNames.clear();
    p.maxWidthCurveName  = ImGui::CalcTextSize(p.unit.toChar()).x;
    p.maxWidthThreadName = 0;
    char tmpStr[256];

    // Discover the potentially missing elem IDs (init and live)
    if(p.isFirstRun || _liveRecordUpdated) {
        p.isFirstRun = false;
        for(PlotCurve& c : p.curves) {
            if(c.elemIdx>=0) continue; // ElemId already known

            u64 threadHash = 0;
            for(cmRecord::Thread& t : _record->threads) {
                if(t.threadUniqueHash!=c.threadUniqueHash) continue;
                threadHash = t.threadHash;
                break;
            }
            if(threadHash==0 && c.threadUniqueHash!=0) continue; // Required thread is not resolved yet
            u64 hashPathWithThread = bsHashStep(threadHash, c.hashPath);

            for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
                cmRecord::Elem& elem = _record->elems[elemIdx];
                if((c.threadUniqueHash!=0 && elem.hashPath!=hashPathWithThread) ||
                   (c.threadUniqueHash==0 && elem.hashPath!=c.hashPath)) continue;
                c.elemIdx   = elemIdx;
                c.isEnabled = true;
                if(!p.isUnitSet) {
                    p.unit = _record->getString(_record->elems[elemIdx].nameIdx).unit;
                    if(p.unit.empty()) p.unit = getUnitFromFlags(_record->elems[elemIdx].flags);
                    p.isUnitSet = true;
                }
                break;
            }
        }
    }
    p.maxWidthCurveName  = ImGui::CalcTextSize(p.unit.toChar()).x;
    p.maxWidthThreadName = 0;

    // Loop on plot indexes
    for(PlotCurve& c : p.curves) {
        // Create the cache for this plot index
        p.cachedItems.push_back( { } );
        if(c.elemIdx<0) { // ElemID is not known yet, so we cannot retrieve any content
            p.curveNames.push_back("");
            p.curveThreadNames.push_back("");
             continue;
        }
        bsVec<PlotCachedPoint>& cache = p.cachedItems.back(); cache.reserve(1024);
        cmRecord::Elem& elem = _record->elems[c.elemIdx];
        const cmRecord::String& s = _record->getString(elem.nameIdx);
        c.isHexa = s.isHexa;
        int eType = elem.flags&PL_FLAG_TYPE_MASK;

        // Compute its name and thread names
        p.curveNames.push_back(getElemName(s.value, elem.flags));
        p.maxWidthCurveName  = bsMax(p.maxWidthCurveName,  (double)ImGui::CalcTextSize(p.curveNames.back().toChar()).x);
        snprintf(tmpStr, sizeof(tmpStr), " [%s]", (elem.threadId>=0)? getFullThreadName(elem.threadId) : "(all)");
        p.curveThreadNames.push_back(tmpStr);
        p.maxWidthThreadName = bsMax(p.maxWidthThreadName, (double)ImGui::CalcTextSize(tmpStr).x);
        double nsPerPix = MIN_PIX_PER_POINT*p.timeRangeNs/winWidth;

        // Fill it with data
        if(eType==PL_FLAG_TYPE_MARKER) { // Marker case (specific iterator)
            bool isCoarseScope = false; cmRecord::Evt evt;
            cmRecordIteratorMarker itMarker(_record, c.elemIdx, (s64)p.startTimeNs, nsPerPix);
            while(itMarker.getNextMarker(isCoarseScope, evt)) {
                double ptValue = evt.filenameIdx;
                cache.push_back( { evt.vS64, ptValue, PL_INVALID, evt } );
                c.absYMin = bsMin(c.absYMin, ptValue);
                c.absYMax = bsMax(c.absYMax, ptValue);
                if(evt.vS64>p.startTimeNs+p.timeRangeNs) break; // Time break at the end, as we want 1 point past the range
            }
        }
        else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) { // Lock notifier case (specific iterator)
            bool isCoarseScope = false; cmRecord::Evt evt;
            int nameIdx = elem.nameIdx;
            cmRecordIteratorLockNtf itLockNtf(_record, nameIdx, (s64)p.startTimeNs, nsPerPix);
            while(itLockNtf.getNextLock(isCoarseScope, evt)) {
                double ptValue = (double)evt.threadId;
                cache.push_back( { evt.vS64, ptValue, PL_INVALID, evt } );
                c.absYMin = bsMin(c.absYMin, ptValue);
                c.absYMax = bsMax(c.absYMax, ptValue);
                if(evt.vS64>p.startTimeNs+p.timeRangeNs) break; // Time break at the end, as we want 1 point past the range
           } // End of loop on lock notification events
        }
        else if(eType==PL_FLAG_TYPE_LOCK_ACQUIRED) { // Lock use case (specific iterator)
            cmRecordIteratorLockUseGraph it(_record, elem.threadId, elem.nameIdx, p.startTimeNs, nsPerPix);
            s64 ptTimeNs; double ptValue; cmRecord::Evt evt;
            while(it.getNextLock(ptTimeNs, ptValue, evt)) {
                cache.push_back( { ptTimeNs, ptValue, PL_INVALID, evt } );
                c.absYMin = bsMin(c.absYMin, ptValue);
                c.absYMax = bsMax(c.absYMax, ptValue);
                if(ptTimeNs>p.startTimeNs+p.timeRangeNs) break; // Time break at the end, as we want 1 point past the range
            } // End of loop on points
        }
        else { // Generic case
            cmRecordIteratorElem it(_record, c.elemIdx, p.startTimeNs, nsPerPix);
            u32 lIdx = PL_INVALID; s64 ptTimeNs; double ptValue; cmRecord::Evt evt;
            while((lIdx=it.getNextPoint(ptTimeNs, ptValue, evt))!=PL_INVALID) {
                cache.push_back( { ptTimeNs, ptValue, lIdx, evt } );
                c.absYMin = bsMin(c.absYMin, ptValue);
                c.absYMax = bsMax(c.absYMax, ptValue);
                if(ptTimeNs>p.startTimeNs+p.timeRangeNs) break; // Time break at the end, as we want 1 point past the range
            } // End of loop on points
        }

    } // End of loop on plot indexes
}


void
vwMain::drawPlots(void)
{
    if(!_record || _plots.empty()) return;
    plgScope(PLOT, "drawPlots");
    int itemToRemoveIdx = -1;

    for(int plotWindowIdx=0; plotWindowIdx<_plots.size(); ++plotWindowIdx) {
        PlotWindow& plot = _plots[plotWindowIdx];
        if(_liveRecordUpdated) plot.isCacheDirty = true;
        if(_uniqueIdFullScreen>=0 && plot.uniqueId!=_uniqueIdFullScreen) continue;

        if(plot.isWindowSelected) {
            plot.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }
        if(plot.isNew) {
            plot.isNew = false;
            if(plot.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(plot.newDockId);
            else selectBestDockLocation(true, false);
        }
        char tmpStr[64];
        snprintf(tmpStr, sizeof(tmpStr), "Plot #%d###%d", plot.uniqueId, plot.uniqueId);
        bool isOpen = true;
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
            drawPlot(plotWindowIdx);
        }
        ImGui::End();

        if(!isOpen) itemToRemoveIdx = plotWindowIdx;
    }

    // Remove window if needed
    if(itemToRemoveIdx>=0) {
        releaseId((_plots.begin()+itemToRemoveIdx)->uniqueId);
        _plots.erase(_plots.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::drawPlot(int curPlotWindowIdx)
{
    plgScope(PLOT, "drawPlot");
    PlotWindow&  pw = _plots[curPlotWindowIdx];

    // Ruler and visible range bar
    double rbWidth, rbStartPix, rbEndPix;
    double rulerHeight = getTimelineHeaderHeight(false, true);
    ImGui::BeginChild("ruler", ImVec2(0, 2.0*ImGui::GetStyle().WindowPadding.y+rulerHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
    const bool isBarHovered  = ImGui::IsWindowHovered();
    drawTimeRuler(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowContentRegionMax().x, rulerHeight,
                  pw.startTimeNs, pw.timeRangeNs, pw.syncMode, rbWidth, rbStartPix, rbEndPix);
    ImGui::EndChild();

    ImGui::BeginChild("plotArea", ImVec2(0,0), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar);  // We manage the wheel ourselves as the display area is virtual

    const double winX  = ImGui::GetWindowPos().x;
    const double winY  = ImGui::GetWindowPos().y;
    const double winWidth  = ImGui::GetWindowContentRegionMax().x;
    const double winHeight = bsMax(1.f, ImGui::GetWindowSize().y);
    const double mouseX  = ImGui::GetMousePos().x;
    const double mouseY  = ImGui::GetMousePos().y;
    const bool   isWindowHovered = ImGui::IsWindowHovered();
    const double fontHeight  = ImGui::GetTextLineHeightWithSpacing();
    const double vMargin = ImGui::GetTextLineHeight();

    // Prepare if cache is dirty (in case of removed curve for instance)
    preparePlot(pw);

    // Compute the maximum vertical range (depends on enabled curved)
    double valueMinLimit =  1e300;
    double valueMaxLimit = -1e300;
    for(int curveIdx=0; curveIdx<pw.cachedItems.size(); ++curveIdx) {
        if(!pw.curves[curveIdx].isEnabled) continue;
        valueMinLimit = bsMin(valueMinLimit, pw.curves[curveIdx].absYMin);
        valueMaxLimit = bsMax(valueMaxLimit, pw.curves[curveIdx].absYMax);
    }
    if     (valueMaxLimit==valueMinLimit) { valueMinLimit -= 1.; valueMaxLimit += 1.; } // Avoid null range
    else if(valueMaxLimit< valueMinLimit) { valueMinLimit  = 0.; valueMaxLimit  = 1.; } // Case range is invalid

    // Did the user click on the scrollbar? (detection based on an unexpected position change)
    float lastScrollPos = ImGui::GetScrollY();
    if(!pw.didUserChangedScrollPos && bsAbs(lastScrollPos-pw.lastScrollPos)>=1. && bsAbs(pw.lastWinHeight-winHeight)<=1.) {
        plgScope(PLOT, "New user scroll position from ImGui");
        plgData(PLOT, "expected pos", pw.lastScrollPos);
        plgData(PLOT, "new pos", lastScrollPos);
        float visibleRatio = (pw.valueMax-pw.valueMin)/(valueMaxLimit-valueMinLimit);
        float scrollMaxY   = winHeight/visibleRatio;
        double deltaY      = valueMinLimit-(lastScrollPos/scrollMaxY-1.)*(valueMaxLimit-valueMinLimit)  - pw.valueMax;
        deltaY = bsMin(deltaY, valueMaxLimit-pw.valueMax);
        deltaY = bsMax(deltaY, valueMinLimit-pw.valueMin);
        pw.valueMin += deltaY;
        pw.valueMax += deltaY;
    }

    // Sanity for the visible range
    if(pw.valueMin>=pw.valueMax)     { pw.valueMin = valueMinLimit; pw.valueMax = valueMaxLimit; }
    if(pw.valueMin<valueMinLimit)      pw.valueMin = valueMinLimit;
    if(pw.valueMax>valueMaxLimit)      pw.valueMax = valueMaxLimit;
    if(pw.valueMax<=pw.valueMin)     { pw.valueMin = 0.; pw.valueMax = 1.; }
    int typicalFlag = 0;
    for(PlotCurve& c : pw.curves) {
        if(c.elemIdx>=0) { typicalFlag = _record->elems[c.elemIdx].flags; break; }
    }
    int flagType = typicalFlag&PL_FLAG_TYPE_MASK;
    if(pw.valueMax-pw.valueMin<1. && // Zooming under the integer for these types is a non-sense
       (flagType==PL_FLAG_TYPE_DATA_STRING || flagType==PL_FLAG_TYPE_LOCK_NOTIFIED || (flagType>=PL_FLAG_TYPE_DATA_S32 && flagType<=PL_FLAG_TYPE_DATA_U64))) {
        s64 closestValue = (s64)(0.5*(pw.valueMax+pw.valueMin));
        pw.valueMin = (double)closestValue;
        pw.valueMax = pw.valueMin+1.;
    }

    // Handle animation (smooth move)
    pw.updateAnimation();

    // Previous navigation may have made dirty the cached data
    pw.checkTimeBounds(_record->durationNs);
    preparePlot(pw);
    float visibleRatio = (pw.valueMax-pw.valueMin)/(valueMaxLimit-valueMinLimit);
    float scrollMaxY   = winHeight/visibleRatio;

    // Set the modified scroll position in ImGui, if not changed through imGui
    if(pw.didUserChangedScrollPos || bsAbs(pw.lastWinHeight-winHeight)>1.) {
        float scrollPosY   = scrollMaxY*(valueMaxLimit-pw.valueMax)/(valueMaxLimit-valueMinLimit);
        plgData(PLOT, "Set new scroll pos from user", scrollPosY);
        plgData(PLOT, "Max possible pos", ImGui::GetScrollMaxY());
        ImGui::SetScrollY(scrollPosY);
    }
    // Mark the virtual total size
    pw.lastScrollPos = ImGui::GetScrollY();
    pw.lastWinHeight = winHeight;
    plgData(PLOT, "Current scroll pos", pw.lastScrollPos);
    plgData(PLOT, "Max scroll pos", scrollMaxY);
    plgData(PLOT, "Current max scroll pos", ImGui::GetScrollMaxY());
    ImGui::SetCursorPosY(scrollMaxY);

    // Some init
    const double xFactor = winWidth/pw.timeRangeNs;
    const double yFactor = (winHeight-2.*vMargin)/(pw.valueMax-pw.valueMin);
    double mouseTimeToPix = winX+(_mouseTimeNs-pw.startTimeNs)*xFactor;
    struct ClosePoint {
        double distanceX = 1e300;
        double distanceY = 1e300;
        PlotCachedPoint point;
        int    curveIdx = -1; // Used only for the global closest point
    };
    bsVec<ClosePoint> highlightedPoints(pw.cachedItems.size()); // Array of highlighted points for each curve (for external selection)
    bsVec<ClosePoint> closePoints      (pw.cachedItems.size()); // Array of closest point for each curve (for tooltip value)
    ClosePoint globalClosestPoint;
    char tmpStr[256];

    // Drawing
    // ========

    double yLowest = winY+winHeight-vMargin;

    // Grid (draw the major ticks only)
    double scaleMajorTick, scaleMinorTick;
    computeTickScales(pw.valueMax-pw.valueMin, bsMinMax(0.2*winHeight/getConfig().getFontSize(), 2., 9.),
                      scaleMajorTick, scaleMinorTick);
    double valueTick = scaleMajorTick*floor(pw.valueMin/scaleMajorTick);
    double pixTick   = yLowest-yFactor*(valueTick-pw.valueMin);
    if(yFactor*scaleMajorTick>0.) {
        while(pixTick>=winY) {
            DRAWLIST->AddLine(ImVec2(winX, pixTick), ImVec2(winX+winWidth, pixTick),
                              vwConst::uGrey128&0x3FFFFFFF, 1.0); // Quarter transparency);
            pixTick   -= yFactor*scaleMajorTick;
            valueTick += scaleMajorTick;
        }
    }

    // Loop on curves to draw them
    for(int curveIdx=0; curveIdx<pw.cachedItems.size(); ++curveIdx) {
        // Get elem on curve
        const PlotCurve& curve = pw.curves[curveIdx];
        if(curve.elemIdx<0) continue; // Not yet known
        cmRecord::Elem& elem  = _record->elems[curve.elemIdx];
        ImU32 color = getConfig().getCurveColor(curve.elemIdx, true);
        if(!pw.curves[curveIdx].isEnabled) continue;
        float pointSize = getConfig().getCurvePointSize(curve.elemIdx);
        vwConfig::CurveStyle style = getConfig().getCurveStyle(curve.elemIdx);
        ClosePoint& cp = closePoints[curveIdx];
        ClosePoint& hp = highlightedPoints[curveIdx];

        // Loop on points on the curve
        bool isFirst = true; double lastX=0., lastY=0.;
        for(const PlotCachedPoint& point : pw.cachedItems[curveIdx]) {
            // Get coordinates
            double x = winX+xFactor*(point.timeNs-pw.startTimeNs);
            double y = yLowest-yFactor*(point.value-pw.valueMin);

            // Draw the point
            if(style!=vwConfig::LOLLIPOP || y<=yLowest) {
                DRAWLIST->AddRectFilled(ImVec2(x-pointSize, y-pointSize), ImVec2(x+pointSize, y+pointSize), color);
            }

            // Update closest point per curve (using the mouse time, not the mouse position which may be in another window)
            if(bsAbs(x-mouseTimeToPix)<cp.distanceX) {
                cp.distanceX = bsAbs(x-mouseTimeToPix);
                if(isWindowHovered) cp.distanceY = bsAbs(y-mouseY); // If not hovered, it remains "too big"
                cp.point = point;
            }

            // Update global closest point
            if(isWindowHovered && bsAbs(x-mouseX)+bsAbs(y-mouseY)<20. &&
               (globalClosestPoint.curveIdx==-1 || bsAbs(x-mouseX)+bsAbs(y-mouseY)<globalClosestPoint.distanceX+globalClosestPoint.distanceY)) {
                globalClosestPoint = { bsAbs(x-mouseX), bsAbs(y-mouseY), point, curveIdx };
            }

            // Update the point to highlight (from external window)
            bool doHighlight =!isWindowHovered &&
                ((elem.nameIdx!=elem.hlNameIdx)?
                 isScopeHighlighted(elem.threadId, point.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx, false) :
                 isScopeHighlighted(elem.threadId, point.timeNs, elem.flags, elem.nestingLevel, elem.hlNameIdx, false));
            if(doHighlight && (hp.curveIdx==-1 || bsAbs(x-mouseX)+bsAbs(y-mouseY)<hp.distanceX+hp.distanceY)) {
                hp = { bsAbs(x-mouseX), bsAbs(y-mouseY), point, curveIdx };
            }

            // Draw the line
            if(style==vwConfig::LOLLIPOP && y<yLowest) {
                DRAWLIST->AddLine(ImVec2(x, yLowest), ImVec2(x, y), color, 1.5);
            }
            if(!isFirst) {
                if(style==vwConfig::LINE) {
                    DRAWLIST->AddLine(ImVec2(lastX, lastY), ImVec2(x, y), color, 1.5);
                } else if(style==vwConfig::STEP) {
                    DRAWLIST->AddLine(ImVec2(lastX, lastY), ImVec2(x, lastY), color, 1.5);
                    DRAWLIST->AddLine(ImVec2(x, lastY),     ImVec2(x, y),     color, 1.5);
                }
            }
            else isFirst = false;
            lastX = x; lastY = y;
        } // End of loop on points
    } // End of loop on curves

    // Draw extreme Y range values, and current one
    bool changedNavigation = false;
    if(!pw.cachedItems.empty() && typicalFlag!=0 && flagType!=PL_FLAG_TYPE_DATA_STRING && flagType!=PL_FLAG_TYPE_MARKER &&
       flagType!=PL_FLAG_TYPE_LOCK_NOTIFIED) { // Extreme range display for strings has no sense
        double yUnderMouse = pw.valueMin-(mouseY-winY-winHeight+vMargin)/yFactor;
        if(isWindowHovered) {
            const char* yString = getValueAsChar(typicalFlag, yUnderMouse, pw.valueMax-pw.valueMin, pw.curves[0].isHexa);
            double x = winX+winWidth-ImGui::CalcTextSize(yString).x;
            double y = winY+winHeight-vMargin-yFactor*(yUnderMouse-pw.valueMin);
            DRAWLIST->AddText(ImVec2(x, y), vwConst::uYellow, yString);
            DRAWLIST->AddLine(ImVec2(winX, y), ImVec2(x, y), vwConst::uYellow&0x3FFFFFFF, 1.0); // Quarter transparency
        }

        const char* valueMaxString = getValueAsChar(typicalFlag, pw.valueMax, pw.valueMax-pw.valueMin, pw.curves[0].isHexa);
        DRAWLIST->AddText(ImVec2(winX+winWidth-ImGui::CalcTextSize(valueMaxString).x,
                                 winY+vMargin), vwConst::uYellow, valueMaxString);

        const char* valueMinString = getValueAsChar(typicalFlag, pw.valueMin, pw.valueMax-pw.valueMin, pw.curves[0].isHexa);
        DRAWLIST->AddText(ImVec2(winX+winWidth-ImGui::CalcTextSize(valueMinString).x,
                                 winY+winHeight-vMargin), vwConst::uYellow, valueMinString);
    }


    // Draw visor, handle middle button drag (range selection) and timeline top bar drag
    if(manageVisorAndRangeSelectionAndBarDrag(pw, isWindowHovered, mouseX, mouseY, winX, winY, winWidth, winHeight,
                                              isBarHovered, rbWidth, rbStartPix, rbEndPix)) {
        changedNavigation = true;
    }

    // Draw legend
    {
        const double legendTextMargin = 5.;
        const double legendSegmWidth  = 25.;
        const double lineHeight       = fontHeight;
        const double legendWidth      = pw.maxWidthCurveName+pw.maxWidthThreadName+30/* ~ bracket size */+legendSegmWidth+3*legendTextMargin;
        const int    unitLineQty      = pw.unit.empty()? 0:1;
        const double legendHeight     = (pw.cachedItems.size()+unitLineQty)*lineHeight;
        double legendX = winX+pw.legendPosX*winWidth-0.5*legendWidth;
        double legendY = winY+pw.legendPosY*winHeight;

        // Draw the box
        DRAWLIST->AddRectFilled(ImVec2(legendX, legendY), ImVec2(legendX+legendWidth, legendY+legendHeight), IM_COL32(0,0,0,160));
        DRAWLIST->AddRect(ImVec2(legendX, legendY), ImVec2(legendX+legendWidth, legendY+legendHeight), vwConst::uWhite);
        // Draw the unit
        if(unitLineQty) {
            DRAWLIST->AddText(ImVec2(legendX+0.5*(legendWidth-ImGui::CalcTextSize(pw.unit.toChar()).x), legendY),
                              vwConst::uYellow, pw.unit.toChar());
            DRAWLIST->AddLine(ImVec2(legendX, legendY+lineHeight-2), ImVec2(legendX+legendWidth, legendY+lineHeight-2), vwConst::uWhite);
        }

        // Loop on curves in the legend
        for(int curveIdx=0; curveIdx<pw.cachedItems.size(); ++curveIdx) {
            const PlotCurve& curve = pw.curves[curveIdx];
            if(curve.elemIdx<0) continue; // Not yet known
            cmRecord::Elem& elem = _record->elems[pw.curves[curveIdx].elemIdx];
            int threadId = elem.threadId;
            ImU32 color  = getConfig().getCurveColor(curve.elemIdx, true);
            ImU32 colorThread = ImColor(getConfig().getThreadColor(threadId, true));
            if(!pw.curves[curveIdx].isEnabled) color = colorThread = vwConst::uGrey;
            double y = legendY+lineHeight*(curveIdx+unitLineQty);
            bool doHighlight = globalClosestPoint.curveIdx==curveIdx;

            // Draw the colored line
            DRAWLIST->AddLine(ImVec2(legendX+legendTextMargin, y+0.5*lineHeight),
                              ImVec2(legendX+legendTextMargin+legendSegmWidth, y+0.5*lineHeight), color, 2.5);

            // Draw the colored curve name
            float textStartPix = legendX+2*legendTextMargin+legendSegmWidth;
            DRAWLIST->AddText(ImVec2(textStartPix, y), doHighlight? vwConst::uWhite : color, pw.curveNames[curveIdx].toChar());

            // Draw the colored thread
            DRAWLIST->AddText(ImVec2(textStartPix+pw.maxWidthCurveName+15, y), colorThread, pw.curveThreadNames[curveIdx].toChar());

            // Legend item hovered?
            if(isWindowHovered && mouseX>=legendX && mouseX<=legendX+legendWidth && mouseY>=y && mouseY<=y+lineHeight) {
                // Double click: toggle curve display
                if(ImGui::IsMouseDoubleClicked(0)) {
                    // Toggle
                    pw.curves[curveIdx].isEnabled = !pw.curves[curveIdx].isEnabled;
                    // Update the Y range
                    if(pw.curves[curveIdx].isEnabled) {
                        pw.valueMin = bsMin(pw.valueMin, pw.curves[curveIdx].absYMin);
                        pw.valueMax = bsMax(pw.valueMax, pw.curves[curveIdx].absYMax);
                    }
                }

                // Right click: contextual menu
                if(pw.legendDragMode==NONE && ImGui::IsMouseReleased(2)) {
                    // Curve contextual menu
                    ImGui::OpenPopup("Plot curve menu");
                    _plotMenuItems.clear();
                    _plotMenuSpecificCurveIdx = curveIdx;
                    prepareGraphContextualMenu(pw.curves[curveIdx].elemIdx, (s64)pw.getStartTimeNs(), (s64)pw.getTimeRangeNs(), false, true);
                }

                // Tooltip: build the full path
                if(pw.legendDragMode==NONE && getLastMouseMoveDurationUs()>500000) {
                    int pathQty = 1;
                    int path[cmConst::MAX_LEVEL_QTY+1] = {curve.elemIdx};
                    while(pathQty<cmConst::MAX_LEVEL_QTY+1 && path[pathQty-1]>=0) { path[pathQty] = _record->elems[path[pathQty-1]].prevElemIdx; ++pathQty; }
                    int offset = snprintf(tmpStr, sizeof(tmpStr), "[%s] ", (threadId>=0)? getFullThreadName(threadId) : "(all)");
                    for(int i=pathQty-2; i>=0; --i) {
                        offset += snprintf(tmpStr+offset, sizeof(tmpStr)-offset, "%s>", _record->getString(_record->elems[path[i]].nameIdx).value.toChar());
                    }
                    tmpStr[offset-1] = 0; // Remove the last '>'
                    ImGui::SetTooltip("%s", tmpStr);
                }

            } // End of legend item hovered
        } // End of loop on curves in the legend

        // Dragging
        if(isWindowHovered) {
            if(mouseX>=legendX && mouseX<=legendX+legendWidth && mouseY>=legendY && mouseY<=legendY+legendHeight && pw.legendDragMode==NONE && ImGui::IsMouseDragging(2)) {
                pw.legendDragMode = DATA;
            }
            if(pw.legendDragMode==DATA) {
                if(ImGui::IsMouseDragging(2)) {
                    pw.legendPosX = bsMinMax(pw.legendPosX+ImGui::GetMouseDragDelta(2).x/winWidth, 0., 0.9);
                    pw.legendPosY = bsMinMax(pw.legendPosY+ImGui::GetMouseDragDelta(2).y/winHeight, 0., 0.9);
                    ImGui::ResetMouseDragDelta(2);
                } else pw.legendDragMode = NONE;
            }
        }

    } // End of legend drawing

    // Manage highlights and tooltips
    const double fontHeightNoSpacing = ImGui::GetTextLineHeight();
    for(int i=0; i<closePoints.size(); ++i) {
        if(closePoints[i].distanceX>100.) continue;
        // Display a small colored box with the value
        const PlotCachedPoint& pcp = closePoints[i].point;
        const PlotCurve& curve = pw.curves[i];
        double x = winX+xFactor*(pcp.timeNs-pw.startTimeNs);
        double y = winY+winHeight-vMargin-yFactor*(bsMinMax(pcp.value, pw.valueMin, pw.valueMax)-pw.valueMin)-fontHeightNoSpacing;
        const char* s = getValueAsChar(typicalFlag, pcp.value, pw.valueMax-pw.valueMin, curve.isHexa);
        double sWidth = ImGui::CalcTextSize(s).x;
        ImU32 color = getConfig().getCurveColor(curve.elemIdx, false);
        DRAWLIST->AddRectFilled(ImVec2(x+5, y), ImVec2(x+5+sWidth, y+fontHeightNoSpacing), color);
        DRAWLIST->AddText(ImVec2(x+5, y), vwConst::uWhite, s);
    }

    // Highlight selected points (after curve drawing to ensure that highlight is visible)
    for(int i=-1; i<highlightedPoints.size(); ++i) {
        ClosePoint& hp = (i==-1)? globalClosestPoint :  highlightedPoints[i]; // So that to handle also the global closest point
        if(hp.curveIdx<0) continue;
        const PlotCachedPoint& pcp = hp.point;
        const PlotCurve& curve = pw.curves[hp.curveIdx];
        const double hlPointHSize = 1.5*getConfig().getCurvePointSize(curve.elemIdx);
        double x = winX+xFactor*(pcp.timeNs-pw.startTimeNs);
        double y = winY+winHeight-vMargin-yFactor*(bsMinMax(pcp.value, pw.valueMin, pw.valueMax)-pw.valueMin);
        // Add a rectangle on the highlighted point
        DRAWLIST->AddRectFilled(ImVec2(x-hlPointHSize, y-hlPointHSize), ImVec2(x+hlPointHSize, y+hlPointHSize), vwConst::uWhite);
    }

    // Hovered window and closest point: highlight it externally
    if(globalClosestPoint.curveIdx>=0) {
        // Highlight in other windows
        const PlotCurve& curve = pw.curves[globalClosestPoint.curveIdx];
        int closestPlotIdx = curve.elemIdx;
        cmRecord::Elem& elem  = _record->elems[closestPlotIdx];
        int nestingLevel = elem.nestingLevel;
        int nameIdx      = elem.nameIdx;
        int flags        = elem.flags;
        const PlotCachedPoint& pcp = globalClosestPoint.point;

        // Highlight in other windows
        if(elem.nameIdx!=elem.hlNameIdx) // "Flat" event, so we highlight its block scope
            setScopeHighlight(pcp.evt.threadId, pcp.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx);
        else
            setScopeHighlight(pcp.evt.threadId, pcp.timeNs, elem.flags, elem.nestingLevel, elem.hlNameIdx);

        // Manage tooltip
        if(ImGui::IsMouseReleased(0) && pw.dragMode==NONE) {
            // Display is toggle when clicking on the highlighted point
            pw.doShowPointTooltip = !pw.doShowPointTooltip;
            // Synchronize the text (after getting the nesting level and lIdx for this date on this thread)
            int nestingLevel;
            u32 lIdx;
            cmGetRecordPosition(_record, pcp.evt.threadId, pcp.timeNs, nestingLevel, lIdx);
            synchronizeText(pw.syncMode, pcp.evt.threadId, nestingLevel, lIdx, pcp.timeNs, pw.uniqueId);
            ensureThreadVisibility(pw.syncMode, pcp.evt.threadId);
        }
        // Show the tooltip
        if(pw.doShowPointTooltip) {
            char titleStr[256];
            u64  durationNs = 1;
            _workDataChildren.clear();
            if(pcp.evt.flags&PL_FLAG_SCOPE_BEGIN) { // Case scope: build title and collect the children
                durationNs = (s64)pcp.value;
                snprintf(titleStr, sizeof(titleStr), "%s { %s }", _record->getString(nameIdx).value.toChar(), getNiceDuration(durationNs));
                cmRecordIteratorScope it(_record, pcp.evt.threadId, nestingLevel, pcp.lIdx);
                it.getChildren(pcp.evt.linkLIdx, pcp.lIdx, false, false, true, _workDataChildren, _workLIdxChildren);
            }
            else { // Case non-scope: just build the title
                snprintf(titleStr, sizeof(titleStr), "%s { %s }", _record->getString(nameIdx).value.toChar(),
                         getValueAsChar(flags, pcp.value, pw.valueMax-pw.valueMin, curve.isHexa));
            }
            // Display the tooltip
            displayScopeTooltip(titleStr, _workDataChildren, pcp.evt, durationNs); // durationNs is used only in case of scope with children
        }

        if(pw.syncMode>0 && ImGui::IsMouseDoubleClicked(0)) {
            double newTimeRangeNs = 0.;
            if   (pcp.lIdx==PL_INVALID) { } // Marker case (we do not know the parent, so no duration)
            else if(elem.nameIdx==elem.hlNameIdx) newTimeRangeNs = vwConst::DCLICK_RANGE_FACTOR*pcp.value; // For scopes, the value is the duration
            else {
                // For "flat" items, the duration is the one of the parent
                cmRecordIteratorHierarchy it(_record, pcp.evt.threadId, nestingLevel, pcp.lIdx);
                newTimeRangeNs = vwConst::DCLICK_RANGE_FACTOR*it.getParentDurationNs();
            }
            if(newTimeRangeNs>0.) {
                double newStartTimeNs = bsMax(0., pw.startTimeNs+(pcp.timeNs-pw.startTimeNs)/pw.timeRangeNs*(pw.timeRangeNs-newTimeRangeNs));
                pw.setView(newStartTimeNs, newTimeRangeNs);
                changedNavigation = true;
            }
        }
    }
    else { // Disable point tooltip
        pw.doShowPointTooltip = false;
    }

    // Navigation
    // ==========

    ImGuiIO& io           = ImGui::GetIO();
    bool hasKeyboardFocus = isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    pw.didUserChangedScrollPos = false;
    if(isWindowHovered || isBarHovered) {
        // Update the time of the mouse
        _mouseTimeNs = pw.startTimeNs + (mouseX-winX)/winWidth*pw.timeRangeNs;

        // Wheel input
        int deltaWheel = (int)io.MouseWheel;
        if(hasKeyboardFocus) {
            if(ImGui::GetIO().KeyCtrl) {      // Ctrl-Up/Down keys are equivalent to the wheel
                if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
                if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
            }
            else {
                if(ImGui::IsKeyPressed(KC_H)) openHelpTooltip(pw.uniqueId, "Help Plot");
            }
        }
        if(deltaWheel!=0) {
            // Ctrl: (Horizontal) range zoom
            if(io.KeyCtrl) {
                deltaWheel *= getConfig().getHWheelInversion();
                constexpr double scrollFactor = 1.25;
                double newTimeRangeNs = pw.getTimeRangeNs();
                while(deltaWheel>0) { newTimeRangeNs /= scrollFactor; --deltaWheel; }
                while(deltaWheel<0) { newTimeRangeNs *= scrollFactor; ++deltaWheel; }
                if(newTimeRangeNs<1000.) newTimeRangeNs = 1000.; // No point zooming more than this
                pw.setView(pw.getStartTimeNs()+(mouseX-winX)/winWidth*(pw.getTimeRangeNs()-newTimeRangeNs), newTimeRangeNs);
                changedNavigation = true;
            }
            // No Ctrl: (Vertical) Y scale zoom
            else {
                deltaWheel *= getConfig().getVWheelInversion();
                // Get the Y corresponding to the mouseY
                const double yFactor2 = (winHeight-2.*vMargin)/(pw.valueMax-pw.valueMin);
                double yUnderMouse = bsMinMax(pw.valueMin-(mouseY-winY-winHeight+vMargin)/yFactor2, valueMinLimit, valueMaxLimit);
                // Compute the new range
                const double scrollFactor = 1.25;
                double alpha = 1.;
                while(deltaWheel>0) { alpha /= scrollFactor; --deltaWheel; }
                while(deltaWheel<0) { alpha *= scrollFactor; ++deltaWheel; }
                double newYRange   = alpha*(pw.valueMax-pw.valueMin);
                // Compute the new valueMin and valueMax
                double screenRatio = (pw.valueMax-yUnderMouse)/(pw.valueMax-pw.valueMin); // So that point under mouse stays fixed on screen while zooming
                pw.valueMin = yUnderMouse-(1.-screenRatio)*newYRange;
                pw.valueMax = yUnderMouse+screenRatio     *newYRange;
                if(pw.valueMin<valueMinLimit) { pw.valueMax += valueMinLimit-pw.valueMin; pw.valueMin = valueMinLimit; }
                if(pw.valueMax>valueMaxLimit) { pw.valueMin += valueMaxLimit-pw.valueMax; pw.valueMax = valueMaxLimit; }
                pw.valueMin = bsMax(pw.valueMin, valueMinLimit);
                pw.valueMax = bsMin(pw.valueMax, valueMaxLimit);
                pw.didUserChangedScrollPos = true;
            }
            dirty();
        }
    }

    // Keys navigation
    double deltaMoveX=0., deltaMoveY=0.;
    if(hasKeyboardFocus) {
        if(!ImGui::GetIO().KeyCtrl) {
            if(ImGui::IsKeyPressed(KC_Up  ))  deltaMoveY = +0.25*(pw.valueMax-pw.valueMin);
            if(ImGui::IsKeyPressed(KC_Down))  deltaMoveY = -0.25*(pw.valueMax-pw.valueMin);
            if(ImGui::IsKeyPressed(KC_Left))  deltaMoveX = -0.25*pw.getTimeRangeNs();
            if(ImGui::IsKeyPressed(KC_Right)) deltaMoveX = +0.25*pw.getTimeRangeNs();
        }
        else { // Ctrl+up/down is handled by the mouse wheel code
            if(ImGui::IsKeyPressed(KC_Left))  deltaMoveX = -pw.getTimeRangeNs();
            if(ImGui::IsKeyPressed(KC_Right)) deltaMoveX = +pw.getTimeRangeNs();
        }
    }

    if(isWindowHovered && pw.dragMode==NONE && ImGui::IsMouseDragging(2) && (bsAbs(ImGui::GetMouseDragDelta(2).x)>1 || bsAbs(ImGui::GetMouseDragDelta(2).y)>1)) { // Data dragging (except for the navigation bar, handled after drawn)
        deltaMoveX = -ImGui::GetMouseDragDelta(2).x*pw.getTimeRangeNs()/winWidth;
        deltaMoveY =  ImGui::GetMouseDragDelta(2).y/winHeight*(pw.valueMax-pw.valueMin);
        ImGui::ResetMouseDragDelta(2);
    }

    if(deltaMoveX!=0. || deltaMoveY!=0.) {
        // Update X coordinate
        pw.setView(pw.getStartTimeNs()+deltaMoveX, pw.getTimeRangeNs());
        changedNavigation = true;
        // Update Y coordinate
        if(pw.valueMin+deltaMoveY<valueMinLimit) deltaMoveY = valueMinLimit-pw.valueMin;
        if(pw.valueMax+deltaMoveY>valueMaxLimit) deltaMoveY = valueMaxLimit-pw.valueMax;
        pw.valueMin += deltaMoveY;
        pw.valueMax += deltaMoveY;
        pw.didUserChangedScrollPos = true;
    }

    // Full screen
    if(isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(pw.uniqueId);
    }

    // Synchronize windows
    if(changedNavigation) {
        synchronizeNewRange(pw.syncMode, pw.getStartTimeNs(), pw.getTimeRangeNs());
    }


    // Contextual menu
    // ===============

    // Curve contextual menu for configuration (curve type, color, etc...)
    if(ImGui::BeginPopup("Plot curve menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Point size ").x+5;
        double widgetWidth = 1.5*ImGui::CalcTextSize("Lollipop XXX").x;
        PlotCurve& curve = pw.curves[_plotMenuSpecificCurveIdx];
        cmRecord::Elem& elem = _record->elems[curve.elemIdx];

        // Title
        ImGui::TextColored(vwConst::grey, "Curve '%s'", _record->getString(elem.nameIdx).value.toChar());
        ImGui::Separator();
        ImGui::Separator();

        // Plot move/removal
        if(!displayPlotContextualMenu(elem.threadId, "Move", headerWidth, widgetWidth)) {
            // Remove from current plot window the plot with non-void action (moved or removed)
            for(auto& pmi : _plotMenuItems) {
                if(pmi.comboSelectionExistingIdx<0 && pmi.comboSelectionNewIdx<0 && !pmi.comboSelectionRemoval) continue;
                PlotWindow& pw2 = _plots[curPlotWindowIdx]; // Caution: "pw" may be invalidated due to new added plot windows
                for(int i=0; i<pw2.curves.size(); ++i) {
                    if(pw2.curves[i].elemIdx!=pmi.elemIdx) continue;
                    pw2.curves.erase(pw2.curves.begin()+i);
                    pw2.isCacheDirty = true;
                    break;
                }
            }
            ImGui::CloseCurrentPopup();
        }

        // Histogram
        if(!displayHistoContextualMenu(headerWidth, widgetWidth)) ImGui::CloseCurrentPopup();

        ImGui::Separator();

        // Color
        std::function<void(int)> curveSetColor = [&curve, this] (int colorIdx) { getConfig().setCurveColorIdx(curve.elemIdx, colorIdx); };
        displayColorSelectMenu("Color", getConfig().getCurveColorIdx(curve.elemIdx), curveSetColor);
        // Style configuration
        ImGui::Text("Style "); ImGui::SameLine(headerWidth);
        ImGui::PushItemWidth(widgetWidth);
        int curveStyle = (int)getConfig().getCurveStyle(curve.elemIdx);
        if(ImGui::Combo("##Plot style", &curveStyle, "Line\0Step\0Point\0Lollipop\0\0")) {
            getConfig().setCurveStyle(curve.elemIdx, (vwConfig::CurveStyle)curveStyle);
        }
        // Point size
        ImGui::Text("Point size"); ImGui::SameLine(headerWidth);
        int pointSize = (int)getConfig().getCurvePointSize(curve.elemIdx);
        if(ImGui::SliderInt("##Point size", &pointSize, 1, 10, "%d", ImGuiSliderFlags_ClampOnInput)) {
            pointSize = bsMinMax(pointSize, 1, 10);
            getConfig().setCurvePointSize(curve.elemIdx, pointSize);
        }

        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }

    // Help
    displayHelpTooltip(pw.uniqueId, "Help Plot",
                       "##Plot view\n"
                       "===\n"
                       "Instantaneous plot of any event kind.\n"
                       "May contain several curves as long as they share the same unit.\n"
                       "\n"
                       "##Actions:\n"
                       "-#H key#| This help\n"
                       "-#F key#| Full screen view\n"
                       "-#Right mouse button dragging#| Move\n"
                       "-#Right mouse button dragging on legend#| Move the legend\n"
                       "-#Middle mouse button dragging#| Select/measure a time range\n"
                       "-#Left/Right key#| Move horizontally\n"
                       "-#Ctrl-Left/Right key#| Move  horizontally faster\n"
                       "-#Up/Down key#| Move vertically\n"
                       "-#Mouse wheel#| Value zoom\n"
                       "-#Ctrl-Up/Down key#| Time zoom views of the same group\n"
                       "-#Ctrl-Mouse wheel#| Time zoom views of the same group\n"
                       "-#Left mouse click on point#| Time synchronize views of the same group + display details\n"
                       "-#Double left mouse click on point#| Time and range synchronize views of the same group\n"
                       "-#Double left mouse click on legend#| Enable/disable the curve under the mouse\n"
                       "-#Right mouse click on legend#| Open menu for curve configuration, move and histogram\n"
                       "\n"
                       );

    ImGui::EndChild();
}
