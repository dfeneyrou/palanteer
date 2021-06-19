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

// This file implements the profile view both as a text array and a flame graph

// System
#include <algorithm>
#include <math.h>
#include <cinttypes>

// Internal
#include "cmRecord.h"
#include "vwMain.h"
#include "vwConst.h"
#include "vwConfig.h"


#ifndef PL_GROUP_PROF
#define PL_GROUP_PROF 0
#endif

// Some constants
static const double MIN_BAR_WIDTH = 3.; // Ensure all item are visible


bsString
vwMain::Profile::getDescr(void) const
{
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "profile %d %d %d %d %" PRIX64,
             syncMode, (int)kind, isFlameGraph?1:0, isFlameGraphDownward?1:0, threadUniqueHash);
    return tmpStr;
}


void
vwMain::addProfileRange(int id, ProfileKind kind, int threadId, u64 threadUniqueHash, s64 startTimeNs, s64 timeRangeNs)
{
    // Sanity
    if(!_record) return;
    plScope("addProfile");

    // Either threadId<0 and the hash shall be known (for live case, the threadId can be discovered later)
    // Either threadId>=0 and a null hash can be deduced
    if(threadId<0) {
        for(int i=0; i<_record->threads.size(); ++i) {
            if(_record->threads[i].threadUniqueHash!=threadUniqueHash) continue;
            threadId = i;
            break;
        }
    }
    if(threadUniqueHash==0) {
        plAssert(threadId>=0);
        threadUniqueHash = _record->threads[threadId].threadUniqueHash;
    }
    plgVar(PROF, threadUniqueHash, startTimeNs, getNiceDuration(timeRangeNs));

    // Add the request
    _profiles.push_back({id, kind, startTimeNs, timeRangeNs, threadUniqueHash});
    setFullScreenView(-1);
}


void
vwMain::addProfileScope(int id, ProfileKind kind, int threadId, int nestingLevel, u32 scopeLIdx)
{
    // Sanity
    if(!_record) return;
    plgScope (PROF, "addProfile");
    plgVar(PROF, threadId, nestingLevel, scopeLIdx);

    // Add the request
    _profiles.push_back({id, kind, 0, 0, _record->threads[threadId].threadUniqueHash, -1, nestingLevel, scopeLIdx});
    setFullScreenView(-1);
}


void
vwMain::_addProfileStack(Profile& prof, const bsString& name, s64 startTimeNs, s64 timeRangeNs,
                          bool addFakeRootNode, int startNestingLevel, bsVec<u32> scopeLIndexes)
{
    // Store the finalized profile infos
    prof.name        = name;
    prof.startTimeNs = startTimeNs;
    prof.timeRangeNs = timeRangeNs;

    // Build the start stack
    _profileBuild.addFakeRootNode = addFakeRootNode;
    bsVec<ProfileBuildItem>& stack = _profileBuild.stack;
    stack.clear(); stack.reserve(128);
    for(u32 scopeLIdx : scopeLIndexes) {
        stack.push_back({ addFakeRootNode? 0:-1, startNestingLevel, scopeLIdx});
    }

    // Add the root node if required
    if(_profileBuild.addFakeRootNode) {
        bsString nodeName = bsString((prof.startTimeNs==0 && prof.timeRangeNs==_record->durationNs)? "<Full record ":"<Partial record ") +
            getNiceDuration(prof.timeRangeNs)+bsString(">");
        // For Timings, the top node range is the inspected time range. For other kinds, it depends on values and will be set later
        prof.data.push_back( { nodeName, (u32)-1, 0, stack.back().nestingLevel-1, PL_INVALID, 1,
                (prof.kind==TIMINGS)? (u64)prof.timeRangeNs : 0, 0, "", 0, 0 } );
    }

    plMarker("user", "Add a profile");
}


