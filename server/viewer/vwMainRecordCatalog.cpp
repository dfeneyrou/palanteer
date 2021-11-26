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

// This file implements the catalog and record statistic views

// System
#include <cinttypes>
#include <algorithm>

// External
#include "imgui.h"
#include "imgui_internal.h" // For the ImGui::IsKeyPressedMap

// Internal
#include "bsOs.h"
#include "cmLiveControl.h"
#include "cmRecording.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwConfig.h"
#include "vwFileDialog.h"


// @#BUG Key 'F' does not work on record window on error table. KB focus issue? Is this true for all tables?
// @#TODO Support multiselection (handly to delete a bunch of old records, all with a build name)

void
vwMain::drawRecord(void)
{
    // New window
    if(_uniqueIdFullScreen>=0 && _recordWindow.uniqueId!=_uniqueIdFullScreen) return;
    if(_recordWindow.isNew) {
        _recordWindow.isNew = false;
        if(_recordWindow.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(_recordWindow.newDockId);
        else selectBestDockLocation(false, true);
    }
    if(_recordWindow.isWindowSelected) {
        _recordWindow.isWindowSelected = false;
        ImGui::SetNextWindowFocus();
    }
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "Record###%d", _recordWindow.uniqueId);

    bool isOpen = true;
    if(!ImGui::Begin(tmpStr, (_underRecordRecIdx>=0)? 0 : &isOpen, // Forced record window when recording
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    if(!isOpen) {
        getConfig().setWindowRecordVisibility(false);
        setFullScreenView(-1);
        ImGui::End();
        return;
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if(ImGui::CollapsingHeader("Statistics")) {

        if(_record) {
            // Get some infos
            const RecordInfos* ri = 0;
            if(_underDisplayAppIdx>=0 && _underDisplayRecIdx>=0) {
                ri = &_cmRecordInfos[_underDisplayAppIdx].records[_underDisplayRecIdx];
            }
            int lastDelimiter = _record->recordPath.rfindChar(PL_DIR_SEP_CHAR);
            bsString basename = (lastDelimiter>=0)? _record->recordPath.subString(lastDelimiter+1) : _record->recordPath;

#define DISPLAY_STAT(titleText, fmt, ...)                             \
            ImGui::TableNextColumn(); ImGui::Text(titleText);           \
            ImGui::TableNextColumn(); ImGui::TextColored(vwConst::grey, fmt, ##__VA_ARGS__);

            if(ImGui::BeginTable("##tableRecord1", 2)) {
                ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                DISPLAY_STAT("Application", "%s", _record->appName.toChar());
                if(ri && ri->nickname[0]!=0) { DISPLAY_STAT("Nickname", "%s", ri->nickname); }
                DISPLAY_STAT("File", "%s", basename.toChar());
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _record->recordPath.toChar());
                DISPLAY_STAT("File size", "%s", getNiceByteSize(_record->recordByteQty));
                DISPLAY_STAT("Compressed", "%s", _record->compressionMode? "Yes":"No");
                DISPLAY_STAT("Duration", "%s", getNiceDuration(_record->durationNs));
                DISPLAY_STAT("Unique strings", "%d", _record->getStrings().size());
                DISPLAY_STAT("Plottable elements", "%d", _record->elems.size());
                DISPLAY_STAT("Streams", "%d%s", _record->streams.size(), (_record->streams.size()==1 && _record->isMultiStream)?" (MultiStream)":"");
                ImGui::Separator();

                // Display the streams
                for(int streamId=0; streamId<_record->streams.size(); ++streamId) {
                    const cmStreamInfo& si = _record->streams[streamId];
                    ImGui::TableNextColumn();
                    if(_record->isMultiStream) {
                        snprintf(tmpStr, sizeof(tmpStr), "Options for stream '%s'", si.appName.toChar());
                    } else {
                        snprintf(tmpStr, sizeof(tmpStr), "Options");
                    }
                    ImGui::PushID(streamId);
                    bool isOptionNodeOpen = ImGui::TreeNodeEx(tmpStr, ImGuiTreeNodeFlags_SpanFullWidth);
                    ImGui::TableNextColumn();
                    if(isOptionNodeOpen) {
                        if(!si.buildName.empty()) { DISPLAY_STAT("Build name", "%s", si.buildName.toChar()); }
                        if(!si.langName.empty())  { DISPLAY_STAT("Language",   "%s", si.langName.toChar()); }
                        DISPLAY_STAT("Remote control", "%s",   si.tlvs[PL_TLV_HAS_NO_CONTROL]? "No":"Yes");
                        DISPLAY_STAT("External strings", "%s", si.tlvs[PL_TLV_HAS_EXTERNAL_STRING]? "Yes":"No");
                        DISPLAY_STAT("Compact model", "%s",    si.tlvs[PL_TLV_HAS_COMPACT_MODEL]? "Yes":"No");
                        DISPLAY_STAT("32 bits clock", "%s",    si.tlvs[PL_TLV_HAS_SHORT_DATE]? "Yes":"No");
                        DISPLAY_STAT("32 bits hash strings", "%s", si.tlvs[PL_TLV_HAS_SHORT_STRING_HASH]? "Yes":"No");
                        DISPLAY_STAT("Hash salt", "%" PRId64,      si.tlvs[PL_TLV_HAS_HASH_SALT]);
                        DISPLAY_STAT("Auto instrumentation", "%s", si.tlvs[PL_TLV_HAS_AUTO_INSTRUMENT]? "Yes":"No");
                        DISPLAY_STAT("Context switches", "%s", si.tlvs[PL_TLV_HAS_CSWITCH_INFO]? "Yes":"No");
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                // Event list
                u32 totalEventQty = bsMax((u32)1, _record->elemEventQty+_record->memEventQty+_record->ctxSwitchEventQty+_record->lockEventQty+_record->markerEventQty);
                ImGui::TableNextColumn();
                bool isEventNodeOpen = ImGui::TreeNodeEx("Events", ImGuiTreeNodeFlags_SpanFullWidth);
                ImGui::TableNextColumn(); ImGui::TextColored(vwConst::grey, "%s", getNiceBigPositiveNumber(totalEventQty));

                if(isEventNodeOpen) {
#define DUMP_STAT_EVENT(name, qty)                                      \
                    ImGui::TableNextColumn(); ImGui::Text(name); ImGui::TableNextColumn(); \
                    ImGui::TextColored(vwConst::grey, "%s events (%d%%)", getNiceBigPositiveNumber(qty), \
                                       (int)((100LL*(qty)+totalEventQty/2)/totalEventQty))
                    DUMP_STAT_EVENT("Generic",    _record->elemEventQty);
                    DUMP_STAT_EVENT("Memory",     _record->memEventQty);
                    DUMP_STAT_EVENT("Lock",       _record->lockEventQty);
                    DUMP_STAT_EVENT("Marker",     _record->markerEventQty);
                    DUMP_STAT_EVENT("Ctx switch", _record->ctxSwitchEventQty);
                    ImGui::TreePop();
                }

                // Thread list
                ImGui::TableNextColumn();
                bool isThreadNodeOpen = ImGui::TreeNodeEx("Threads", ImGuiTreeNodeFlags_SpanFullWidth);
                ImGui::TableNextColumn(); ImGui::TextColored(vwConst::grey, "%d", _record->threads.size());
                if(isThreadNodeOpen) {
                    // Loop on thread layout instead of direct thread list, as the layout have sorted threads
                    for(int layoutIdx=0; layoutIdx<getConfig().getLayout().size(); ++layoutIdx) {
                        const vwConfig::ThreadLayout& ti = getConfig().getLayout()[layoutIdx];
                        if(ti.threadId>=cmConst::MAX_THREAD_QTY) continue;
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", getFullThreadName(ti.threadId));
                        const auto& t = _record->threads[ti.threadId];
                        u32 threadEventQty = t.elemEventQty+t.memEventQty+t.ctxSwitchEventQty+t.lockEventQty+t.markerEventQty;
                        ImGui::TableNextColumn();
                        ImGui::TextColored(vwConst::grey, "%s events (%d%%)", getNiceBigPositiveNumber(threadEventQty),
                                           (int)((100LL*threadEventQty+totalEventQty/2)/totalEventQty));
                    }
                    ImGui::TreePop();
                }

                ImGui::EndTable();
            } // End of table

        } // if(_record)
        else ImGui::TextColored(vwConst::grey, "(No record loaded)");

    } // Statistics collapsible header

    ImGui::Dummy(ImVec2(1, 0.5f*ImGui::GetTextLineHeight()));

    if(_recordWindow.doForceShowLive) {
        _recordWindow.doForceShowLive = false;
        ImGui::SetNextItemOpen(true);
    }
    if(ImGui::CollapsingHeader("Live control")) {
        if(_underRecordRecIdx<0) {
            // Not connected case
            ImGui::TextColored(vwConst::grey, "(No program connected)");
        }
        else {
            // Connected case: display the recorded program name and the state
            ImGui::Text("Running"); ImGui::SameLine();
            ImGui::TextColored(vwConst::gold, "'%s'", _record->appName.toChar());

            // Kill button
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth()-ImGui::CalcTextSize("Kill").x-2*ImGui::GetStyle().ItemSpacing.x);
            if(ImGui::Button("Kill")) ImGui::OpenPopup("Kill program");
            if(ImGui::BeginPopupModal("Kill program", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Really kill the running program?\n\n");
                ImGui::Separator();
                if(ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter)) {
                    for(int streamId=0; streamId<_newStreamQty; ++streamId) {
                        _live->remoteKillProgram(streamId);
                    }
                    plMarker("menu", "Kill program");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if(ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
    } // Live collapsible header
    ImGui::Dummy(ImVec2(1, 0.5f*ImGui::GetTextLineHeight()));

    if(_record && _record->errorQty) {

        if(ImGui::CollapsingHeader("Instrumentation errors")) {
            // Display the recording errors of the current record
            if(ImGui::BeginTable("##table profile", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollX |
                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Thread");
                ImGui::TableSetupColumn("Event name");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("File");
                ImGui::TableSetupColumn("Line");
                ImGui::TableHeadersRow();

                // Sort errors if required
                if(ImGuiTableSortSpecs* sortsSpecs= ImGui::TableGetSortSpecs()) {
                    if(sortsSpecs->SpecsDirty) {
                        if(sortsSpecs->SpecsCount>0) {
                            s64 direction = (sortsSpecs->Specs->SortDirection==ImGuiSortDirection_Ascending)? 1 : -1;
                            if(sortsSpecs->Specs->ColumnIndex==0) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*((int)a.type-(int)b.type)<=0; } );
                            }
                            if(sortsSpecs->Specs->ColumnIndex==1) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction, this](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*(_record->getString(_record->threads[a.threadId].nameIdx).alphabeticalOrder-
                                                                     _record->getString(_record->threads[b.threadId].nameIdx).alphabeticalOrder)<=0; } );
                            }
                            if(sortsSpecs->Specs->ColumnIndex==2) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction, this](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*(_record->getString(a.nameIdx).alphabeticalOrder-
                                                                     _record->getString(b.nameIdx).alphabeticalOrder)<=0; } );
                            }
                            if(sortsSpecs->Specs->ColumnIndex==3) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*((int)a.count-(int)b.count)<=0; } );
                            }
                            if(sortsSpecs->Specs->ColumnIndex==4) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction, this](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*(_record->getString(a.filenameIdx).alphabeticalOrder-
                                                                     _record->getString(b.filenameIdx).alphabeticalOrder)<=0; } );
                            }
                            if(sortsSpecs->Specs->ColumnIndex==5) {
                                std::stable_sort(_record->errors, _record->errors+_record->errorQty,
                                                 [direction](const cmRecord::RecError& a, const cmRecord::RecError& b)->bool
                                                 { return direction*((int)a.lineNbr-(int)b.lineNbr)<=0; } );
                            }
                         }
                        sortsSpecs->SpecsDirty = false;
                    }
                }

                for(u32 i=0; i<_record->errorQty; ++i) {
                    const cmRecord::RecError& e = _record->errors[i];
                    // Type
                    ImGui::TableNextColumn();
                    switch(e.type) {
                    case cmRecord::ERROR_MAX_THREAD_QTY_REACHED:
                        ImGui::Text("Maximum thread quantity %d reached", cmConst::MAX_THREAD_QTY);
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The program uses too much threads.\nThe last ones will be ignored.");
                        break;
                    case cmRecord::ERROR_TOP_LEVEL_REACHED:
                        ImGui::Text("Unbalanced begin/end blocks");
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Some extra scope END are breaking the scope hierarchy.\nplScope is easier to use and may prevent these kind of errors.");
                        break;
                    case cmRecord::ERROR_MAX_LEVEL_QTY_REACHED:
                        ImGui::Text("Maximum nesting level quantity (%d) reached", cmConst::MAX_LEVEL_QTY);
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Either the instrumentation stack is too deep, either Some END scope are missing.\nplScope() is easier to use and may prevent these kind of errors.");
                        break;
                    case cmRecord::ERROR_EVENT_OUTSIDE_SCOPE:
                        ImGui::Text("Dropped data events because outside a scope");
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("All data events shall be nested inside a scope.\nMove these data or scope them in a scope.");
                        break;
                    case cmRecord::ERROR_MISMATCH_SCOPE_END:
                        ImGui::Text("End scope name does not match the begin scope");
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The name given in plEnd shall match the one in plBegin.\nAlso check that no plEnd call is missing in some cases.");
                        break;
                    default:
                        break;
                    }

                    // Thread and event names
                    ImGui::TableNextColumn();
                    if(e.type==cmRecord::ERROR_MAX_THREAD_QTY_REACHED) {
                        ImGui::Text("%s", _record->getString(e.nameIdx).value.toChar());
                        ImGui::TableNextColumn();
                        ImGui::Text(" - ");
                    } else {
                        ImGui::Text("%s", getFullThreadName(e.threadId));
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", _record->getString(e.nameIdx).value.toChar());
                    }

                    // Count
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", e.count);

                    // File and line number
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", (e.filenameIdx==PL_INVALID)? "N/A (marker)" : _record->getString(e.filenameIdx).value.toChar());
                    if(e.filenameIdx!=PL_INVALID && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _record->getString(e.filenameIdx).value.toChar());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", e.lineNbr);
                }
            } // End of table
            ImGui::EndTable();
        } // Instrumentation error collapsible header

    }

    // Check full screen
    if(ImGui::IsWindowHovered() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(_recordWindow.uniqueId);
    }

    ImGui::End();
}




