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

// This file implements the histogram view

// System
#include <cinttypes>

// Internal
#include "bsKeycode.h"
#include "cmRecord.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwConfig.h"


#ifndef PL_GROUP_HISTO
#define PL_GROUP_HISTO 0
#endif

// Some constants
static const int    MAX_BIN_QTY     = 20000; // Implicitely defines the maximum resolution
static const double MIN_BAR_PIX_QTY = 5.;
static const double MIN_BAR_QTY     = 2.;
static const double MIN_BAR_HEIGHT  = 3.;

// @#TODO Check if we work on "all names" or only one... What to do for this??

bsString
vwMain::Histogram::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "histogram %d %" PRIX64 " %" PRIX64, syncMode, threadUniqueHash, hashPath);
    return tmpStr;
}

// elemIdx may be negative if not known yet (case of saved layout)
bool
vwMain::addHistogram(int id, u64 threadUniqueHash, u64 hashPath, int elemIdx, s64 startTimeNs, s64 timeRangeNs)
{
    // Sanity
    if(!_record) return false;
    plScope("addHistogram");
    plgVar(HISTO, threadUniqueHash, hashPath, elemIdx, startTimeNs, getNiceDuration(timeRangeNs));

    // Add the half-initialized histogram entry
    if(elemIdx<0) {
        _histograms.push_back( { id, elemIdx, threadUniqueHash, hashPath, "(Not present)", startTimeNs, timeRangeNs, 0, false } );
    } else {
        char tmpStr[256];
        cmRecord::Elem& elem = _record->elems[elemIdx];
        snprintf(tmpStr, sizeof(tmpStr), "%s [%s]", getElemName(_record->getString(elem.nameIdx).value.toChar(), elem.flags),
                 (elem.threadId>=0)? getFullThreadName(elem.threadId) : "(all)");
        _histograms.push_back( { id, elemIdx, threadUniqueHash, hashPath, tmpStr, startTimeNs, timeRangeNs, 0, false } );
    }
    setFullScreenView(-1);
    plMarker("user", "Add a histogram");
    return true;
}