bool
vwMain::_computeChunkProfileStack(Profile& prof)
{
    // Need to work?
    if(prof.computationLevel>=100) return true;
    if(prof.computationLevel==0 && _backgroundComputationInUse) return true; // Waiting for a free slot

    // Finish the initialization if needed (init and live)
    if(prof.threadId<0 && (prof.isFirstRun || _liveRecordUpdated)) {
        prof.isFirstRun = false;

        for(int threadId=0; threadId<_record->threads.size(); ++threadId) {
            if(_record->threads[threadId].threadUniqueHash!=prof.threadUniqueHash) continue;
            prof.threadId = threadId;

            // Thread found: complete the profile initialization
            if(prof.reqNestingLevel<0) {
                // Range based request
                s64  dummyScopeStartTimeNs, dummyScopeEndTimeNs, durationNs;
                bool isCoarseScope;
                cmRecord::Evt evt;
                bsVec<u32> scopeLIndexes;
                if(prof.timeRangeNs==0) prof.timeRangeNs = _record->durationNs; // Live record starts empty...

                // Collect the data
                for(int startNestingLevel=0; startNestingLevel<_record->threads[threadId].levels.size(); ++startNestingLevel) {
                    // Try this level, until we find scopes which are fully contained in the desired range
                    u32 scopeLIdx;
                    cmRecordIteratorScope it(_record, threadId, startNestingLevel, (s64)prof.startTimeNs, 0);
                    while((scopeLIdx=it.getNextScope(isCoarseScope, dummyScopeStartTimeNs, dummyScopeEndTimeNs, evt, durationNs))!=PL_INVALID) {
                        plAssert(!isCoarseScope); // By design
                        if(evt.vS64<prof.startTimeNs) continue;
                        if(evt.vS64+durationNs>prof.startTimeNs+prof.timeRangeNs) break;
                        scopeLIndexes.push_back(scopeLIdx);
                    }
                    // If we have non empty stack with this level, create the profile
                    if(!scopeLIndexes.empty()) {
                        // Inverse its content to match the stack way of working (and have chronological processing order)
                        for(int i=0; i<scopeLIndexes.size()/2; ++i) bsSwap(scopeLIndexes[i], scopeLIndexes[scopeLIndexes.size()-1-i]);

                        // Build the new profiling view and return (we found a level with data for the given range)
                        _addProfileStack(prof, getFullThreadName(threadId), prof.startTimeNs,
                                         prof.timeRangeNs, true, startNestingLevel, scopeLIndexes);
                        break;
                    }
                }
                if(scopeLIndexes.empty()) {
                    return false;  // Nothing to profile was found, so cancel the request
                }
            }
            else {
                // Scope based request
                s64  dummyScopeStartTimeNs, dummyScopeEndTimeNs, durationNs;
                bool isCoarseScope;
                cmRecord::Evt evt;
                cmRecordIteratorScope it(_record, threadId, prof.reqNestingLevel, prof.reqScopeLIdx);
                u32 scopeLIdx2 = it.getNextScope(isCoarseScope, dummyScopeStartTimeNs, dummyScopeEndTimeNs, evt, durationNs);
                (void)scopeLIdx2;
                plAssert(!isCoarseScope);                            // By design
                plAssert(scopeLIdx2==prof.reqScopeLIdx, scopeLIdx2, prof.reqScopeLIdx); // By design
                // Build the new profiling view
                _addProfileStack(prof, _record->getString(evt.nameIdx).value, evt.vS64,
                                 durationNs, false, prof.reqNestingLevel, { prof.reqScopeLIdx });
            }
            // Thread has been found
            return true;  // We do not do a first chunk computation now so that ImGui stack is consistent for the progress dialog
        }
    }
    if(prof.threadId<0) return true; // Hash is not resolved yet

    // Compute a chunk of the profiled data
    bsVec<ProfileBuildItem>& stack      = _profileBuild.stack;
    bsVec<cmRecord::Evt>& dataChildren  = _profileBuild.dataChildren;
    bsVec<cmRecord::Evt>& dataChildren2 = _profileBuild.dataChildren2;
    bsVec<u32>& lIdxChildren     = _profileBuild.lIdxChildren;
    bsVec<u32>& lIdxChildren2    = _profileBuild.lIdxChildren2;
    bsVec<u32>& childrenScopeLIdx = _profileBuild.childrenScopeLIdx;
    s64  dummyScopeStartTimeNs, dummyScopeEndTimeNs, durationNs, durationNs2;
    bool isCoarseScope;
    cmRecord::Evt evt, evt2;
    char extraStr[128] = {0};
    dirty();

    // Bootstrap the computation
    if(prof.computationLevel==0) {
        _backgroundComputationInUse = true;
        ImGui::OpenPopup("In progress##WaitProfile");
    }

    // Collect the profiling data
    bsUs_t endComputationTimeUs = bsGetClockUs() + vwConst::COMPUTATION_TIME_SLICE_US; // Time slice of computation
    while(!stack.empty()) {
        plgScope (PROF, "stack iteration");

        // Get info on the scope
        const ProfileBuildItem item = stack.back(); stack.pop_back();
        plgVar(PROF, item.nestingLevel, item.scopeLIdx);
        cmRecordIteratorScope itScope(_record, prof.threadId, item.nestingLevel, item.scopeLIdx);
        u32 scopeLIdx2 = itScope.getNextScope(isCoarseScope, dummyScopeStartTimeNs, dummyScopeEndTimeNs, evt, durationNs);
        (void)scopeLIdx2;
        plAssert(!isCoarseScope);                                      // By design
        plAssert(scopeLIdx2==item.scopeLIdx, scopeLIdx2, item.scopeLIdx); // By design
        prof.computationLevel = bsMinMax(100LL*(evt.vS64-prof.startTimeNs)/prof.timeRangeNs, 1LL, 99LL); // 0 means just started, 100 means finished

        // Get infos on its children
        u64 childrenValue = 0; // Unit depends on the profiling kind. Nanosecond for TIMINGS, bytes for MEMORY, and quantity for MEMORY_CALLS
        int lastChildStartIdx = -1;
        u64 value = 0, callQty = 0;
        childrenScopeLIdx.clear();

        itScope.getChildren(evt.linkLIdx, item.scopeLIdx, true, false, false, dataChildren, lIdxChildren);

        // Timing case
        if(prof.kind==TIMINGS) {
            value   = durationNs;
            callQty = 1;
            for(int i=0; i<dataChildren.size(); ++i) {
                const cmRecord::Evt& d = dataChildren[i];
                if(d.flags&PL_FLAG_SCOPE_BEGIN) { lastChildStartIdx = i; continue; }
                if(!(d.flags&PL_FLAG_SCOPE_END) || lastChildStartIdx<0) continue;
                childrenValue += d.vS64-dataChildren[lastChildStartIdx].vS64;
                childrenScopeLIdx.push_back(lIdxChildren[lastChildStartIdx]);
                lastChildStartIdx = -1;
            }
        }
        // Memory case
        else {
            // Get the current node's value
            for(int i=0; i<dataChildren.size(); ++i) {
                const cmRecord::Evt& d = dataChildren[i];
                // ALLOC node = we get the node memory infos
                if((d.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_ALLOC) {
                    value   = (prof.kind==MEMORY_CALLS)? (d.vU64>>32):(d.vU64&0xFFFFFFFF);
                    callQty = (prof.kind==MEMORY_CALLS)? 1 : (d.vU64>>32);
                }
                // Begin bloc = child. Look at its children to find the ALLOC node
                if(d.flags&PL_FLAG_SCOPE_BEGIN) {
                    // Get children of this child to find its "ALLOC" node (usually last child)
                    cmRecordIteratorScope itScope2(_record, prof.threadId, item.nestingLevel+1, lIdxChildren[i]);
                    scopeLIdx2 = itScope2.getNextScope(isCoarseScope, dummyScopeStartTimeNs, dummyScopeEndTimeNs, evt2, durationNs2);
                    (void)scopeLIdx2;
                    itScope2.getChildren(evt2.linkLIdx, lIdxChildren[i], true, false, false, dataChildren2, lIdxChildren2);
                    // Get the memory value from it
                    u32 childValue = 0;
                    for(const cmRecord::Evt& d2 : dataChildren2) {
                        if((d2.flags&PL_FLAG_TYPE_MASK)!=PL_FLAG_TYPE_ALLOC) continue;
                        childValue = (prof.kind==MEMORY_CALLS)? (d2.vU64>>32):(d2.vU64&0xFFFFFFFF);
                    }
                    if(childValue==0) continue; // No memory info
                    // Store the child infos
                    childrenValue += childValue;
                    childrenScopeLIdx.push_back(lIdxChildren[i]);
                }
            }
        }
        if(value==0) continue; // May happen for some top nodes

        // Add or update a node
        int currentDataIdx = -1;
        if(item.parentIdx>=0) {
            plgScope (PROF, "Update data");
            // Try to find a brother with the same name
            for(int brotherIdx : prof.data[item.parentIdx].childrenIndices) {
                ProfileData& brother = prof.data[brotherIdx];
                if(evt.nameIdx!=brother.nameIdx) continue;
                // Update the existing node
                currentDataIdx = brotherIdx;
                brother.callQty       += callQty;
                brother.value         += value;
                brother.childrenValue += childrenValue;
                if(evt.vS64<brother.firstStartTimeNs) { // We want the canonical first one
                    brother.firstStartTimeNs = evt.vS64;
                    brother.firstRangeNs     = durationNs;
                }
                plgVar(PROF, brother.callQty, brother.value, brother.childrenValue);
                break;
            }
        }

        // No "brother", create a new node
        if(currentDataIdx<0) {
            plgScope (PROF, "Add new data");
            plgData(PROF, "Name", _record->getString(evt.nameIdx).value.toChar());
            plgVar(PROF, value, childrenValue);
            currentDataIdx = prof.data.size();
            if(prof.kind==TIMINGS) {
                if(evt.lineNbr>0) {
                    snprintf(extraStr, sizeof(extraStr), "At line %d in file %-20s", evt.lineNbr, _record->getString(evt.filenameIdx).value.toChar());
                } else {
                    snprintf(extraStr, sizeof(extraStr), "In %-20s", _record->getString(evt.filenameIdx).value.toChar());
                }
            }
            bsString prefix = ((evt.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_WAIT)? "<lock wait> " : "";
            prof.data.push_back({ prefix + _record->getString(evt.nameIdx).value, evt.nameIdx, evt.flags, item.nestingLevel,
                    item.scopeLIdx, (int)callQty, value, childrenValue, extraStr, evt.vS64, durationNs });
            if(item.parentIdx>=0) {
                prof.data[item.parentIdx].childrenIndices.push_back(currentDataIdx);
            }
        }

        // Push children on stack to propagate the processing
        for(int i=childrenScopeLIdx.size()-1; i>=0; --i) {
            u32 cLIdx = childrenScopeLIdx[i];
            plgScope (PROF, "Push on stack");
            plgData(PROF, "nesting level", item.nestingLevel+1);
            plgData(PROF, "scopeLIdx", cLIdx);
            stack.push_back( { currentDataIdx, item.nestingLevel+1, cLIdx } );
        }
        if(bsGetClockUs()>endComputationTimeUs) break;
    } // End of loop on the stack

     // Computations are finished?
    if(stack.empty()) prof.computationLevel = 100;

    bool openPopupModal = true;
    if(ImGui::BeginPopupModal("In progress##WaitProfile",
                              &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(vwConst::gold, "Profile computation...");
        snprintf(extraStr, sizeof(extraStr), "%d %%", prof.computationLevel);
        ImGui::ProgressBar(0.01*prof.computationLevel, ImVec2(-1,ImGui::GetTextLineHeight()), extraStr);
        if(prof.computationLevel==100) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(!openPopupModal) { // Cancelled by used
        _backgroundComputationInUse = false;
        return false;
    }
    if(prof.computationLevel<100) return true; // Not finished
    _backgroundComputationInUse = false;

    // Compute the value of the artificial top node
    if(_profileBuild.addFakeRootNode) {
        for(int ci : prof.data[0].childrenIndices) prof.data[0].childrenValue += prof.data[ci].value;
        if(prof.kind!=TIMINGS) { // For timing, it is already set to the inspected time range
            prof.data[0].value = prof.data[0].childrenValue;
        }
    }
    if(prof.data.empty() || prof.data[0].value==0) { // Cancel; no data to show
        return false;
    }
    prof.totalValue = prof.data[0].value;

    // Sort the children alphabetically
    for(auto& d : prof.data) {
        if(d.childrenIndices.size()<2) continue;
        std::sort(d.childrenIndices.begin(), d.childrenIndices.end(),
                  [this, &prof](int& a, int& b)->bool {
                      return strcasecmp(this->_record->getString(prof.data[a].nameIdx).value.toChar(),
                                        this->_record->getString(prof.data[b].nameIdx).value.toChar())<=0; });
    }

    // Create the list display indexes (order of data above shall not be modified)
    prof.listDisplayIdx.reserve(prof.data.size());
    for(int i=0; i<prof.data.size(); ++i) prof.listDisplayIdx.push_back(i);

    // Base fields
    prof.callName = (prof.kind==MEMORY)? "alloc" : "scope";
    if(prof.kind==MEMORY_CALLS) prof.minRange = 100.; // Minor tuning
    prof.endValue   = prof.data[0].value;
    // Compute colors
    for(ProfileData& d : prof.data) {
        const char* s = d.name.toChar();
        u32 h = 2166136261;
        while(*s) h = (h^((u32)(*s++)))*16777619; // FNV-1A 32 bits
        double h1   = (double)h/(double)0xFFFFFFFFL;
        double h2   = (double)((h^31415926)*16777619)/(double)0xFFFFFFFFL;
        d.color  = ImColor((int)(155+55*h1), (int)(180*h2), (int)45*h2, 255); // Red-ish color
    }
    // Compute max depth (recursively)
    prof.workStack.clear();
    prof.workStack.push_back( { 0, 1 } );
    while(!prof.workStack.empty()) {
        ProfileStackItem si = prof.workStack.back(); prof.workStack.pop_back();
        if(si.nestingLevel>prof.maxDepth) prof.maxDepth = si.nestingLevel;
        for(int cidx : prof.data[si.idx].childrenIndices) prof.workStack.push_back( { cidx, si.nestingLevel+1 } );
    }

    plAssert(prof.timeRangeNs>0);
    plAssert(prof.totalValue>0);
    dirty();
    return true;
}


void
vwMain::drawProfiles(void)
{
    if(!_record) return;

    int itemToRemoveIdx = -1;
    const double fontHeight = ImGui::GetTextLineHeightWithSpacing();
    for(int profIdx=0; profIdx<_profiles.size(); ++profIdx) {
        // Display complete tabs
        auto& prof = _profiles[profIdx];

        if(!_computeChunkProfileStack(prof)) {
            // Cancelled by user: remove this histogram from the list
            itemToRemoveIdx = profIdx;
            continue;
        }

        if(_uniqueIdFullScreen>=0 && prof.uniqueId!=_uniqueIdFullScreen) continue;

        // Configure the tab with the thread color
        bool hasColoredTab =(prof.threadId>=0);
        if(hasColoredTab) {
            const ImVec4 c = getConfig().getThreadColor(prof.threadId);
            float a;
            a = 1.1; ImGui::PushStyleColor(ImGuiCol_TabActive,          ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 1.4; ImGui::PushStyleColor(ImGuiCol_TabHovered,         ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 0.4; ImGui::PushStyleColor(ImGuiCol_Tab,                ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 0.4; ImGui::PushStyleColor(ImGuiCol_TabUnfocused,       ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 0.5; ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 0.4; ImGui::PushStyleColor(ImGuiCol_TitleBg,            ImVec4(a*c.x, a*c.y, a*c.z, 1.));
            a = 1.1; ImGui::PushStyleColor(ImGuiCol_TitleBgActive,      ImVec4(a*c.x, a*c.y, a*c.z, 1.));
        }

        if(prof.isWindowSelected) {
            prof.isWindowSelected = false;
            ImGui::SetNextWindowFocus();
        }
        if(prof.isNew) {
            prof.isNew = false;
            if(prof.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(prof.newDockId);
            else selectBestDockLocation(true, false);
        }
        char tmpStr[256];
        snprintf(tmpStr, sizeof(tmpStr), "%s [%s]###%d", (prof.kind==TIMINGS)? "Timings" : ((prof.kind==MEMORY)? "Alloc mem" : "Alloc calls"),
                 (prof.threadId>=0)? prof.name.toChar() : "(Not present)", prof.uniqueId);
        bool isOpen = true;
        if(!ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing) || prof.computationLevel<100) {
            if(!isOpen) itemToRemoveIdx = profIdx;
            if(hasColoredTab) ImGui::PopStyleColor(7);
            ImGui::End();
            continue;
        }
        if(!isOpen) itemToRemoveIdx = profIdx;

        // Header
        // ======
        const bool isFullRange = (prof.startTimeNs==0 && prof.timeRangeNs==_record->durationNs);
        double comboWidth = ImGui::CalcTextSize("Isolated XXX").x;

        // Display the thread name
        float   textBgY     = ImGui::GetWindowPos().y+ImGui::GetCursorPos().y;
        float   baseHeaderX = ImGui::GetWindowContentRegionMax().x-2.*comboWidth;
        DRAWLIST->AddRectFilled(ImVec2(ImGui::GetWindowPos().x+ImGui::GetCursorPos().x-2., textBgY),
                                ImVec2(ImGui::GetWindowPos().x+baseHeaderX, textBgY+ImGui::GetStyle().FramePadding.y+fontHeight), vwConst::uGrey48);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(" [%s] %s", getFullThreadName(prof.threadId), (prof.kind==TIMINGS)?"Timings" : ((prof.kind==MEMORY)? "Allocated memory" : "Allocation calls"));
        ImGui::SameLine();
        ImGui::Text("(%s range)", isFullRange?"Full":"Partial");
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Range: %s -> %s", getNiceTime(prof.startTimeNs, prof.timeRangeNs),
                              getNiceTime(prof.startTimeNs+prof.timeRangeNs, prof.timeRangeNs, 1));
        }

        // Drawing kind selection
        ImGui::SameLine(baseHeaderX+1.); // Let 1 pixel spacing
        if     ( prof.isFlameGraph && ImGui::Button("To list",  ImVec2(comboWidth-2., 0))) prof.isFlameGraph = false;
        else if(!prof.isFlameGraph && ImGui::Button("To flame", ImVec2(comboWidth-2., 0))) prof.isFlameGraph = true;

        // Sync combo
        ImGui::SameLine(baseHeaderX+comboWidth);
        drawSynchroGroupCombo(comboWidth, &prof.syncMode);
        ImGui::Spacing();

        // Main display
        // ============

        ImGui::BeginChild("scope profile");
        if(prof.isFlameGraph) {
            // Flame graph
            _drawFlameGraph(prof.isFlameGraphDownward, prof);

            // Right click outside a scope
            if(!prof.isDragging && ImGui::IsWindowHovered() &&
               !ImGui::IsPopupOpen("profile menu") && ImGui::IsMouseReleased(2)) {
                ImGui::OpenPopup("profile menu");
                prof.cmDataIdx = -1; // Means not on a scope
            }

            // Update the dragging state *after* full drawing
            if(!ImGui::IsMouseDragging(2)) prof.isDragging = false;
        }
        else {
            // List
            _drawTextProfile(prof);
        }

        // Full screen
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
           !ImGui::GetIO().KeyCtrl) {
            if(ImGui::IsKeyPressed(KC_F)) setFullScreenView(prof.uniqueId);
            if(ImGui::IsKeyPressed(KC_H)) openHelpTooltip(prof.uniqueId, "Help Profile");
        }

        // Contextual menu
        if(ImGui::BeginPopup("profile menu", ImGuiWindowFlags_AlwaysAutoResize)) {
            if(prof.cmDataIdx<0) { // Not on a scope
                ImGui::Checkbox("Downward", &prof.isFlameGraphDownward);
            } else {               // On a scope
                double headerWidth = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Histogram").x+5;
                const ProfileData& d = prof.data[prof.cmDataIdx];
                ImGui::TextColored(vwConst::grey, "%s", _record->getString(d.nameIdx).value.toChar());
                ImGui::Separator();

                // Plot & histogram
                if(!displayPlotContextualMenu(prof.threadId, "Plot", headerWidth)) ImGui::CloseCurrentPopup();
                ImGui::Separator();
                if(!displayHistoContextualMenu(headerWidth))                       ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Help
        displayHelpTooltip(prof.uniqueId, "Help Profile",
                           "##Profile view\n"
                           "===\n"
                           "Flame graph or table of hierarchical aggregated resource usage.\n"
                           "The 3 following resources can be profiled per time range or for a particular scope:\n"
                           "-#CPU time#\n"
                           "-#Allocation calls#\n"
                           "-#Allocated memory#\n"
                           "\n"
                           "##Actions for flame graph:\n"
                           "-#Left mouse click on scope#| Zoom on this scope\n"
                           "-#Double left mouse click on scope#| Time and range synchronize views of the same group\n"
                           "-#Right mouse click on scope#| Open menu for plot/histogram\n"
                           "-#Right mouse button dragging#| Move the viewed range\n"
                           "-#Left/Right key#| Move horizontally\n"
                           "-#Ctrl-Left/Right key#| Move horizontally faster\n"
                           "-#Up/Down key#| Move vertically\n"
                           "-#Mouse wheel#| Move vertically\n"
                           "-#Middle mouse button dragging#| Select a resource range\n"
                           "-#Ctrl-Up/Down key#| Resource zoom\n"
                           "-#Ctrl-Mouse wheel#| Resource zoom\n"
                           "\n"
                           );

        ImGui::EndChild();

        ImGui::End();
        if(hasColoredTab) ImGui::PopStyleColor(7);
    }

    // Remove profile if needed
    if(itemToRemoveIdx>=0) {
        releaseId((_profiles.begin()+itemToRemoveIdx)->uniqueId);
        _profiles.erase(_profiles.begin()+itemToRemoveIdx);
        dirty();
        setFullScreenView(-1);
    }
}


void
vwMain::_drawTextProfile(Profile& prof)
{
    // Some init
    bsVec<ProfileData>& data = prof.data;
    bsVec<int>& lkup         = prof.listDisplayIdx;
    const double fontHeight  = ImGui::GetTextLineHeightWithSpacing();
    const char* tooltipSelf  = "'Self' means the contribution of the function itself, without all inner called functions";
    const char* tooltipIncl  = "'Inclusive' means the total contribution of the function itself and of the inner called functions";

    // Table header with sorting buttons
    ImGui::SetCursorPosY(ImGui::GetScrollY()); // Fix the drawing cursor to the top of the window
    int  cmDataIdx = -1;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x*3., style.CellPadding.y));
    if(ImGui::BeginTable("##table profile", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollX |
                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TableHeader("Name");
        ImGui::TableNextColumn(); ImGui::TableHeader("Self % total");
        if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) ImGui::SetTooltip("%s", tooltipSelf);
        ImGui::TableNextColumn(); ImGui::TableHeader(prof.kind==TIMINGS? "Self time" : "Self value");
        if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) ImGui::SetTooltip("%s", tooltipSelf);
        ImGui::TableNextColumn(); ImGui::TableHeader("Incl. % total");
        if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) ImGui::SetTooltip("%s", tooltipIncl);
        ImGui::TableNextColumn(); ImGui::TableHeader(prof.kind==TIMINGS? "Incl. time" : "Incl. value");
        if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) ImGui::SetTooltip("%s", tooltipIncl);
        ImGui::TableNextColumn(); ImGui::TableHeader(prof.kind==MEMORY?"Allocs":"Count");
        //ImGui::TableHeadersRow();


        // Sort files if required
        if(ImGuiTableSortSpecs* sortsSpecs= ImGui::TableGetSortSpecs()) {
            if(sortsSpecs->SpecsDirty) {
                if(!lkup.empty() && sortsSpecs->SpecsCount>0) {
                    s64 direction = (sortsSpecs->Specs->SortDirection==ImGuiSortDirection_Ascending)? 1 : -1;
                    if(sortsSpecs->Specs->ColumnIndex==0) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(a-b)<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==1 || sortsSpecs->Specs->ColumnIndex==2) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(((s64)(data[a].value-data[a].childrenValue)-(s64)(data[b].value-data[b].childrenValue)))<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==3 || sortsSpecs->Specs->ColumnIndex==4) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(((s64)data[a].value-(s64)data[b].value))<=0; } );
                    }
                    if(sortsSpecs->Specs->ColumnIndex==5) {
                        std::stable_sort(lkup.begin(), lkup.end(), [direction, &data](const int a, const int b)->bool \
                        { return direction*(data[a].callQty-data[b].callQty)<=0; } );
                    }
                }
                sortsSpecs->SpecsDirty = false;
            }
        }

        // Check if hovered
        char tmpStr[128];
        // Loop on profile items
        for(int i=0; i<lkup.size(); ++i) {
            int dataIdx = lkup[i];
            const ProfileData& d = data[dataIdx];

            // Display the line
            bool doHighlight = isScopeHighlighted(prof.threadId, -1., d.flags, d.nestingLevel, d.nameIdx);
            if(doHighlight) ImGui::PushStyleColor(ImGuiCol_Text, vwConst::uYellow);

            // Name
            ImGui::TableNextColumn();
            snprintf(tmpStr, sizeof(tmpStr), "%s%s", &("                "[2*(8-bsMinMax(d.nestingLevel, 0, 8))]), d.nameIdx!=PL_INVALID? d.name.toChar():"<Top>");
            ImGui::Selectable(tmpStr, doHighlight, ImGuiSelectableFlags_SpanAllColumns);
            if(ImGui::IsItemHovered() && d.nameIdx!=PL_INVALID) {
                // Store highlight infos
                setScopeHighlight(prof.threadId, prof.startTimeNs, prof.startTimeNs+prof.timeRangeNs, d.flags, d.nestingLevel, d.nameIdx, true);

                if(prof.syncMode>0) {
                    // Navigation: double click to show the first occurence (N/A on top node)
                    if(dataIdx>0 && ImGui::IsMouseDoubleClicked(0)) {
                        synchronizeNewRange(prof.syncMode, d.firstStartTimeNs-0.1*d.firstRangeNs, 1.2*d.firstRangeNs);
                        ensureThreadVisibility(prof.syncMode, prof.threadId);
                    }

                    // Zoom the timeline
                    int deltaWheel = ImGui::GetIO().MouseWheel;
                    if(deltaWheel!=0) {
                        // Ctrl: (Horizontal) range zoom
                        if(ImGui::GetIO().KeyCtrl) {
                            deltaWheel *= getConfig().getHWheelInversion();
                            double syncStartTimeNs, syncTimeRangeNs;
                            getSynchronizedRange(prof.syncMode, syncStartTimeNs, syncTimeRangeNs);
                            double targetTimeNs   = syncStartTimeNs + 0.5*syncTimeRangeNs; // Middle of screen is the invariant
                            double newTimeRangeNs = getUpdatedRange(deltaWheel, syncTimeRangeNs);
                            synchronizeNewRange(prof.syncMode,
                                                syncStartTimeNs+(targetTimeNs-syncStartTimeNs)/syncTimeRangeNs*(syncTimeRangeNs-newTimeRangeNs),
                                                newTimeRangeNs);
                            ensureThreadVisibility(prof.syncMode, prof.threadId);
                        }
                        // No Ctrl: standard vertical scrolling
                        else {
                            ImGui::SetScrollY(ImGui::GetScrollY()-3.*fontHeight*deltaWheel*getConfig().getVWheelInversion());
                        }
                    }
                }

                // Click right: contextual menu
                if(!prof.isDragging && ImGui::IsMouseReleased(2)) cmDataIdx = dataIdx; // Ctx menu will be open outside of the child window
            }

            // Self %
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", 100.*(d.value-d.childrenValue)/(double)prof.totalValue);
            // Self time or value
            ImGui::TableNextColumn();
            if(prof.kind==TIMINGS)     { ImGui::Text("%s", getNiceDuration(d.value-d.childrenValue)); }
            else if(prof.kind==MEMORY) { ImGui::Text("%s bytes", getNiceBigPositiveNumber(d.value-d.childrenValue)); }
            else                       { ImGui::Text("%s allocs", getNiceBigPositiveNumber(d.value-d.childrenValue)); }
            // Incl. %
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", 100.*d.value/(double)prof.totalValue);
            // Incl. time or value
            ImGui::TableNextColumn();
            if(prof.kind==TIMINGS)     { ImGui::Text("%s", getNiceDuration(d.value)); }
            else if(prof.kind==MEMORY) { ImGui::Text("%s bytes", getNiceBigPositiveNumber(d.value)); }
            else                       { ImGui::Text("%s allocs", getNiceBigPositiveNumber(d.value)); }
            // Count
            ImGui::TableNextColumn();
            ImGui::Text("%d", d.callQty);

            if(doHighlight) ImGui::PopStyleColor();
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    // Open the contextual menu
    if(cmDataIdx>=0) {
        prof.cmDataIdx = cmDataIdx;
        ImGui::OpenPopup("profile menu");
        _plotMenuItems.clear(); // Reset the popup menu state
        const ProfileData& d = prof.data[cmDataIdx];
        prepareGraphContextualMenu(prof.threadId, d.nestingLevel, d.scopeLIdx, prof.startTimeNs, prof.timeRangeNs);
    }
}