void
vwMain::drawCatalog(void)
{
    if(_uniqueIdFullScreen>=0 && _catalogWindow.uniqueId!=_uniqueIdFullScreen) return;
    if(_catalogWindow.isNew) {
        _catalogWindow.isNew = false;
        if(_catalogWindow.newDockId!=0xFFFFFFFF) ImGui::SetNextWindowDockID(_catalogWindow.newDockId);
        else selectBestDockLocation(false, true);
    }
    if(_catalogWindow.isWindowSelected) {
        _catalogWindow.isWindowSelected = false;
        ImGui::SetNextWindowFocus();
    }
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "Catalog###%d", _catalogWindow.uniqueId);
    bool isOpen = true;
    if(!ImGui::Begin(tmpStr, &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    if(!isOpen) {
        getConfig().setWindowCatalogVisibility(false);
        setFullScreenView(-1);
        ImGui::End();
        return;
    }

    // Loop on all profiled application names
    bsDate now                = osGetDate();
    u64    allRecordTotalSize = 0;
    int    nextHeaderAction   = 0;  // No action
    bool   isAnItemHovered    = false;
    for(AppRecordInfos& appInfo : _cmRecordInfos) {

        if(appInfo.idx==_forceOpenAppIdx) {
            ImGui::SetNextItemOpen(true);
            _forceOpenAppIdx = -1;
        }
        bool doHighlight = (appInfo.idx==_underDisplayAppIdx);
        if(doHighlight) ImGui::PushStyleColor(ImGuiCol_Header, vwConst::uDarkOrange);

        // Get some information on this application
        bool doOpenAppMenu            = false;
        bool doOpenDeleteAppAll       = false;
        bool doOpenDeleteAppAllwoNick = false;
        bool doOpenKeepLast           = false;
        int  countAppWithNickname = 0;
        u64  appRecordTotalSize   = 0;
        bool keepOnlyLastRecordState;
        int  keepOnlyLastRecordQty;
        bsString appExtStringsPath;

        getConfig().getKeepOnlyLastNRecord(appInfo.name, keepOnlyLastRecordState, keepOnlyLastRecordQty);
        getConfig().getExtStringsPath(appInfo.name, appExtStringsPath);
        for(RecordInfos& ri : appInfo.records) {
            if(ri.nickname[0]!=0) ++countAppWithNickname;
            appRecordTotalSize += ri.size;
        }
        allRecordTotalSize += appRecordTotalSize;

        ImGui::PushID(appInfo.idx);
        if(_catalogWindow.headerAction==1) ImGui::SetNextItemOpen(true);
        if(_catalogWindow.headerAction==2) ImGui::SetNextItemOpen(false);

        if(ImGui::TreeNodeEx(appInfo.name.toChar(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
            if(ImGui::IsItemClicked(2)) doOpenAppMenu = true;
            if(ImGui::IsItemHovered()) {
                isAnItemHovered = true;
                if(getLastMouseMoveDurationUs()>500000) {
                    ImGui::SetTooltip("Total size: %s", getNiceByteSize(appRecordTotalSize));
                }
            }

            // Loop on record files for this application
            for(RecordInfos& ri : appInfo.records) {
                // Display it
                char name[128];
                snprintf(name, sizeof(name), "%s - %s   [%s]", getNiceDate(ri.date, now),
                         ri.nickname[0]? ri.nickname: "<no name>", getNiceByteSize(ri.size));
                ImGui::PushID(&ri);
                ImGui::Bullet(); ImGui::SameLine();
                if(ImGui::Selectable(name, doHighlight && ri.idx==_underDisplayRecIdx, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if(ImGui::IsMouseDoubleClicked(0)) {
                        _msgRecordLoad.t1GetFreeMsg()->recordPath = ri.path;
                        _msgRecordLoad.t1Send();
                    }
                }
                if(ImGui::IsItemHovered()) isAnItemHovered = true;

                // Record contextual menu
                static RecordInfos* openedRecord = 0;
                bool doOpenDeletePopup = false;
                if(ImGui::BeginPopupContextItem("Record file menu", 2)) {
                    ImGui::TextColored(vwConst::gold, "%s", getNiceDate(ri.date, now));
                    ImGui::Separator();

                    // Load the record
                    ImGui::Separator();
                    if(ImGui::MenuItem("Load record")) {
                        _msgRecordLoad.t1GetFreeMsg()->recordPath = ri.path;
                        _msgRecordLoad.t1Send();
                        plMarker("menu", "Load record");
                        ImGui::CloseCurrentPopup();
                    }

                    // Delete the record
                    ImGui::Separator();
                    if(ImGui::MenuItem("Delete record")) doOpenDeletePopup = true;

                    // Nickname
                    ImGui::Separator();
                    static char localBuffer[sizeof(ri.nickname)+1];
                    if(!openedRecord) {
                        openedRecord = &ri;
                        memcpy(localBuffer, &ri.nickname[0], sizeof(ri.nickname));
                    }
                    bool isChanged = (strncmp(&ri.nickname[0], localBuffer, sizeof(ri.nickname))!=0);
                    if(isChanged) ImGui::PushStyleColor(ImGuiCol_FrameBg, vwConst::darkBlue);
                    ImGui::Text("Nickname"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(150);
                    bool doCloseAndSave = ImGui::InputText("##Nickname", &localBuffer[0], sizeof(ri.nickname), ImGuiInputTextFlags_EnterReturnsTrue);
                    if(isChanged) ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if(doCloseAndSave || ImGui::SmallButton("OK")) {
                        memcpy(&ri.nickname[0], localBuffer, sizeof(ri.nickname));
                        ri.nickname[sizeof(ri.nickname)-1] = 0;
                        if(ri.nickname[0]==0) {
                            // Remove the nickname file
                            osRemoveFile(ri.path.subString(0, ri.path.size()-4)+"_nickname");
                        } else {
                            // Update the nickname file
                            FILE* fh = osFileOpen(ri.path.subString(0, ri.path.size()-4)+"_nickname", "wb");
                            if(fh) {
                                fwrite(&ri.nickname[0], 1, strlen(ri.nickname)+1, fh);
                                fclose(fh);
                            }
                        }
                        plMarker("menu", "Changed record nickname");
                        ImGui::CloseCurrentPopup();
                        openedRecord = 0;
                        dirty();
                    }

                    // External string lookup update
                    if(!appExtStringsPath.empty()) {
                        ImGui::Separator();
                        if(ImGui::MenuItem("Update the external strings lookup content"))  {
                            osCopyFile(appExtStringsPath, ri.path.subString(0, ri.path.size()-4)+"_externalStrings");
                            ImGui::CloseCurrentPopup();
                        }
                        if(ImGui::IsItemHovered() && !appExtStringsPath.empty()) ImGui::SetTooltip("from %s", appExtStringsPath.toChar());
                    }

                    // End the contextual menu
                    ImGui::EndPopup();
                }
                // No contextual menu
                else if(openedRecord==&ri) {
                    openedRecord = 0;
                }

                // Modal popup to confirm the deletion
                if(doOpenDeletePopup) ImGui::OpenPopup("Delete a record");
                if(ImGui::BeginPopupModal("Delete a record", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Really delete this record?\n  %s\n", ri.path.toChar());
                    ImGui::Separator();
                    if(ImGui::Button("OK", ImVec2(120, 0))) {
                        _recordsToDelete.push_back(ri.path);
                        plMarker("menu", "Delete one record");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if(ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                    ImGui::EndPopup();
                }

                // Tooltip on the record
                if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
                    ImGui::SetNextWindowSize(ImVec2(300, 0.0));
                    ImGui::BeginTooltip();
                    ImGui::TextColored(vwConst::gold, "%s", appInfo.name.toChar());
                    ImGui::Text("%s", getNiceDate(ri.date, now));
                    ImGui::Separator();
                    if(ImGui::BeginTable("##tablecatalog", 2)) {
                        if(ri.nickname[0]!=0) {
                            ImGui::TableNextColumn();
                            ImGui::Text("Nickname");
                            ImGui::TableNextColumn();
                            ImGui::TextColored(vwConst::grey, "%s", &ri.nickname[0]);
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("Size");
                        ImGui::TableNextColumn();
                        ImGui::TextColored(vwConst::grey, "%s", getNiceByteSize(ri.size));
                        ImGui::EndTable();
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            } // End of loop on records
            ImGui::TreePop();
        }
        else {
            if(ImGui::IsItemClicked(2)) doOpenAppMenu = true;
            if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
                ImGui::SetTooltip("Total size: %s", getNiceByteSize(appRecordTotalSize));
            }
        }


        // Application menu
        // ================

        if(doOpenAppMenu) ImGui::OpenPopup("Record app menu");
        if(ImGui::BeginPopup("Record app menu", ImGuiWindowFlags_AlwaysAutoResize)) {

            // Header
            ImGui::TextColored(vwConst::gold, "%s", appInfo.name.toChar()); ImGui::SameLine();
            ImGui::Text(" (%d records)", appInfo.records.size());
            ImGui::Separator();
            ImGui::Separator();

            // Keep only the last N records without nickname
            ImGui::Checkbox("Keep only last", &keepOnlyLastRecordState); ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x);
            ImGui::InputInt("##Kept qty", &keepOnlyLastRecordQty, 0, 0); ImGui::SameLine();
            ImGui::Text("records without nicknames");
            keepOnlyLastRecordQty = bsMinMax(keepOnlyLastRecordQty, 2, 999);
            getConfig().setKeepOnlyLastNRecord(appInfo.name, keepOnlyLastRecordState, keepOnlyLastRecordQty);

            ImGui::Separator();
            if(countAppWithNickname>0 && countAppWithNickname!=appInfo.records.size() &&
               ImGui::MenuItem("Delete all records without nicknames")) doOpenDeleteAppAllwoNick = true;
            if(appInfo.records.size()-countAppWithNickname>keepOnlyLastRecordQty) {
                snprintf(tmpStr, sizeof(tmpStr), "Remove last %d records without nickname", appInfo.records.size()-countAppWithNickname-keepOnlyLastRecordQty);
                if(ImGui::MenuItem(tmpStr)) doOpenKeepLast = true;
            }
            if(ImGui::MenuItem("Delete all records")) doOpenDeleteAppAll = true;


            // External string
            ImGui::Separator();
            ImGui::Separator();
            if(ImGui::MenuItem(appExtStringsPath.empty()? "Set pathname of the external strings lookup" : "Update pathname of the external strings lookup")) {
                _fileDialogExtStrings->open(getConfig().getLastFileExtStringsPath());
                dirty();
            }
            if(ImGui::IsItemHovered() && !appExtStringsPath.empty()) ImGui::SetTooltip("%s", appExtStringsPath.toChar());

            if(!appExtStringsPath.empty() && ImGui::MenuItem("Unset pathname of the  external strings lookup")) {
                getConfig().setExtStringsPath(appInfo.name, "");
            }

            // Collapse/open headers
            ImGui::Separator();
            ImGui::Separator();
            if(ImGui::MenuItem("Open all headers"))     nextHeaderAction = 1;
            if(ImGui::MenuItem("Collapse all headers")) nextHeaderAction = 2;

            ImGui::EndPopup();
        }

        // Modal popup to confirm the deletion of all records
        if(doOpenDeleteAppAll) ImGui::OpenPopup("Delete all record");
        if(ImGui::BeginPopupModal("Delete all record", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Really delete the %d records?\n\n", appInfo.records.size());
            ImGui::Separator();
            if(ImGui::Button("OK", ImVec2(120, 0))) {
                for(RecordInfos& ri : appInfo.records) _recordsToDelete.push_back(ri.path);
                plMarker("menu", "Delete all records");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // Modal popup to confirm the deletion of all records without nicknames
        if(doOpenDeleteAppAllwoNick) ImGui::OpenPopup("Delete all record without nickname");
        if(ImGui::BeginPopupModal("Delete all record without nickname", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Really delete the %d records?\n\n", appInfo.records.size()-countAppWithNickname);
            ImGui::Separator();
            if(ImGui::Button("OK", ImVec2(120, 0))) {
                for(RecordInfos& ri : appInfo.records) if(ri.nickname[0]==0) _recordsToDelete.push_back(ri.path);
                plMarker("menu", "Delete all records without nickname");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // Modal popup to confirm the deletion of all records without nicknames
        if(doOpenKeepLast) ImGui::OpenPopup("Keep only last records");
        if(ImGui::BeginPopupModal("Keep only last records", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Really delete the %d records?\n\n", appInfo.records.size()-countAppWithNickname-keepOnlyLastRecordQty);
            ImGui::Separator();
            if(ImGui::Button("OK", ImVec2(120, 0))) {
                int skipQty = keepOnlyLastRecordQty;
                for(RecordInfos& ri : appInfo.records)
                    if(ri.nickname[0]==0 && --skipQty<0) _recordsToDelete.push_back(ri.path);
                plMarker("menu", "Delete last N records");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // Handle the external string file dialog
        if(_fileDialogExtStrings->draw(getConfig().getFontSize())) dirty();
        if(_fileDialogExtStrings->hasSelection()) {
            const bsVec<bsString>& result = _fileDialogExtStrings->getSelection();
            getConfig().setExtStringsPath(appInfo.name, result[0]);
            getConfig().setLastFileExtStringsPath(result[0]);
            _fileDialogExtStrings->clearSelection();
        }


        ImGui::PopID();
        if(doHighlight) ImGui::PopStyleColor();
    } // End of loop on applications

    _catalogWindow.headerAction = nextHeaderAction;

    // Display the total size of all records
    ImGui::Spacing();
    ImGui::TextColored(vwConst::grey, "Total record size: %s", getNiceByteSize(allRecordTotalSize));
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _storagePath.toChar());

    // Refresh menu
    if(!isAnItemHovered && ImGui::IsWindowHovered() && !ImGui::IsPopupOpen("refresh menu") && ImGui::IsMouseReleased(2)) {
        ImGui::OpenPopup("refresh menu");
    }
    if(ImGui::BeginPopup("refresh menu", ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::MenuItem("Refresh record list")) {
            updateRecordList();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Check full screen
    if(ImGui::IsWindowHovered() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(_catalogWindow.uniqueId);
    }

    ImGui::End();
}