bool
vwMain::_computeChunkHistogram(Histogram& h)
{
    // Need to work?
    if(h.computationLevel>=100) return true;
    if(h.computationLevel==0 && _backgroundComputationInUse) return true; // Waiting for a free slot

    // Finish the initialization if needed (init and live)
    if(h.elemIdx<0 && (h.isFirstRun || _liveRecordUpdated)) {
        h.isFirstRun = false;

        u64 threadHash = 0;
        for(cmRecord::Thread& t : _record->threads) {
            if(t.threadUniqueHash!=h.threadUniqueHash) continue;
            threadHash = t.threadHash;
            break;
        }
        if(threadHash==0 && h.threadUniqueHash!=0) return true; // Required thread is not resolved yet
        u64 hashPathWithThread = bsHashStep(threadHash, h.hashPath);

        // Find the elem
        for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
            cmRecord::Elem& elem = _record->elems[elemIdx];
            if((h.threadUniqueHash!=0 && elem.hashPath!=hashPathWithThread) ||
               (h.threadUniqueHash==0 && elem.hashPath!=h.hashPath)) continue;
            // Complete the histogram initialization
            char tmpStr[256];
            snprintf(tmpStr, sizeof(tmpStr), "%s [%s]", getElemName(_record->getString(elem.nameIdx).value.toChar(), elem.flags),
                     (elem.threadId>=0)? getFullThreadName(elem.threadId) : "(all)");
            h.elemIdx = elemIdx;
            h.name    = tmpStr;
            h.isHexa  = _record->getString(elem.nameIdx).isHexa;
            return true;  // We do not do a first chunk computation now so that ImGui stack is consistent for the progress dialog
        }
    }
    if(h.elemIdx<0) return true; // Elem is not resolved yet

    // Bootstrap the computation
    dirty();
    cmRecord::Elem& elem  = _record->elems[h.elemIdx];
    bsVec<HistoData>& frd = h.fullResData;
    if(h.computationLevel==0) {
        frd.resize(MAX_BIN_QTY);
        _histoBuild.absMinValue =  1e300;
        _histoBuild.absMaxValue = -1e300;
        if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER) { // Marker case (specific iterator)
            _histoBuild.itMarker.init(_record, h.elemIdx, h.startTimeNs, 0.);
        } else if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_NOTIFIED) { // Lock notif case (specific iterator)
            _histoBuild.itLockNtf.init(_record, elem.nameIdx, h.startTimeNs, 0.);
        } else if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_ACQUIRED) { // Lock use case (specific iterator)
            _histoBuild.itLockUse.init(_record, elem.threadId, elem.nameIdx, h.startTimeNs, 0.);
        } else { // Generic case
            _histoBuild.itGen.init(_record, h.elemIdx, h.startTimeNs, 0.);
        }
        _histoBuild.maxValuePerBin.resize(MAX_BIN_QTY);
        for(int i=0; i<MAX_BIN_QTY; ++i) {
            frd[i] = {0, 0, -1, 0, -1LL};
            _histoBuild.maxValuePerBin[i] = -1e300;
        }

        // Clear fields
        _backgroundComputationInUse = true;
        h.isCacheDirty     = true;
        h.viewZoom         = 1.;
        h.viewStartX       = 0.;
        h.fsCumulFactor    = -1.;
        h.rangeSelStartIdx = 0;
        h.rangeSelEndIdx   = 0;
        h.totalQty         = 0;
        ImGui::OpenPopup("In progress##WaitHistogram");
    }

    // Get infos on the elem
    bool isDiscrete  = ((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_DATA_STRING || (elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER); // And supposedly small range
    double yToBinIdx = isDiscrete? 1. : (double)MAX_BIN_QTY/bsMax(1e-300, elem.absYMax-elem.absYMin); // Discrete values shall stay unmodified

    bsUs_t endComputationTimeUs = bsGetClockUs() + vwConst::COMPUTATION_TIME_SLICE_US; // Time slice of computation
    double absMinValue = _histoBuild.absMinValue, absMaxValue = _histoBuild.absMaxValue;
    double ptValue; s64 ptTimeNs = 0; cmRecord::Evt evt; u32 lIdx = PL_INVALID;
    bool   isCoarseScope;
    char   tmpStr[64];

    // Collect data
    bool isFinished = true;
    if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER) { // Marker case (specific iterator)
        while(_histoBuild.itMarker.getNextMarker(isCoarseScope, evt)) {
            ptValue = evt.filenameIdx;

            // Get the bin index, if time range matches
            if(evt.vS64<h.startTimeNs) continue;
            if(evt.vS64>h.startTimeNs+h.timeRangeNs) break; // Stop if time is past
            int idx = bsMinMax((int)((ptValue-elem.absYMin)*yToBinIdx+0.5), 0, MAX_BIN_QTY-1);

            // Update the bin & global statistics
            frd[idx].qty++;
            if(ptValue>_histoBuild.maxValuePerBin[idx]) {
                _histoBuild.maxValuePerBin[idx] = ptValue;
                frd[idx].timeNs   = evt.vS64;
                frd[idx].threadId = evt.threadId;
                frd[idx].lIdx     = PL_INVALID;
            }
            if(ptValue<absMinValue) absMinValue = ptValue;
            if(ptValue>absMaxValue) absMaxValue = ptValue;

            // End of computation time slice?
            h.computationLevel = (int)bsMinMax(100.*(evt.vS64-h.startTimeNs)/h.timeRangeNs, 1., 99.); // 0 means just started, 100 means finished

            if(bsGetClockUs()>endComputationTimeUs) {
                isFinished = false;
                break;
            }
        }
    }
    else if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_NOTIFIED) { // Lock notif case (specific iterator)
        while(_histoBuild.itLockNtf.getNextLock(isCoarseScope, evt)) {
            // Get the bin index, if time range matches
            if(evt.vS64<h.startTimeNs) continue;
            if(evt.vS64>h.startTimeNs+h.timeRangeNs) break; // Stop if time is past
            ptValue = evt.threadId;  // For the lock notification, the value is the thread id (what else?)
            int idx = bsMinMax((int)((ptValue-elem.absYMin)*yToBinIdx+0.5), 0, MAX_BIN_QTY-1);

            // Update the bin & global statistics
            frd[idx].qty++;
            if(ptValue>_histoBuild.maxValuePerBin[idx]) {
                _histoBuild.maxValuePerBin[idx] = ptValue;
                frd[idx].timeNs   = evt.vS64;
                frd[idx].threadId = evt.threadId;
                frd[idx].lIdx     = PL_INVALID;
            }
            if(ptValue<absMinValue) absMinValue = ptValue;
            if(ptValue>absMaxValue) absMaxValue = ptValue;

            // End of computation time slice?
            h.computationLevel = (int)bsMinMax(100.*(evt.vS64-h.startTimeNs)/h.timeRangeNs, 1., 99.); // 0 means just started, 100 means finished

            if(bsGetClockUs()>endComputationTimeUs) {
                isFinished = false;
                break;
            }
        }
    }
    else if((elem.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_ACQUIRED) { // Lock use case (specific iterator)

        while(_histoBuild.itLockUse.getNextLock(ptTimeNs, ptValue, evt)) {
            // Get the bin index, if time range matches
            if(ptTimeNs<h.startTimeNs) continue;
            if(ptTimeNs>h.startTimeNs+h.timeRangeNs) break; // Stop if time is past
            int idx = bsMinMax((int)((ptValue-elem.absYMin)*yToBinIdx+0.5), 0, MAX_BIN_QTY-1);

            // Update the bin & global statistics
            frd[idx].qty++;
            if(ptValue>_histoBuild.maxValuePerBin[idx]) {
                _histoBuild.maxValuePerBin[idx] = ptValue;
                frd[idx].timeNs   = ptTimeNs;
                frd[idx].threadId = evt.threadId;
                frd[idx].lIdx     = PL_INVALID;
            }
            if(ptValue<absMinValue) absMinValue = ptValue;
            if(ptValue>absMaxValue) absMaxValue = ptValue;

            // End of computation time slice?
            h.computationLevel = (int)bsMinMax(100.*(ptTimeNs-h.startTimeNs)/h.timeRangeNs, 1., 99.); // 0 means just started, 100 means finished

            if(bsGetClockUs()>endComputationTimeUs) {
                isFinished = false;
                break;
            }
        }
    }
    else {
        while((lIdx=_histoBuild.itGen.getNextPoint(ptTimeNs, ptValue, evt))!=PL_INVALID) {
            // Get the bin index, if time range matches
            if(ptTimeNs<h.startTimeNs) continue;
            if(ptTimeNs>h.startTimeNs+h.timeRangeNs) break; // Stop if time is past
            int idx = bsMinMax((int)((ptValue-elem.absYMin)*yToBinIdx+0.5), 0, MAX_BIN_QTY-1);

            // Update the bin & global statistics
            frd[idx].qty++;
            if(ptValue>_histoBuild.maxValuePerBin[idx]) {
                _histoBuild.maxValuePerBin[idx] = ptValue;
                frd[idx].timeNs   = ptTimeNs;
                frd[idx].threadId = evt.threadId;
                frd[idx].lIdx     = lIdx;
            }
            if(ptValue<absMinValue) absMinValue = ptValue;
            if(ptValue>absMaxValue) absMaxValue = ptValue;

            // End of computation time slice?
            h.computationLevel = (int)bsMinMax(100.*(ptTimeNs-h.startTimeNs)/h.timeRangeNs, 1., 99.); // 0 means just started, 100 means finished

            if(bsGetClockUs()>endComputationTimeUs) {
                isFinished = false;
                break;
            }
        }
    }

    // Save bound updates in the persistent build structure
    _histoBuild.absMinValue = absMinValue;
    _histoBuild.absMaxValue = absMaxValue;

    // Computations are finished?
    if(ptTimeNs>h.startTimeNs+h.timeRangeNs || isFinished) h.computationLevel = 100;

    bool openPopupModal = true;
    if(ImGui::BeginPopupModal("In progress##WaitHistogram",
                              &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(vwConst::gold, "Histogram computation...");
        snprintf(tmpStr, sizeof(tmpStr), "%d %%", h.computationLevel);
        ImGui::ProgressBar(0.01f*h.computationLevel, ImVec2(-1,ImGui::GetTextLineHeight()), tmpStr);
        if(h.computationLevel==100) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(!openPopupModal) {
        _backgroundComputationInUse = false;
        return false;  // False: Cancelled by used
    }
    if(h.computationLevel<100) return true; // Not finished
    _backgroundComputationInUse = false;

    // Finalize the histogram
    if(absMinValue>absMaxValue) { absMinValue = absMaxValue = 0.; }
    h.absMinValue = absMinValue;
    h.absMaxValue = absMaxValue;

    // Get index bounds and stats (computed on the bins for efficiency)
    int firstUsedBin = -1, lastUsedBin = -1, totalUsedBinQty = 0;
    for(int idx=0; idx<MAX_BIN_QTY; ++idx) {
        if(frd[idx].qty==0) continue;
        h.totalQty += frd[idx].qty;
        if(firstUsedBin<0) firstUsedBin = idx;
        lastUsedBin = idx;
        ++totalUsedBinQty;
    }

    // Shrink the raw data array (only if partial range)
    int rangeUsedBin = lastUsedBin+1-firstUsedBin;
    if(firstUsedBin>0) memmove(&frd[0], &frd[firstUsedBin], rangeUsedBin*sizeof(HistoData));
    frd.resize(rangeUsedBin);

    // Special process for discrete values
    if(isDiscrete) {
        // Fill the constant data array with packed value. Initial values are stored in discreteLkup
        h.data        .reserve(totalUsedBinQty);
        h.discreteLkup.reserve(totalUsedBinQty);
        h.maxQty = 0;
        for(int i=0; i<rangeUsedBin; ++i) {
            if(frd[i].qty==0) continue;
            // Add the next non null discrete value
            h.data.push_back(frd[i]);
            h.discreteLkup.push_back((int)h.absMinValue+i);
            // Compute the cumulative value
            HistoData& hd = h.data.back();
            hd.cumulQty   = hd.qty + ((h.data.size()==1)? 0 : h.data[h.data.size()-2].cumulQty);
            if(hd.qty>h.maxQty) h.maxQty = hd.qty;
        }
    }

    return true;
}


void
vwMain::Histogram::checkBounds(void)
{
    // Sanity
    const int fullResBinQty = fullResData.size();
    if(fsCumulFactor<=0.) fsCumulFactor = fullResBinQty/50.; // Initial value gives ~50 bins
    fsCumulFactor = bsMax(fsCumulFactor, 1.);
    viewZoom = bsMinMax(viewZoom, 1., (double)fullResBinQty/MIN_BAR_QTY); // No unzoom and no zoom more than MIN_BAR bins

    // Enforce minimum bar width
    fsCumulFactor = bsMax(fsCumulFactor, MIN_BAR_PIX_QTY*fullResBinQty/(viewZoom*bsMax(ImGui::GetWindowSize().x, 300.)));

    // Enfore maximum visible bar quantity
    fsCumulFactor = bsMin(fsCumulFactor, bsMax(1., (double)fullResBinQty/(MIN_BAR_QTY*viewZoom)));
}


void
vwMain::prepareHistogram(Histogram& h)
{
    // Worth working?
    const float winWidth = ImGui::GetWindowSize().x;
    if(!h.isCacheDirty && winWidth>=h.lastWinWidth) return;
    plgScope(HISTO, "prepareHistogram");
    h.lastWinWidth = winWidth;
    h.isCacheDirty = false;
    if(!h.discreteLkup.empty()) return; // Cache is constant for discrete values

    // Compute the bin quantity from the full resolution cumul factor
    h.checkBounds();
    const int fullResBinQty = h.fullResData.size();
    const int realBinQty = bsDivCeil(fullResBinQty, (int)h.fsCumulFactor);
    plAssert(realBinQty>0, realBinQty, h.viewZoom, h.fsCumulFactor);
    plAssert(realBinQty<=fullResBinQty, realBinQty, h.viewZoom, h.fsCumulFactor);

    // Compute the bin content
    h.data.clear(); h.data.resize(realBinQty);
    for(int i=0; i<realBinQty; ++i) {
        h.data[i] = {0, 0, 0, 0};
    }
    h.maxQty = 0;
    for(int i=0; i<fullResBinQty; ++i) {
        HistoData& hd   = h.data[i/(int)h.fsCumulFactor];
        HistoData& hdfr = h.fullResData[i];
        if(hdfr.qty==0) continue;
        hd.qty     += hdfr.qty;
        hd.threadId = hdfr.threadId;
        hd.lIdx     = hdfr.lIdx;
        hd.timeNs   = hdfr.timeNs; // Highest value by design
        if(hd.qty>h.maxQty) h.maxQty = hd.qty;
    }

    // Compute the cumulative quantities
    h.data[0].cumulQty = h.data[0].qty;
    for(int i=1; i<realBinQty; ++i) h.data[i].cumulQty = h.data[i-1].cumulQty+h.data[i].qty;
}


void
vwMain::drawHistograms(void)
{
    if(!_record || _histograms.empty()) return;
    plgScope(HISTO, "drawHistograms");
    int itemToRemoveIdx = -1;

    for(int histogramIdx=0; histogramIdx<_histograms.size(); ++histogramIdx) {
        Histogram& histogram = _histograms[histogramIdx];

        if(!_computeChunkHistogram(histogram)) {
            // Cancelled by user: remove this histogram from the list
            itemToRemoveIdx = histogramIdx;
            continue;
        }

        if(_liveRecordUpdated) histogram.isCacheDirty = true;
        if(_uniqueIdFullScreen>=0 && histogram.uniqueId!=_uniqueIdFullScreen) continue;
        if(histogram.isNew) {
            histogram.isNew = false;
            if(histogram.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(histogram.newDockId);
            else selectBestDockLocation(true, false);
        }
        if(histogram.isWindowSelected) {
            histogram.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }

        char tmpStr[64];
        snprintf(tmpStr, sizeof(tmpStr), "Histogram %s###%d", histogram.name.toChar(), histogram.uniqueId);
        bool isOpen = true;
        if(ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs)) {
            drawHistogram(histogramIdx);
        }
        ImGui::End();

        if(!isOpen) itemToRemoveIdx = histogramIdx;
    }

    // Remove window if needed
    if(itemToRemoveIdx>=0) {
        releaseId((_histograms.begin()+itemToRemoveIdx)->uniqueId);
        _histograms.erase(_histograms.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::drawHistogram(int histogramIdx)
{
    plgScope(HISTO, "drawHistogram");
    if(_histograms[histogramIdx].computationLevel<100) return; // Computations are not finished

    ImGui::BeginChild("histoArea", ImVec2(0.,0.), false, ImGuiWindowFlags_NoScrollbar);

    const float winX      = ImGui::GetWindowPos().x;
    const float winY      = ImGui::GetWindowPos().y;
    const float winWidth  = ImGui::GetWindowSize().x;
    const float winHeight = bsMax(ImGui::GetWindowSize().y, 1.f);
    const float mouseX    = ImGui::GetMousePos().x;
    const float mouseY    = ImGui::GetMousePos().y;
    const bool   isWindowHovered = ImGui::IsWindowHovered();
    const float fontHeight   = ImGui::GetTextLineHeight();
    const float topBarHeight = ImGui::GetTextLineHeightWithSpacing();
    const float uMargin   = 5.f;
    const float vMargin   = 10.f;
    const float pointSize = 3.f;

    Histogram& h = _histograms[histogramIdx];
    prepareHistogram(h); // Ensure cache is up to date, even at window creation
    const bool isDiscrete   = !h.discreteLkup.empty();
    const int fullResBinQty = h.fullResData.size();
    cmRecord::Elem& elem    = _record->elems[h.elemIdx];
    int   eType             = elem.flags & PL_FLAG_TYPE_MASK;
    ImU32 colorDark         = getConfig().getCurveColor(h.elemIdx, false);
    ImU32 colorLight        = getConfig().getCurveColor(h.elemIdx, true);

    // compute some drawing parameters (which may be altered by the navigation, so updated before drawing)
    float scrollX       = (float)h.viewStartX;
    float barTotalWidth = (float)(h.viewZoom*winWidth-2.f*uMargin)/h.data.size();
    int    firstBarIdx  = bsMax((int)(scrollX/barTotalWidth), 0);
    int    lastBarIdx   = bsMin((int)((scrollX+winWidth)/barTotalWidth), h.data.size()-1);

    // Draw the top horizontal bar with the synchronization groups
    DRAWLIST->AddRectFilled(ImVec2(winX, winY), ImVec2(winX+winWidth, winY+topBarHeight), vwConst::uGrey);
    float comboWidth = ImGui::CalcTextSize("Isolated XXX").x;
    ImGui::SetCursorPos(ImVec2(winWidth-comboWidth, 0.));
    drawSynchroGroupCombo(comboWidth, &h.syncMode);

    // Visible range bar
    float rbBgStartPix = winX;
    float rbWidth      = winWidth-comboWidth;
    float rbStartPix   = rbBgStartPix+(rbWidth-3.f)*firstBarIdx/h.data.size();
    float rbEndPix     = rbStartPix+bsMax(3.f, ((lastBarIdx+1-firstBarIdx)*rbWidth/h.data.size()));
    DRAWLIST->AddRectFilled(ImVec2(rbStartPix, winY+4.f),
                            ImVec2(rbEndPix,   winY+topBarHeight-4.f), vwConst::uGrey128);

    // Navigation
    bool hasKeyboardFocus   = isWindowHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    double targetFsBinIndex = -1.; // Set and used only if user changes scroll position manually
    if(isWindowHovered) {
        // Wheel input
        ImGuiIO& io    = ImGui::GetIO();
        int deltaWheel = (int)io.MouseWheel;
        if(hasKeyboardFocus) {      // Up/Down keys are equivalent to the wheel
            if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
            if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
            if(!io.KeyCtrl && ImGui::IsKeyPressed(KC_H)) openHelpTooltip(h.uniqueId, "Help Histogram");
        }
        if(deltaWheel!=0) {
            const float scrollFactor = 1.25f;
            deltaWheel *= getConfig().getHWheelInversion();
            // Ctrl: (Horizontal) range zoom
            if(io.KeyCtrl) {
                // Keep the position under the mouse at the same place
                targetFsBinIndex = (mouseX-winX+scrollX-uMargin-0.5*barTotalWidth)/barTotalWidth*fullResBinQty/h.data.size();
                // Update the view parameters
                while(deltaWheel>0) { h.viewZoom = bsMin(h.viewZoom*scrollFactor, (double)fullResBinQty/MIN_BAR_QTY); --deltaWheel; }
                while(deltaWheel<0) { h.viewZoom = bsMax(h.viewZoom/scrollFactor, 1.); ++deltaWheel; }
                h.checkBounds();
            }
            // No Ctrl: Resolution zoom
            else {
                // Compute the new accumulation factor (so indirectly the bin quantity)
                while(deltaWheel>0) { h.fsCumulFactor /= scrollFactor; --deltaWheel; }
                while(deltaWheel<0) { h.fsCumulFactor *= scrollFactor; ++deltaWheel; }
                h.checkBounds();
            }
            h.isCacheDirty = true;
            dirty();
        }

        // Navigation from range bar (just set the start time)
        if(h.dragMode==BAR || (h.dragMode==NONE && h.legendDragMode==NONE && mouseY<winY+topBarHeight)) {
            if(ImGui::IsMouseDragging(2)) {
                if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1.) {
                    h.viewStartX = bsMinMax(h.viewStartX+h.viewZoom*winWidth*ImGui::GetMouseDragDelta(2).x/rbWidth, 0., (h.viewZoom-1.)*winWidth);
                    ImGui::ResetMouseDragDelta(2);
                    h.dragMode = BAR;
                }
            }
            // Else just set the middle time if click outside of the bar
            else if(ImGui::IsMouseDown(0) && mouseX<rbBgStartPix+rbWidth && (mouseX<rbStartPix || mouseX>rbEndPix)) {
                h.viewStartX = bsMinMax(h.viewZoom*winWidth*(mouseX-rbBgStartPix)/rbWidth-0.5*winWidth, 0., (h.viewZoom-1.)*winWidth);
                h.dragMode       = BAR;
            }
            else h.dragMode = NONE;
        }

        // Dragging on drawn histogram @#TODO Have a complete drag automata like for other windows
        if(isWindowHovered && h.dragMode==NONE && h.legendDragMode==NONE && ImGui::IsMouseDragging(2) && bsAbs(ImGui::GetMouseDragDelta(2).x)>1.) {
            h.viewStartX = bsMinMax(h.viewStartX-ImGui::GetMouseDragDelta(2).x, 0., (h.viewZoom-1.)*winWidth);
            ImGui::ResetMouseDragDelta(2);
            dirty();
        }

        if(hasKeyboardFocus) {
            float step = winWidth*(ImGui::GetIO().KeyCtrl? 1.f : 0.25f);
            if(ImGui::IsKeyPressed(KC_Left))  h.viewStartX = bsMinMax(h.viewStartX-step, 0., (h.viewZoom-1.)*winWidth);
            if(ImGui::IsKeyPressed(KC_Right)) h.viewStartX = bsMinMax(h.viewStartX+step, 0., (h.viewZoom-1.)*winWidth);
        }
    } // End of hovered window

    // Set the modified scroll position in ImGui, if not changed through imGui
    if(targetFsBinIndex>=0) {
        const double newBarTotalWidth  = (h.viewZoom*winWidth-2.*uMargin)/h.data.size();
        h.viewStartX = targetFsBinIndex*(h.viewZoom*winWidth-2.*uMargin)/fullResBinQty+winX+uMargin-mouseX+0.5f*newBarTotalWidth;
    }
    h.viewStartX = bsMinMax(h.viewStartX, 0., (h.viewZoom-1.)*winWidth);


    // Update the cache if needed
    prepareHistogram(h);
    scrollX        = (float)h.viewStartX;
    barTotalWidth  = (float)(h.viewZoom*winWidth-2.*uMargin)/h.data.size();
    firstBarIdx    = bsMax((int)(scrollX/barTotalWidth), 0);
    lastBarIdx     = bsMin((int)((scrollX+winWidth)/barTotalWidth), h.data.size()-1);

    // Compute drawing parameters
    const float halfBarSpacing = bsMax(0.09f*barTotalWidth, 1.f);
    const float yLowest        = winY+winHeight-fontHeight;
    const float yHistFactor    = (yLowest-winY-topBarHeight-vMargin)/(float)h.maxQty;
    const float yCumulFactor   = (yLowest-winY-topBarHeight-vMargin)/(float)h.totalQty;
    const float yDelta         = (float)bsMax((h.absMaxValue-h.absMinValue)/bsMax(1, h.data.size()-1), 1e-300);
    const u32   doubleMedianQty = ((firstBarIdx==0)? 0: h.data[firstBarIdx-1].cumulQty) + h.data[lastBarIdx].cumulQty; // Double value to keep precision

    // Draw the grid
    double scaleMajorTick, scaleMinorTick;
    computeTickScales((double)h.maxQty, bsMinMax((int)(0.2f*winHeight/getConfig().getFontSize()), 2, 12),
                      scaleMajorTick, scaleMinorTick);
    double valueTick = 0.;
    float pixTick   = (float)(yLowest-yHistFactor*valueTick);
    if(yHistFactor*scaleMajorTick>0.) {
        while(pixTick>=winY) {
            DRAWLIST->AddLine(ImVec2(winX, pixTick), ImVec2(winX+winWidth, pixTick),
                              vwConst::uGrey128&0x3FFFFFFF, 1.0); // Quarter transparency);
            pixTick   -= (float)(yHistFactor*scaleMajorTick);
            valueTick += scaleMajorTick;
        }
    }

    // Highlighted index
    int highlightedIdx = (int)((mouseX-winX+scrollX-uMargin-0.5*barTotalWidth)/barTotalWidth+0.5);
    if(!isWindowHovered || highlightedIdx<0 || highlightedIdx>=h.data.size()) highlightedIdx = -1;
    if(highlightedIdx>=0) { // Refine to hit only hovering the primitives (cumulative point and drawn bar)
        const HistoData& hd = h.data[highlightedIdx];
        if(hd.qty==0 ||
           (mouseY<yLowest-bsMax(yHistFactor*hd.qty, MIN_BAR_HEIGHT) && bsAbs(mouseY-(yLowest-yCumulFactor*hd.cumulQty))>1.5*pointSize) ||
           (mouseX<winX-scrollX+uMargin+highlightedIdx*barTotalWidth+halfBarSpacing || mouseX>winX-scrollX+uMargin+(highlightedIdx+1)*barTotalWidth-halfBarSpacing)) {
            highlightedIdx = -1;
        }
    }

    // Draw the histogram bars
    int medianIdx = -1;
    double averageValue = 0.; int averageCount = 0;
    for(int barIdx=firstBarIdx; barIdx<=lastBarIdx; ++barIdx) {
        const HistoData& hd = h.data[barIdx];
        if(hd.qty==0) continue;
        // External highlight?
        if(!isWindowHovered && highlightedIdx==-1) {
            if(elem.nameIdx!=elem.hlNameIdx) { // "Flat" event, so we highlight its block scope
                if(isScopeHighlighted(hd.threadId, hd.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx, false)) highlightedIdx = barIdx;
            } else {
                if(isScopeHighlighted(hd.threadId, hd.timeNs, elem.flags, elem.nestingLevel, elem.hlNameIdx, false)) highlightedIdx = barIdx;
            }
        }
        // Draw
        float x = winX-scrollX+uMargin+barIdx*barTotalWidth+halfBarSpacing;
        float y = yLowest-bsMax(yHistFactor*hd.qty, MIN_BAR_HEIGHT);
        DRAWLIST->AddRect      (ImVec2(x+barTotalWidth-2.f*halfBarSpacing, yLowest), ImVec2(x, y), colorLight);
        DRAWLIST->AddRectFilled(ImVec2(x+barTotalWidth-2.f*halfBarSpacing, yLowest), ImVec2(x, y),
                                (barIdx==highlightedIdx)? vwConst::uWhite : colorDark);
        // Update median & average
        double value = (h.absMinValue+yDelta*barIdx);
        averageValue += value*hd.qty;
        averageCount += hd.qty;
        if(medianIdx<0 && 2*hd.cumulQty>=doubleMedianQty) medianIdx = barIdx;
    }
    if(medianIdx<0) medianIdx = firstBarIdx; // Will not be used (empty bin), but valid index
    DRAWLIST->AddLine(ImVec2(winX, yLowest-1.f), ImVec2(winX+winWidth, yLowest-1.f), vwConst::uGrey);

    // Draw the cumulative probability
    float lastX=-1.f, lastY=-1.f;
    int firstCumulIdx = bsMax(firstBarIdx-1, 0);
    while(firstCumulIdx>0 && h.data[firstCumulIdx].qty==0) --firstCumulIdx;
    int lastCumulIdx  = bsMin(lastBarIdx+1, h.data.size()-1);
    while(lastCumulIdx<h.data.size()-1 && h.data[lastCumulIdx].qty==0) ++lastCumulIdx;
    for(int barIdx=firstCumulIdx; barIdx<=lastCumulIdx; ++barIdx) {
        const HistoData& hd = h.data[barIdx];
        if(hd.qty==0) continue; // No point if no data
        float x = winX-scrollX+uMargin+(0.5f+barIdx)*barTotalWidth;
        float y = yLowest-yCumulFactor*hd.cumulQty;
        if(barIdx>firstCumulIdx) DRAWLIST->AddLine(ImVec2(lastX, lastY), ImVec2(x, y), vwConst::uGrey, 2.);
        DRAWLIST->AddRectFilled(ImVec2(x-pointSize, y-pointSize), ImVec2(x+pointSize, y+pointSize),
                                (barIdx==highlightedIdx)? vwConst::uGrey128 : vwConst::uGrey);
        lastX = x; lastY = y;
    }

    // Draw item names in case of strings
    char  tmpStr[256];
    ImU32 textBg = IM_COL32(32, 32, 32, 128);
    if(isDiscrete) {
        for(int barIdx=firstBarIdx; barIdx<=lastBarIdx; ++barIdx) {
            snprintf(tmpStr, sizeof(tmpStr), "%s", getValueAsChar(elem.flags, (double)h.discreteLkup[barIdx], 0., h.isHexa));
            float x = winX-scrollX+uMargin+(0.5f+barIdx)*barTotalWidth-0.5f*ImGui::CalcTextSize(tmpStr).x;
            DRAWLIST->AddText(ImVec2(x+5.f, yLowest), vwConst::uYellow, tmpStr);
        }
    }
    // Draw the horizontal extreme X-axis in other cases
    else if(h.data[0].qty>0) {
        const char* valueMinString = getValueAsChar(elem.flags, h.absMinValue+yDelta*firstBarIdx, 0., h.isHexa);
        DRAWLIST->AddText(ImVec2(winX+5, yLowest), vwConst::uYellow, valueMinString);
        const char* valueMaxString = getValueAsChar(elem.flags, h.absMinValue+yDelta*lastBarIdx, 0., h.isHexa);
        DRAWLIST->AddText(ImVec2(winX+winWidth-ImGui::CalcTextSize(valueMaxString).x-2.f, yLowest), vwConst::uYellow, valueMaxString);
    }

    // Draw average and median on the window
    if(!isDiscrete && averageCount>0) {
        double avgValue = averageValue/(double)averageCount;
        snprintf(tmpStr, sizeof(tmpStr), "Avg: %s", getValueAsChar(elem.flags, avgValue, 0., h.isHexa));
        float sWidth = ImGui::CalcTextSize(tmpStr).x;
        double avgIdx = (avgValue-h.absMinValue)/yDelta;
        float x = winX-scrollX+uMargin+(float)(avgIdx*barTotalWidth)+0.5f*barTotalWidth;
        float y = winY+topBarHeight+4.f*fontHeight;
        DRAWLIST->AddLine(ImVec2(x, winY+topBarHeight), ImVec2(x, yLowest), vwConst::uCyan, 1.f);
        DRAWLIST->AddRectFilled(ImVec2(x+3.f, y), ImVec2(x+sWidth+8.f, y+fontHeight), textBg);
        DRAWLIST->AddText(ImVec2(x+5.f, y), vwConst::uCyan, tmpStr);
    }
    if(h.data[medianIdx].qty>0) {
        snprintf(tmpStr, sizeof(tmpStr), "Median: %s", getValueAsChar(elem.flags, isDiscrete?
                                                                      (double)h.discreteLkup[medianIdx] : h.absMinValue+yDelta*medianIdx,
                                                                      0., h.isHexa));
        float sWidth = ImGui::CalcTextSize(tmpStr).x;
        float x = winX-scrollX+uMargin+medianIdx*barTotalWidth+0.5f*barTotalWidth;
        float y = winY+topBarHeight+6.f*fontHeight;
        DRAWLIST->AddLine(ImVec2(x, winY+topBarHeight), ImVec2(x, yLowest), vwConst::uRed, 1.f);
        DRAWLIST->AddRectFilled(ImVec2(x+3.f, y), ImVec2(x+sWidth+8.f, y+fontHeight), textBg);
        DRAWLIST->AddText(ImVec2(x+5.f, y), vwConst::uRed, tmpStr);
    }

    // Highlight
    if(isWindowHovered && highlightedIdx>=0) {
        const HistoData& hd = h.data[highlightedIdx];

        // Highlight in other windows
        if(elem.nameIdx!=elem.hlNameIdx) // "Flat" event, so we highlight its block scope
            setScopeHighlight(hd.threadId, hd.timeNs, PL_FLAG_SCOPE_BEGIN|PL_FLAG_TYPE_DATA_TIMESTAMP, elem.nestingLevel-1, elem.hlNameIdx);
        else
            setScopeHighlight(hd.threadId, hd.timeNs, elem.flags, elem.nestingLevel, elem.hlNameIdx);

        // Tooltip
        bsString deltaString;
        if(eType!=PL_FLAG_TYPE_DATA_STRING && eType!=PL_FLAG_TYPE_MARKER && eType!=PL_FLAG_TYPE_LOCK_NOTIFIED) {
            deltaString = bsString(" +/-") + getValueAsChar(elem.flags, 0.5f*yDelta, 0., h.isHexa); // 0.5 because "plus or minus"
        }
        snprintf(tmpStr, sizeof(tmpStr),  "%s { %s%s }", h.name.toChar(),
                 getValueAsChar(elem.flags, isDiscrete? (double)h.discreteLkup[highlightedIdx] : h.absMinValue+yDelta*(0.5+highlightedIdx), 0., h.isHexa),
                 deltaString.toChar());
        float ttWidth = bsMax(ImGui::CalcTextSize(tmpStr).x, 3.f*ImGui::CalcTextSize(" Cumulative: ").x);
        ImGui::SetNextWindowSize(ImVec2(ttWidth+20.f, 0));
        ImGui::BeginTooltip();

        ImGui::TextColored(vwConst::gold, "%s", tmpStr);
        ImGui::Separator();
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x*3.f, style.CellPadding.y));
        if(ImGui::BeginTable("##tooltipHist", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextColumn();
            ImGui::Text("Quantity:"); ImGui::TableNextColumn();
            ImGui::Text("%d", hd.qty);  ImGui::TableNextColumn();
            ImGui::Text("%.2f%%", 100.*hd.qty/h.totalQty);  ImGui::TableNextColumn();
            ImGui::Text("Cumulative:"); ImGui::TableNextColumn();
            ImGui::Text("%d", hd.cumulQty);  ImGui::TableNextColumn();
            ImGui::Text("%.2f%%", 100.*hd.cumulQty/h.totalQty);
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndTooltip();

        // Synchronized navigation
        if(h.syncMode>0 && (ImGui::IsMouseDoubleClicked(0) || (ImGui::IsMouseClicked(0) && h.dragMode==NONE))) { // No synchronized navigation for isolated windows
            s64 syncStartTimeNs, newTimeRangeNs;
            getSynchronizedRange(h.syncMode, syncStartTimeNs, newTimeRangeNs);

            // Simple click: set timeline position at middle of the screen
            // Double click: adapt also the scale to have the scope at a fixed percentage of the size of the screen
            s64 scopeDurationNs = 0;
            if     (hd.lIdx==PL_INVALID) { } // Marker case (we do not know the parent, so no duration)
            else if(elem.nameIdx==elem.hlNameIdx) scopeDurationNs = (s64)(h.absMinValue+yDelta*highlightedIdx); // For scopes, the value is the duration
            else scopeDurationNs = cmGetParentDurationNs(_record,hd.threadId, elem.nestingLevel, hd.lIdx); // For "flat" items, the duration is the one of the parent
            if(ImGui::IsMouseDoubleClicked(0) && scopeDurationNs>0) newTimeRangeNs = vwConst::DCLICK_RANGE_FACTOR*scopeDurationNs;
            synchronizeNewRange(h.syncMode, bsMax(hd.timeNs-(s64)(0.5*(newTimeRangeNs-scopeDurationNs)), 0LL), newTimeRangeNs);

            if(ImGui::IsMouseClicked(0) && h.dragMode==NONE) {
                // Double click: view the thread. Single click: view the lock section (if it is a lock)
                bool isLock = (eType==PL_FLAG_TYPE_LOCK_NOTIFIED || eType==PL_FLAG_TYPE_LOCK_ACQUIRED || eType==PL_FLAG_TYPE_LOCK_WAIT);
                ensureThreadVisibility(h.syncMode, (ImGui::IsMouseDoubleClicked(0) || !isLock)? hd.threadId : vwConst::LOCKS_THREADID);

                // Synchronize the text (after getting the nesting level and lIdx for this date on this thread)
                int nestingLevel;
                u32 lIdx;
                cmGetRecordPosition(_record, hd.threadId, hd.timeNs, nestingLevel, lIdx);
                synchronizeText(h.syncMode, hd.threadId, nestingLevel, lIdx, hd.timeNs, h.uniqueId);
            }
        }
    } // End of processing of the highlight

    // Draw legend
    {
        const float legendTextMargin = 5.f;
        const bool   isFullRange      = (h.startTimeNs==0 && h.timeRangeNs==_record->durationNs);
        const float legendCol1Width  = ImGui::CalcTextSize("Quantity").x+legendTextMargin;
        const float legendCol2Width  = ImGui::CalcTextSize("<Lock notified>").x+legendTextMargin;
        const float legendWidth      = bsMax(legendCol1Width+legendCol2Width, (float)ImGui::CalcTextSize(h.name.toChar()).x)+3.f*legendTextMargin;
        const float lineHeight       = ImGui::GetTextLineHeightWithSpacing();
        const float legendHeight     = 4.f*lineHeight;
        const float legendX          = winX+h.legendPosX*winWidth;
        const float legendY          = winY+topBarHeight+h.legendPosY*(winHeight-topBarHeight-vMargin);

        // Box
        DRAWLIST->AddRectFilled(ImVec2(legendX, legendY), ImVec2(legendX+legendWidth, legendY+legendHeight), IM_COL32(0,0,0,160)); // Transparent black
        DRAWLIST->AddRect       (ImVec2(legendX, legendY),       ImVec2(legendX+legendWidth, legendY+legendHeight), vwConst::uWhite);

        // Title
        DRAWLIST->AddText(ImVec2(legendX+0.5f*(legendWidth-ImGui::CalcTextSize(h.name.toChar()).x), legendY), vwConst::uYellow, h.name.toChar());
        DRAWLIST->AddLine(ImVec2(legendX, legendY+lineHeight-2), ImVec2(legendX+legendWidth, legendY+lineHeight-2.f), vwConst::uWhite);

        // Elems
        const char* binSizeStr = 0;
        if     (eType==PL_FLAG_TYPE_DATA_STRING)   binSizeStr = "<Enum>";
        else if(eType==PL_FLAG_TYPE_MARKER)        binSizeStr = "<Marker>";
        else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) binSizeStr = "<Lock notified>";
        else binSizeStr = getValueAsChar(elem.flags, yDelta, 0., h.isHexa);  // Persistent string until next call
        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin,                 legendY+1.f*lineHeight), vwConst::uWhite, "Bin size");
        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin+legendCol1Width, legendY+1.f*lineHeight), vwConst::uGrey, binSizeStr);

        snprintf(tmpStr, sizeof(tmpStr), "%u", h.totalQty);
        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin,                 legendY+2.f*lineHeight), vwConst::uWhite, "Quantity");
        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin+legendCol1Width, legendY+2.f*lineHeight), vwConst::uGrey, tmpStr);

        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin,                 legendY+3.f*lineHeight), vwConst::uWhite, "Range");
        DRAWLIST->AddText(ImVec2(legendX+legendTextMargin+legendCol1Width, legendY+3.f*lineHeight), vwConst::uGrey, isFullRange?"Full":"Partial");

        if(isWindowHovered) {
            bool isLegendHovered = (mouseX>=legendX && mouseX<=legendX+legendWidth && mouseY>=legendY && mouseY<=legendY+legendHeight);

            // Right click: open contextual menu
            if(isLegendHovered && highlightedIdx<0 && h.legendDragMode==NONE && ImGui::IsMouseReleased(2)) {
                ImGui::OpenPopup("Histogram menu");
                // Precompute the menu content
                _rangeMenuItems[0] = { 0, 0,        ""}; _rangeMenuItems[1] = { 0, 0, "Full range"};
                _rangeMenuItems[2] = { 0, 0, "Group 1"}; _rangeMenuItems[3] = { 0, 0,    "Group 2"};
                _rangeMenuSelection = -1; // -1 means not needed
                if(h.timeRangeNs!=_record->durationNs) {
                    _rangeMenuItems[1].timeRangeNs = _record->durationNs; // Activate the full range menu if current is not full range
                    _rangeMenuSelection = 0;  // Activate the range menu
                }
                for(int i=2; i<4; ++i) {
                    s64 dst, dtr;
                    getSynchronizedRange(i-1, dst, dtr); // Query group 1 & 2
                    if(dtr!=_record->durationNs && (dst!=h.startTimeNs || dtr!=h.timeRangeNs)) { // Not already seen range?
                        _rangeMenuItems[i].startTimeNs = dst; _rangeMenuItems[i].timeRangeNs = dtr;
                        _rangeMenuSelection = 0; // Activate the range menu
                    }
                }
            }

            // Tooltip
            if(isLegendHovered && getLastMouseMoveDurationUs()>500000) {
                int pathQty = 1;
                int path[cmConst::MAX_LEVEL_QTY+1] = {h.elemIdx};
                while(pathQty<cmConst::MAX_LEVEL_QTY+1 && path[pathQty-1]>=0) { path[pathQty] = _record->elems[path[pathQty-1]].prevElemIdx; ++pathQty; }
                int offset = snprintf(tmpStr, sizeof(tmpStr), "[%s] ", (elem.threadId>=0)? getFullThreadName(elem.threadId) : "(all)*");
                for(int i=pathQty-2; i>=0; --i) {
                    offset += snprintf(tmpStr+offset, sizeof(tmpStr)-offset, "%s>", _record->getString(_record->elems[path[i]].nameIdx).value.toChar());
                }
                tmpStr[offset-1] = 0; // Remove the last '>'
                ImGui::SetTooltip("%s\nFrom %s to %s", tmpStr, getNiceTime(h.startTimeNs, 0, 0), getNiceTime(h.startTimeNs+h.timeRangeNs, 0, 1));
            }

            // Dragging
            if(isLegendHovered && h.legendDragMode==NONE && h.dragMode==NONE && ImGui::IsMouseDragging(2)) {
                h.legendDragMode = DATA;
            }
            if(h.legendDragMode==DATA) {
                if(ImGui::IsMouseDragging(2)) {
                    h.legendPosX = bsMinMax(h.legendPosX+ImGui::GetMouseDragDelta(2).x/winWidth, 0.05f, 0.9f);
                    h.legendPosY = bsMinMax(h.legendPosY+ImGui::GetMouseDragDelta(2).y/(winHeight-topBarHeight-vMargin), 0.f, 0.85f);
                    ImGui::ResetMouseDragDelta(2);
                } else h.legendDragMode = NONE;
            }
        }
    } // End of legend drawing

    // Middle click: Range drag selection
    if(isWindowHovered && ImGui::IsMouseDragging(1)) { // Button 1, no sensitivity threshold
        h.rangeSelStartIdx = (int)((mouseX-winX-ImGui::GetMouseDragDelta(1).x+scrollX-uMargin-0.5f*barTotalWidth)/barTotalWidth+0.5f);
        h.rangeSelEndIdx   = (int)((mouseX-winX                              +scrollX-uMargin-0.5f*barTotalWidth)/barTotalWidth+0.5f);
        h.rangeSelEndIdx   = bsMin(h.rangeSelEndIdx, h.data.size()-1);
        if(h.rangeSelStartIdx>=h.rangeSelEndIdx) { h.rangeSelStartIdx = h.rangeSelEndIdx = 0; } // Cancel
        else { // Display the selection box with transparency
            float x1 = winX-scrollX+uMargin+h.rangeSelStartIdx  *barTotalWidth;
            float x2 = winX-scrollX+uMargin+(h.rangeSelEndIdx+1)*barTotalWidth;
            constexpr float arrowSize = 4.f;
            // White background
            DRAWLIST->AddRectFilled(ImVec2(x1, winY+topBarHeight), ImVec2(x2, winY+winHeight), IM_COL32(255,255,255,128));
            // Range line
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x2, mouseY), vwConst::uBlack, 2.);
            // Arrows
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x1+arrowSize, mouseY-arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x1+arrowSize, mouseY+arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x2, mouseY), ImVec2(x2-arrowSize, mouseY-arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x2, mouseY), ImVec2(x2-arrowSize, mouseY+arrowSize), vwConst::uBlack, 2.f);
            // Text
            snprintf(tmpStr, sizeof(tmpStr),  "{ %s -> %s }",
                     getValueAsChar(elem.flags, isDiscrete? (double)h.discreteLkup[h.rangeSelStartIdx] : h.absMinValue+yDelta*h.rangeSelStartIdx, 0., h.isHexa),
                     getValueAsChar(elem.flags, isDiscrete? (double)h.discreteLkup[h.rangeSelEndIdx  ] : h.absMinValue+yDelta*h.rangeSelEndIdx  , 0., h.isHexa, 1));
            ImVec2 tb = ImGui::CalcTextSize(tmpStr);
            float x3 = 0.5f*(x1+x2-tb.x);
            if(x3<x1)      DRAWLIST->AddRectFilled(ImVec2(x3, mouseY-tb.y-5.f), ImVec2(x1,      mouseY-5.f), IM_COL32(255,255,255,128));
            if(x3+tb.x>x2) DRAWLIST->AddRectFilled(ImVec2(x2, mouseY-tb.y-5.f), ImVec2(x3+tb.x, mouseY-5.f), IM_COL32(255,255,255,128));
            DRAWLIST->AddText(ImVec2(x3, mouseY-tb.y-5), vwConst::uBlack, tmpStr);
        }
    }
    else if(h.rangeSelEndIdx>0.) {
        // Set the selected range view
        double zoomRatio = winWidth/(h.rangeSelEndIdx+1-h.rangeSelStartIdx)/barTotalWidth;
        h.viewZoom = bsMin(h.viewZoom*zoomRatio, (double)fullResBinQty/MIN_BAR_QTY);
        h.checkBounds();
        h.viewStartX = (double)h.rangeSelStartIdx/h.data.size()*(h.viewZoom*winWidth-2.f*uMargin);
        // Reset the selection
        h.rangeSelStartIdx =  h.rangeSelEndIdx = 0;
        h.isCacheDirty = true;
        dirty();
    }

    // Full screen
    if(hasKeyboardFocus && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(h.uniqueId);
    }

    // Contextual menu
    if(ImGui::BeginPopup("Histogram menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        // Title
        ImGui::TextColored(vwConst::grey, "Histogram '%s'", h.name.toChar());
        ImGui::Separator();
        ImGui::Separator();

        // Color
        std::function<void(int)> curveSetColor = [&h, this] (int colorIdx) { getConfig().setCurveColorIdx(h.elemIdx, colorIdx); };
        displayColorSelectMenu("Color", getConfig().getCurveColorIdx(h.elemIdx), curveSetColor);

        // Open as plot
        if(ImGui::Selectable("View as plot")) {
            // Create an empty plot
            _plots.push_back( { } );
            auto& pw       = _plots.back();
            pw.uniqueId    = getId();
            pw.unit        = _record->getString(elem.nameIdx).unit;
            if(pw.unit.empty()) pw.unit = getUnitFromFlags(elem.flags);
            pw.startTimeNs = 0;
            pw.timeRangeNs = _record->durationNs;
            // Plot all corresponding names
            for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
                const cmRecord::Elem& elem2 = _record->elems[elemIdx];
                if(elem.isPartOfHStruct==elem2.isPartOfHStruct && elem2.threadId==elem.threadId &&
                   elem2.nameIdx==elem.nameIdx && elem2.flags==elem.flags) {
                    pw.curves.push_back( { h.threadUniqueHash, elem2.partialHashPath, elemIdx, true } );
                }
            }
            setFullScreenView(-1);
        }

        // Worth having a range menu item?
        if(_rangeMenuSelection>=0) {
            ImGui::Text("New range"); ImGui::SameLine(0, 20);
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("Full range XXX").x);
            if(ImGui::BeginCombo("", _rangeMenuItems[_rangeMenuSelection].name.toChar(), 0)) {
                for(int i=0; i<4; ++i) {
                    if(_rangeMenuItems[i].timeRangeNs==0) continue; // Inactive
                    if(!ImGui::Selectable(_rangeMenuItems[i].name.toChar(), false) || i==0) continue;
                    // Do switch range, which involve recomputations
                    h.startTimeNs = _rangeMenuItems[i].startTimeNs;
                    h.timeRangeNs = _rangeMenuItems[i].timeRangeNs;
                    h.computationLevel = 0;
                }
                ImGui::EndCombo();
            }
        }

        ImGui::EndPopup();
    }

    // Help
    displayHelpTooltip(h.uniqueId, "Help Histogram",
                       "##Histogram view\n"
                       "===\n"
                       "Histogram of any event kind.\n"
                       "#Warning#: view creation is not instantaneous as it requires reading all data for the selected time range.\n"
                       "\n"
                       "##Actions:\n"
                       "-#H key#| This help\n"
                       "-#F key#| Full screen view\n"
                       "-#Right mouse button dragging#| Move the viewed range\n"
                       "-#Right mouse button dragging on legend#| Move the legend\n"
                       "-#Middle mouse button dragging#| Select a value range\n"
                       "-#Left/Right key#| Move horizontally\n"
                       "-#Ctrl-Left/Right key#| Move horizontally faster\n"
                       "-#Up/Down key#| Bin size zoom\n"
                       "-#Mouse wheel#| Bin size zoom\n"
                       "-#Ctrl-Up/Down key#| Value zoom\n"
                       "-#Ctrl-Mouse wheel#| Value zoom\n"
                       "-#Left mouse click on point#| Time synchronize views of the same group, for one of the item\n"
                       "-#Double left mouse click on point#| Time and range synchronize views of the same group, for one of the item\n"
                       "-#Right mouse click on legend#| Open menu for histogram configuration and plot\n"
                       "\n"
                       );

    ImGui::EndChild();
}