void
vwMain::_drawFlameGraph(bool doDrawDownward, Profile& prof)
{
    const double fontHeight    = ImGui::GetTextLineHeightWithSpacing();
    const double topBarHeight  = ImGui::GetTextLineHeightWithSpacing();
    const double topBarVMargin = 10.;
    const double fontSpacing   = 0.5*ImGui::GetStyle().ItemSpacing.y;
    const double textPixMargin = 3.*fontSpacing;
    const double winPosX    = ImGui::GetWindowPos().x;
    const double winPosY    = ImGui::GetWindowPos().y+ImGui::GetCursorPosY()-ImGui::GetScrollY();
    const double winWidth   = ImGui::GetWindowContentRegionMax().x;
    const double winHeight  = ImGui::GetWindowSize().y;
    const bool isWindowHovered = ImGui::IsWindowHovered();
    const double mouseX     = ImGui::GetMousePos().x;
    const double mouseY     = ImGui::GetMousePos().y;
    const double dataColMargin  = 40.;
    const double eps        = 1e-6;
    ImFont* font         = ImGui::GetFont();
    ImU32 colorOutline   = vwConst::uGrey48;
    ImU32 colorText1     = vwConst::uWhite;
    ImU32 colorText2     = vwConst::uGrey;

    auto getValueString = [this] (const vwMain::Profile& prof, double value) {
        if(prof.kind==TIMINGS) return bsString(getNiceDuration((s64)value));
        return bsString(getNiceBigPositiveNumber((s64)value)) + ((prof.kind==MEMORY)? " bytes" : " allocs");
    };

    // Handle animation (smooth move)
    if(prof.animTimeUs>0) {
        bsUs_t currentTimeUs = bsGetClockUs();
        double ratio = sqrt(bsMin((double)(currentTimeUs-prof.animTimeUs)/vwConst::ANIM_DURATION_US, 1.)); // Sqrt for more reactive start
        prof.startValue = ratio*prof.animStartValue2+(1.-ratio)*prof.animStartValue1;
        prof.endValue   = ratio*prof.animEndValue2  +(1.-ratio)*prof.animEndValue1;
        if(ratio==1.) prof.animTimeUs = 0;
    }

    // Initialize the stack
    bsVec<ProfileStackItem>& stack = prof.workStack;
    stack.clear();
    stack.push_back( { 0, 0, 0. } );

    // Loop on stack
    bool  isToolTipAlreadyDisplayed = false;
    const double c = winWidth/(prof.endValue-prof.startValue);
    while(!stack.empty()) {
        // Pop the next element to draw
        ProfileStackItem si = stack.back(); stack.pop_back();
        const ProfileData& item = prof.data[si.idx];
        double x1 = winPosX+c*(si.startValue-prof.startValue);
        double x2 = winPosX+c*(si.startValue+item.value-prof.startValue);
        if(x2<winPosX || x1>winPosX+winWidth) continue; // Outside of visible scope
        double y  = winPosY+(doDrawDownward? topBarHeight+topBarVMargin+fontHeight*si.nestingLevel : winHeight-fontHeight*(si.nestingLevel+1));
        bool isTruncated = false;
        if(x1<winPosX-eps)          { x1 = winPosX;          isTruncated = true; }
        if(x2>winPosX+winWidth+eps) { x2 = winPosX+winWidth; isTruncated = true; }
        prof.maxNestingLevel   = bsMax(prof.maxNestingLevel, si.nestingLevel);
        x2 = bsMax(x1+MIN_BAR_WIDTH, x2); // Ensure minimum with for antialiasing
        bool isHovered     = (isWindowHovered && mouseX>x1 && mouseX<x2 && mouseY>y && mouseY<y+fontHeight);
        bool isHighlighted = isHovered || isScopeHighlighted(prof.threadId, -1, item.flags, item.nestingLevel, item.nameIdx);

        // Draw
        const char* remaining = 0;
        u32 colorBg = item.color;
        if(isTruncated) { // Darkened color for partially visible items
            colorBg = ( ((((colorBg>>IM_COL32_R_SHIFT)&0xFF)/2)<<IM_COL32_R_SHIFT) |
                        ((((colorBg>>IM_COL32_G_SHIFT)&0xFF)/2)<<IM_COL32_G_SHIFT) |
                        ((((colorBg>>IM_COL32_B_SHIFT)&0xFF)/2)<<IM_COL32_B_SHIFT) |
                        (255<<IM_COL32_A_SHIFT));
        }
        DRAWLIST->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y+fontHeight), isHighlighted? vwConst::uWhite : colorBg);
        font->CalcTextSizeA(ImGui::GetFontSize(), x2-x1-textPixMargin*2.f, 0.0f, item.name.toChar(), NULL, &remaining);
        if(item.name.toChar()!=remaining) {
            DRAWLIST->AddText(ImVec2(x1+textPixMargin, y+fontSpacing),
                              isHighlighted? vwConst::uBlack : (isTruncated? colorText2 : colorText1),
                              item.name.toChar(), remaining);
        }
        DRAWLIST->AddRect(ImVec2(x1, y), ImVec2(x2, y+fontHeight), colorOutline);

        // Propagate to the children
        double startValue = si.startValue;
        for(int idx : item.childrenIndices) {
            stack.push_back( { idx, si.nestingLevel+1, startValue } );
            startValue += prof.data[idx].value;
        }

        // Hovered case: highlight + tooltip + clicks
        if(isHovered) {
            // Hover callback
            setScopeHighlight(prof.threadId, prof.startTimeNs, prof.startTimeNs+prof.timeRangeNs,
                             item.flags, item.nestingLevel, item.nameIdx, true);

            // Double click callback
            if(ImGui::IsMouseDoubleClicked(0)) {
                synchronizeNewRange(prof.syncMode, item.firstStartTimeNs-0.1*item.firstRangeNs, 1.2*item.firstRangeNs);
                ensureThreadVisibility(prof.syncMode, prof.threadId);
                synchronizeText(prof.syncMode, prof.threadId, item.nestingLevel, item.scopeLIdx, prof.startTimeNs, prof.uniqueId);
            }

            // Tooltip
            if(!isToolTipAlreadyDisplayed) {
                isToolTipAlreadyDisplayed = true;
                double dataCol1Width = 0., dataCol2Width = 0.;
                // Analyse children
                double timeInChildrenNs = 0.;
                bsVec<int> oci; oci.reserve(1+item.childrenIndices.size());
                for(int cIdx : item.childrenIndices) {
                    const ProfileData& cpd = prof.data[cIdx];
                    dataCol1Width = bsMax(dataCol1Width, (double)ImGui::CalcTextSize(cpd.name.toChar()).x);
                    dataCol2Width = bsMax(dataCol2Width, (double)ImGui::CalcTextSize((getValueString(prof, cpd.value)+" (100.0%% parent)").toChar()).x);
                    timeInChildrenNs += cpd.value;
                    oci.push_back(cIdx);
                }
                std::sort(oci.begin(), oci.end(), [&prof](int& a, int& b)->bool { return prof.data[a].value>prof.data[b].value; });
                // Make header
                char tmpStr[256];
                snprintf(tmpStr, sizeof(tmpStr), "%.1f%% in %d child%s", 100.*timeInChildrenNs/item.value,
                         oci.size(), (oci.size()>1)? "ren" : "");
                double headerWidth = bsMax((double)ImGui::CalcTextSize(tmpStr).x, dataCol1Width+dataCol2Width+2*dataColMargin);
                if(!oci.empty()) ImGui::SetNextWindowSize(ImVec2(headerWidth,
                                                                 ImGui::GetTextLineHeightWithSpacing()*(oci.size()+4+(item.extraInfos.empty()?0:1))));
                ImGui::BeginTooltip();
                ImGui::TextColored(vwConst::gold, "%s { %s }", item.name.toChar(), getValueString(prof, item.value).toChar());
                if(!item.extraInfos.empty()) ImGui::Text("%s", item.extraInfos.toChar());
                ImGui::Text("%.1f%% total in %d %s%s", 100.*item.value/prof.data[0].value, item.callQty,
                            prof.callName.toChar(), (item.callQty>1)?"s":"");
                // Display children
                if(!oci.empty()) {
                    ImGui::Separator();
                    ImGui::Text("%s", tmpStr);
                    ImGui::Columns(2);
                    ImGui::SetColumnWidth(0, dataCol1Width+dataColMargin);
                    ImGui::SetColumnWidth(1, dataCol2Width+dataColMargin);
                    for(int cIdx : oci) {
                        const ProfileData& cpd = prof.data[cIdx];
                        ImGui::Text("%s",cpd.name.toChar()); ImGui::NextColumn();
                        double ratio = (double)cpd.value/(double)item.value;
                        snprintf(tmpStr, sizeof(tmpStr), "%s (%.1f%% parent)", getValueString(prof, cpd.value).toChar(), 100.*ratio);
                        ImGui::ProgressBar(ratio, ImVec2(-1,ImGui::GetTextLineHeight()), tmpStr);
                        ImGui::NextColumn();
                    }
                    ImGui::Columns(1);
                }
                ImGui::EndTooltip();
            } // End of drawing tooltip

            // Click = select the scope
            if(!prof.isDragging && ImGui::IsMouseReleased(0)) {
                prof.startValue = si.startValue;
                prof.endValue   = si.startValue+item.value;
            }

            // Right click = callback
            if(!prof.isDragging && isWindowHovered && si.idx!=0 && ImGui::IsMouseReleased(2)) { // si.idx==0 is artificial <Top> node
                prof.cmDataIdx = si.idx;
                ImGui::OpenPopup("profile menu");
                _plotMenuItems.clear(); // Reset the popup menu state
                const ProfileData& d = prof.data[si.idx];
                prepareGraphContextualMenu(prof.threadId, d.nestingLevel, d.scopeLIdx, prof.startTimeNs, prof.timeRangeNs);
            }
        }
    } // End of loop on stack

    // Visible range bar
    const double vrbStartPix = winPosX+winWidth*prof.startValue/prof.data[0].value;
    const double vrbEndPix   = vrbStartPix+bsMax(3., winWidth*(prof.endValue-prof.startValue)/prof.data[0].value);
    DRAWLIST->AddRectFilled(ImVec2(winPosX, winPosY), ImVec2(winPosX+winWidth, winPosY+topBarHeight),  vwConst::uGrey);
    DRAWLIST->AddRectFilled(ImVec2(vrbStartPix, winPosY+4), ImVec2(vrbEndPix, winPosY+topBarHeight-4), vwConst::uGrey128);

    // Set the IMGUI cursor to enable vertical scrolling
    ImGui::SetCursorPosY(fontHeight*(prof.maxNestingLevel+2));

    // Navigation
    // ==========
    bool hasKeyboardFocus = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Range zoom with scroll wheel
    constexpr double vScrollPixPerTick = 50.;
    ImGuiIO& io    = ImGui::GetIO();
    int deltaWheel = (int)io.MouseWheel;
    if(hasKeyboardFocus) {      // Up/Down keys are equivalent to the wheel
        if(ImGui::IsKeyPressed(KC_Up))   deltaWheel =  1;
        if(ImGui::IsKeyPressed(KC_Down)) deltaWheel = -1;
    }
    if(isWindowHovered && deltaWheel!=0) {
        // Ctrl: (Horizontal) range zoom
        if(io.KeyCtrl) {
            deltaWheel *= getConfig().getHWheelInversion();
            const double scrollFactor = 1.25;
            double oldRange = prof.getEndValue()-prof.getStartValue();
            double newRange = oldRange;
            while(deltaWheel>0) { newRange /= scrollFactor; --deltaWheel; }
            while(deltaWheel<0) { newRange *= scrollFactor; ++deltaWheel; }
            if(newRange<prof.minRange) newRange = prof.minRange;
            double newStartValue = prof.getStartValue()+(mouseX-winPosX)/winWidth*(oldRange-newRange);
            prof.setView(bsMax(0., newStartValue), bsMin(prof.data[0].value, newStartValue+newRange));
        }
        // No Ctrl: standard vertical scrolling
        else ImGui::SetScrollY(ImGui::GetScrollY()-deltaWheel*getConfig().getVWheelInversion()*vScrollPixPerTick);
    }

    // Two start time dragging: 1) on data scopes (real dragging)   2) on range bar (just set the start time)
    if(prof.dragMode==DATA || (isWindowHovered && prof.dragMode!=BAR && mouseY>winPosY+topBarHeight)) {
        if(ImGui::IsMouseDragging(2)) { // Data dragging
            prof.isDragging = true;
            if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1. || bsAbs(ImGui::GetMouseDragDelta(2).y)>1) {
                double range = prof.getEndValue()-prof.getStartValue();
                double newStartValue = bsMax(0., prof.getStartValue()-ImGui::GetMouseDragDelta(2).x*range/winWidth);
                double newEndValue = newStartValue+range;
                if(newEndValue>prof.data[0].value) {
                    newStartValue -= bsMax(0., newEndValue-prof.data[0].value);
                    newEndValue    = prof.data[0].value;
                }
                prof.setView(newStartValue, newEndValue);
                ImGui::SetScrollY(ImGui::GetScrollY()-ImGui::GetMouseDragDelta(2).y);
                ImGui::ResetMouseDragDelta(2);
                prof.dragMode = DATA;
            }
        }
        else prof.dragMode = NONE;
    }
    else if(prof.dragMode==BAR || (prof.dragMode==NONE && isWindowHovered)) { // Bar start time forcing
        prof.isDragging = true;
        if(ImGui::IsMouseDragging(2)) {
            if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1.) {
                double range = prof.getEndValue()-prof.getStartValue();
                double newStartValue = bsMax(0., prof.getStartValue()+prof.data[0].value*ImGui::GetMouseDragDelta(2).x/winWidth);
                double newEndValue = newStartValue+range;
                if(newEndValue>prof.data[0].value) {
                    newStartValue -= bsMax(0., newEndValue-prof.data[0].value);
                    newEndValue    = prof.data[0].value;
                }
                prof.setView(newStartValue, newEndValue);
                ImGui::ResetMouseDragDelta(2);
                prof.dragMode = BAR;
            }
        }
        // Else just set the middle time if click outside of the bar
        else if(ImGui::IsMouseDown(0) && (mouseX<vrbStartPix || mouseX>vrbEndPix)) {
            double range = prof.getEndValue()-prof.getStartValue();
            double newStartValue = prof.data[0].value*(mouseX-winPosX)/winWidth-0.5*range;
            prof.endValue   = prof.startValue + range;
            double newEndValue = newStartValue+range;
            if(newEndValue>prof.data[0].value) {
                newStartValue -= bsMax(0., newEndValue-prof.data[0].value);
                newEndValue    = prof.data[0].value;
            }
            prof.setView(newStartValue, newEndValue);
            prof.dragMode = BAR;
        }
        else prof.dragMode = NONE;
    }
    else prof.dragMode = NONE;

    // Arrow keys navigation
    if(hasKeyboardFocus) {
        float step = 0.;
        if(ImGui::IsKeyPressed(KC_Left )) step = -1.;
        if(ImGui::IsKeyPressed(KC_Right)) step = +1.;
        if(step!=0.) {
            if(!ImGui::GetIO().KeyCtrl) step *= 0.25;
            double range         = prof.getEndValue()-prof.getStartValue();
            double newStartValue = bsMax(0., prof.getStartValue()+step*range);
            double newEndValue   = newStartValue+range;
            if(newEndValue>prof.data[0].value) {
                newStartValue -= bsMax(0., newEndValue-prof.data[0].value);
                newEndValue    = prof.data[0].value;
            }
            prof.setView(newStartValue, newEndValue);
        }
    }

    // Middle click: Range drag selection
    if(isWindowHovered && ImGui::IsMouseDragging(1, 0.)) { // Button 1, no sensitivity threshold
        prof.selStartValue = prof.startValue + (mouseX-winPosX-ImGui::GetMouseDragDelta(1).x)/c;
        prof.selEndValue   = prof.startValue + (mouseX-winPosX)/c;
        if(prof.selStartValue>=prof.selEndValue) { prof.selStartValue = prof.selEndValue = 0.; } // Cancel
        else { // Display the selection box with transparency
            float scrolledPosY = winPosY + ImGui::GetScrollY();
            DRAWLIST->AddRectFilled(ImVec2(winPosX+c*(prof.selStartValue-prof.startValue), scrolledPosY),
                                    ImVec2(winPosX+c*(prof.selEndValue-prof.startValue), scrolledPosY+winHeight), IM_COL32(255,255,255,128));
        }
    }
    else if(prof.selEndValue>0.) {
        // Set the selected range view
        prof.setView(prof.selStartValue, bsMax(prof.selEndValue, prof.selEndValue+1000.)); // 1µs at least
        prof.selStartValue = prof.selEndValue = 0.;
    }

    // "Enter" repeats last search
    if(isWindowHovered && prof.lastSearchedNameIdx!=PL_INVALID && ImGui::IsKeyPressed(KC_Enter)) prof.notifySearch(prof.lastSearchedNameIdx);

    // Sanity
    if(prof.startValue<0.)               prof.startValue = 0;
    if(prof.endValue>prof.data[0].value) prof.endValue   = prof.data[0].value;
}


void
vwMain::Profile::notifySearch(u32 searchedNameIdx)
{
    double firstStartValue = -1;
    double firstEndValue   = -1;
    int    firstSearchedItemIdx = -1;
    workStack.clear();
    workStack.push_back( { 0, 0, 0. } );
    if(searchedNameIdx!=lastSearchedNameIdx) {
        lastSearchedNameIdx = searchedNameIdx;
        lastSearchedItemIdx = -1;
    }
    if(searchedNameIdx==PL_INVALID) return;

    // Loop on stack (only way to get the start date of each element)
    while(!workStack.empty()) {
        // Pop the next element
        ProfileStackItem si = workStack.back(); workStack.pop_back();
        const ProfileData& item = data[si.idx];

        // Matches the searched name ?
        if(item.nameIdx==searchedNameIdx) {
            // Automata to browse all the matching items
            if(firstStartValue<0) {
                firstStartValue = si.startValue;
                firstEndValue   = si.startValue+item.value;
                firstSearchedItemIdx = si.idx;
            }
            if(lastSearchedItemIdx<0) {
                startValue = si.startValue;
                endValue   = si.startValue+item.value;
                lastSearchedItemIdx = si.idx;
                return;
            }
            if(si.idx==lastSearchedItemIdx) lastSearchedItemIdx = -1;
        }

        // Propagate to the children
        double startValue = si.startValue;
        for(int idx : item.childrenIndices) {
            workStack.push_back( { idx, si.nestingLevel+1, startValue } );
            startValue += data[idx].value;
        }
    }

    // Wrap on the first match found
    if(firstStartValue>=0) {
        startValue = firstStartValue;
        endValue   = firstEndValue;
        lastSearchedItemIdx = firstSearchedItemIdx;
    }
    else lastSearchedItemIdx = -1;
}
