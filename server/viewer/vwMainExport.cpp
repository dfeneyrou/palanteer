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

// This file implements the functions and automata for all kinds of export

// System
#include <inttypes.h>

// External
#include "palanteer.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#define STBIW_ASSERT(x) plAssert(x)
#include "stb_image_write.h"      // To save screenshots
#include "imgui.h"
#include "imgui_internal.h" // For the ImGui::IsKeyPressedMap

// Internal
#include "cmRecord.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwConfig.h"
#include "vwPlatform.h"
#include "vwFileDialog.h"

/*
  1) Finish the text export in text views
    - add support for computation per chunk
    - add the GUI automata to ask for a filename and check for already existing one
    - write in a file, not on screen
  2) Export marker views. Same interface and behavior than text.
  3) Export the plot & histogram  as CSV
  4?) Export the profile view ?
*/

// Initiate an export automata
// ===========================

void
vwMain::initiateExportCTF(void)
{
    if(_isExportOnGoing || _backgroundComputationInUse || _exportCTF.state!=IDLE || !_record) return;

    bsString filenameProposal = osGetDirname(getConfig().getLastFileExportPath())+bsString(PL_DIR_SEP)+_record->appName+".json";
    _fileDialogExportChromeTF->open(filenameProposal);
    _exportCTF.state = FILE_DIALOG;
    _isExportOnGoing = true;
    _backgroundComputationInUse = true;
}


// If startNestingLevel<0, then startTimeNs is used and shall not be negative
// If qty or endTimeNs are negative, they are ignored
void
vwMain::initiateExportText(int threadId, s64 startTimeNs, int startNestingLevel, u32 startLIdx, s64 endTimeNs, int dumpedQty)
{
    if(_isExportOnGoing || _backgroundComputationInUse || _exportText.state!=IDLE || !_record) return;

    // If no start index is provided, we compute it from the provided start date
    if(startNestingLevel<0) {
        plAssert(startTimeNs>=0);
        cmGetRecordPosition(_record, threadId, startTimeNs, startNestingLevel, startLIdx);
    }
    _exportText.it.init(_record, threadId, startNestingLevel, startLIdx);

    bsString filenameProposal = osGetDirname(getConfig().getLastFileExportPath())+bsString(PL_DIR_SEP)+
        (_record->appName + bsString("_") + _record->getString(_record->threads[threadId].nameIdx).value + ".txt").filterForFilename();
    _fileDialogExportText->open(filenameProposal);
    _exportText.state       = FILE_DIALOG;
    _exportText.startTimeNs = startTimeNs;
    _exportText.endTimeNs   = endTimeNs;
    _exportText.dumpedQty   = dumpedQty;
    _isExportOnGoing        = true;
    _backgroundComputationInUse = true;
}


void
vwMain::initiateExportPlot(int elemIdx, s64 startTimeNs, s64 endTimeNs)
{
    if(_isExportOnGoing || _backgroundComputationInUse || _exportPlot.state!=IDLE || !_record) return;

    bsString filenameProposal = osGetDirname(getConfig().getLastFileExportPath())+bsString(PL_DIR_SEP)+
        (_record->appName + bsString("_") + _record->getString(_record->elems[elemIdx].nameIdx).value + ".csv").filterForFilename();
    _fileDialogExportPlot->open(filenameProposal);
    _exportPlot.state       = FILE_DIALOG;
    _exportPlot.elemIdx     = elemIdx;
    _exportPlot.startTimeNs = startTimeNs;
    _exportPlot.endTimeNs   = endTimeNs;
    _isExportOnGoing        = true;
    _backgroundComputationInUse = true;
}


// Handle export automata (file dialog, override confirmation and per chunk computations)
// ======================================================================================

void
vwMain::handleExportCTF(void)
{
    ExportChromeTraceFormat& exp = _exportCTF;

    // Display the file dialog to get the name of the capture
    if(exp.state==FILE_DIALOG) {
        if(_fileDialogExportChromeTF->draw(getConfig().getFontSize())) dirty();
        if(_fileDialogExportChromeTF->hasSelection()) {
            const bsVec<bsString>& result = _fileDialogExportChromeTF->getSelection();
            if(result.empty()) {
                _fileDialogExportChromeTF->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            } else {
                getConfig().setLastFileExportPath(result[0]);
                exp.state = CONFIRMATION_DIALOG;
            }
        }
    }

    if(exp.state==CONFIRMATION_DIALOG) {
        exp.computationLevel = 0;

        if(osFileExists(_fileDialogExportChromeTF->getSelection()[0])) {
            ImGui::OpenPopup("File already exists##CTFFileAlreadyExists");
        } else {
            exp.state = EFFECTIVE_SAVE;
        }

        if(ImGui::BeginPopupModal("File already exists##CTFFileAlreadyExists", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please confirm the file overwrite\n\n");
            ImGui::Separator();
            if(ImGui::Button("Yes", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter)) {
                exp.state = EFFECTIVE_SAVE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                _fileDialogExportChromeTF->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            }
            ImGui::EndPopup();
        }
    }

    if(exp.state==EFFECTIVE_SAVE) {
        // Task initialization
        if(exp.computationLevel==0) {
            // Sanity
            if(!_record || _record->threads.empty()) {
                _fileDialogExportChromeTF->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
                return;
            }

            // Open the export file
            const bsString& filename = _fileDialogExportChromeTF->getSelection()[0];
            exp.fileHandle = osFileOpen(filename, "w");
            if(!exp.fileHandle) {
                notifyErrorForDisplay(ERROR_GENERIC, bsString("Unable to open the file for writing: ") + filename);
                _fileDialogExportChromeTF->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
                return;
            }

            // Write the metadata: thread and process names
            fprintf(exp.fileHandle, "{ \n\"displayTimeUnit\": \"ns\",\n");
            fprintf(exp.fileHandle, "\"traceEvents\": [\n");
            for(int threadId=0; threadId<_record->threads.size(); ++threadId) {
                fprintf(exp.fileHandle,
                        "{\"name\": \"thread_name\", \"ph\": \"M\", \"cat\":\"__metadata\", \"pid\": %d, \"tid\": %d, \"args\": { \"name\": \"%s\" }  },\n",
                        _record->threads[threadId].streamId, threadId, _record->getString(_record->threads[threadId].nameIdx).value.toChar());
            }
            for(int streamId=0; streamId<_record->streams.size(); ++streamId) {
                fprintf(exp.fileHandle,
                        "{\"name\":\"process_name\",\"ph\":\"M\",\"cat\":\"__metadata\",\"pid\":%d,\"tid\":0,\"ts\":0,\"args\":{\"name\":\"%s\"} },\n",
                        streamId, _record->streams[streamId].appName.toChar());
            }

            // Update the export automata state
            _fileDialogExportChromeTF->clearSelection();
            exp.computationLevel = 1;
            exp.it.init(_record, 0, 0, 0);
            ImGui::OpenPopup("In progress##WaitExportCTF");
        }  // End of task initialization

        // Compute during a slice of time
        s64    lastDate = 0;
        bsUs_t endComputationTimeUs = bsGetClockUs() + vwConst::COMPUTATION_TIME_SLICE_US; // Time slice of computation
        while(_record && exp.it.getThreadId()<_record->threads.size()) {

            int threadId = exp.it.getThreadId();
            int streamId = _record->threads[threadId].streamId;
            int nestingLevel; u32 lIdx; s64 scopeEndTimeNs;
            cmRecord::Evt evt;
            bool isIteratorOk = false;

            // Process a batch of events
            int batchSize = 10000;  // Granularity of the time check
            while(--batchSize>=0) {
                if(!(isIteratorOk=exp.it.getItem(nestingLevel, lIdx, evt, scopeEndTimeNs))) break;

                int eType = (evt.flags&PL_FLAG_TYPE_MASK);
                if(evt.flags&PL_FLAG_SCOPE_MASK) {
                    fprintf(exp.fileHandle, "{\"name\": \"%s\", \"cat\": \"%s\", \"ph\": \"%s\", \"pid\": %d, \"tid\": %d, \"ts\": %" PRId64 "},\n",
                            _record->getString(evt.nameIdx).value.toChar(), (eType==PL_FLAG_TYPE_LOCK_WAIT)? "Lock wait":"Scope",
                            (evt.flags&PL_FLAG_SCOPE_BEGIN)? "B":"E", streamId, threadId, evt.vS64);
                    lastDate = evt.vS64;
                }
                else if(eType==PL_FLAG_TYPE_MARKER) {
                    fprintf(exp.fileHandle, "{\"name\": \"%s\", \"ph\": \"i\", \"pid\": %d, \"tid\": %d, \"ts\": %" PRId64 ", \"s\": \"t\"},\n",
                            _record->getString(evt.filenameIdx).value.toChar(), streamId, threadId, evt.vS64);
                    lastDate = evt.vS64;
                }
            }

            // End of batch
            if(!isIteratorOk) exp.it.init(_record, threadId+1, 0, 0); // Go to next thread
            if(bsGetClockUs()>endComputationTimeUs) break;
        }

        // Computations are finished?
        if(!_record || exp.it.getThreadId()>=_record->threads.size()) {
            exp.computationLevel = 100;
        }
        else {
            double threadLevelShare = 100./_record->threads.size();
            double level = threadLevelShare*((double)exp.it.getThreadId() + (double)lastDate/(double)_record->durationNs);
            exp.computationLevel = bsMinMax((int)level, 1, 99); // 0 means just started, 100 means finished
            dirty();  // No idle
        }

        bool openPopupModal = true;
        if(ImGui::BeginPopupModal("In progress##WaitExportCTF",
                                  &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
            char tmpStr[128];
            ImGui::TextColored(vwConst::gold, "Exporting in JSON Chrome Trace Event Format...");
            snprintf(tmpStr, sizeof(tmpStr), "%d %%", exp.computationLevel);
            ImGui::ProgressBar(0.01f*exp.computationLevel, ImVec2(-1,ImGui::GetTextLineHeight()), tmpStr);
            if(exp.computationLevel==100) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(!openPopupModal) exp.computationLevel = 100;  // Cancelled by used

        // End of computation
        if(exp.computationLevel>=100) {
            fprintf(exp.fileHandle, "\n]\n}\n");
            fclose(exp.fileHandle);
            exp.fileHandle = 0;
            exp.state = IDLE;
            _isExportOnGoing = false;
            _backgroundComputationInUse = false;
        }
    } // End of effective saving per chunk
}


void
vwMain::handleExportScreenshot(void)
{
    // Display the file dialog to get the name of the capture
    if(_exportScreenshot.state==FILE_DIALOG) {
        if(_fileDialogExportScreenshot->draw(getConfig().getFontSize())) dirty();
        if(_fileDialogExportScreenshot->hasSelection()) {
            const bsVec<bsString>& result = _fileDialogExportScreenshot->getSelection();
            if(result.empty()) {
                _fileDialogExportScreenshot->clearSelection();
                _exportScreenshot.free();
                _exportScreenshot.state = IDLE;
                _isExportOnGoing = false;
            } else {
                getConfig().setLastFileExportScreenshotPath(result[0]);
                _exportScreenshot.state = CONFIRMATION_DIALOG;
            }
        }
    }

    if(_exportScreenshot.state==CONFIRMATION_DIALOG) {
        if(osFileExists(_fileDialogExportScreenshot->getSelection()[0])) {
            ImGui::OpenPopup("File already exists##screenShotAlreadyExists");
        } else {
            _exportScreenshot.state = EFFECTIVE_SAVE;
        }

        if(ImGui::BeginPopupModal("File already exists##screenShotAlreadyExists", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please confirm the file overwrite\n\n");
            ImGui::Separator();
            if(ImGui::Button("Yes", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter)) {
                _exportScreenshot.state = EFFECTIVE_SAVE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                _fileDialogExportScreenshot->clearSelection();
                _exportScreenshot.free();
                _exportScreenshot.state = IDLE;
                _isExportOnGoing = false;
            }
            ImGui::EndPopup();
        }
    }

    else if(_exportScreenshot.state==EFFECTIVE_SAVE) {
        // Save the image
        stbi_flip_vertically_on_write(1); // Screenshot image saving requires vertical flip (OpenGL...)
        if(!stbi_write_png(_fileDialogExportScreenshot->getSelection()[0].toChar(), _exportScreenshot.width, _exportScreenshot.height, 3,
                           _exportScreenshot.buffer, 3*_exportScreenshot.width)) {
            notifyErrorForDisplay(ERROR_GENERIC, bsString("Unable to properly write the image of the screen capture."));
        }

        _fileDialogExportScreenshot->clearSelection();
        _exportScreenshot.free();
        _exportScreenshot.state = IDLE;
        _isExportOnGoing = false;
    }
}


void
vwMain::handleExportText(void)
{
    ExportText& exp = _exportText;

    // Display the file dialog to get the name of the capture
    if(exp.state==FILE_DIALOG) {
        if(_fileDialogExportText->draw(getConfig().getFontSize())) dirty();
        if(_fileDialogExportText->hasSelection()) {
            const bsVec<bsString>& result = _fileDialogExportText->getSelection();
            if(result.empty()) {
                _fileDialogExportText->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            } else {
                getConfig().setLastFileExportPath(result[0]);
                exp.state = CONFIRMATION_DIALOG;
            }
        }
    }

    if(exp.state==CONFIRMATION_DIALOG) {
        if(osFileExists(_fileDialogExportText->getSelection()[0])) {
            ImGui::OpenPopup("File already exists##TextFileAlreadyExists");
        } else {
            exp.state = EFFECTIVE_SAVE;
        }

        if(ImGui::BeginPopupModal("File already exists##TextFileAlreadyExists", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please confirm the file overwrite\n\n");
            ImGui::Separator();
            if(ImGui::Button("Yes", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter)) {
                exp.state = EFFECTIVE_SAVE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                _fileDialogExportText->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            }
            ImGui::EndPopup();
        }
    }

    if(exp.state==EFFECTIVE_SAVE) {

        // Task initialization
        if(exp.fileHandle==0) {

            // Open the export file
            const bsString& filename = _fileDialogExportText->getSelection()[0];
            exp.fileHandle = osFileOpen(filename, "w");
            if(!exp.fileHandle) {
                notifyErrorForDisplay(ERROR_GENERIC, bsString("Unable to open the file for writing: ") + filename);
                _fileDialogExportText->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
                return;
            }
            ImGui::OpenPopup("In progress##WaitExportText");
        }

        // Compute during a slice of time
        bsUs_t endComputationTimeUs = bsGetClockUs() + vwConst::COMPUTATION_TIME_SLICE_US; // Time slice of computation
        cmRecord::Evt evt;
        s64  lastDate = 0;
        bool isIteratorOk = false;
        int  nestingLevel; u32 lIdx; s64 scopeEndTimeNs;
        while(_record) {

            if(!(isIteratorOk=exp.it.getItem(nestingLevel, lIdx, evt, scopeEndTimeNs))) break;
            int flagsType = evt.flags&PL_FLAG_TYPE_MASK;
            const char* name = _record->getString(evt.nameIdx).value.toChar();

            if(flagsType==PL_FLAG_TYPE_DATA_TIMESTAMP ||
               (flagsType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && flagsType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST)) {
                lastDate = evt.vS64;
                if(exp.endTimeNs>=0 && evt.vS64>exp.endTimeNs) break;
                fprintf(exp.fileHandle, "%-28s %*s", getNiceTime(evt.vS64, 0), 2*nestingLevel, "");
            }
            else fprintf(exp.fileHandle, "%-27s %*s", "", 2*nestingLevel, "");

            if(evt.flags&PL_FLAG_SCOPE_BEGIN) {
                if(flagsType==PL_FLAG_TYPE_LOCK_WAIT) fprintf(exp.fileHandle, "%-32s [WAIT FOR LOCK]\n", name);
                else fprintf(exp.fileHandle, "> %s\n", name);
            }
            else if(evt.flags&PL_FLAG_SCOPE_END) {
                if(flagsType==PL_FLAG_TYPE_LOCK_WAIT) fprintf(exp.fileHandle, "%-32s [LOCK AVAILABLE]\n", name);
                else fprintf(exp.fileHandle, "< %s\n", name);
            }
            else if(flagsType==PL_FLAG_TYPE_MARKER)        fprintf(exp.fileHandle, "%-32s [MARKER '%s']\n", _record->getString(evt.filenameIdx).value.toChar(), name);
            else if(flagsType==PL_FLAG_TYPE_LOCK_ACQUIRED) fprintf(exp.fileHandle, "%-32s [LOCK ACQUIRED]\n", name);
            else if(flagsType==PL_FLAG_TYPE_LOCK_RELEASED) fprintf(exp.fileHandle, "%-32s [LOCK RELEASED]\n", name);
            else if(flagsType==PL_FLAG_TYPE_LOCK_NOTIFIED) fprintf(exp.fileHandle, "%-32s [LOCK NOTIFIED]\n", name);
            else fprintf(exp.fileHandle, "%-32s %s\n", name, getValueAsChar(evt));

            if(bsGetClockUs()>endComputationTimeUs) break;
            if(exp.dumpedQty>=0 && (--exp.dumpedQty)<0) { isIteratorOk = false; break; }
        }

        // Computations are finished?
        int computationLevel = 0;
        if(!_record || !isIteratorOk) computationLevel = 100;
        else if(exp.endTimeNs>=0) {  // Date based end criteria
            computationLevel = bsMinMax((int)(100.*((double)(lastDate-exp.startTimeNs)/(double)(exp.endTimeNs-exp.startTimeNs))), 1, 100);
        }
        else computationLevel = bsMax(10, 100-exp.dumpedQty); // Does not matter as line quantity based end criteria should finish in 1 cycle anyway...
        dirty();  // No idle

        bool openPopupModal = true;
        if(ImGui::BeginPopupModal("In progress##WaitExportText",
                                  &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
            char tmpStr[128];
            ImGui::TextColored(vwConst::gold, "Exporting in text...");
            snprintf(tmpStr, sizeof(tmpStr), "%d %%", computationLevel);
            ImGui::ProgressBar(0.01f*computationLevel, ImVec2(-1,ImGui::GetTextLineHeight()), tmpStr);
            if(computationLevel==100) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(!openPopupModal) computationLevel = 100;  // Cancelled by used

        // End of computation
        if(computationLevel>=100) {
            fclose(exp.fileHandle);
            exp.fileHandle = 0;
            exp.state = IDLE;
            _isExportOnGoing = false;
            _backgroundComputationInUse = false;
        }
    } // End of effective saving per chunk
}


void
vwMain::handleExportPlot(void)
{
    ExportPlot& exp = _exportPlot;

    // Display the file dialog to get the name of the capture
    if(exp.state==FILE_DIALOG) {
        if(_fileDialogExportPlot->draw(getConfig().getFontSize())) dirty();
        if(_fileDialogExportPlot->hasSelection()) {
            const bsVec<bsString>& result = _fileDialogExportPlot->getSelection();
            if(result.empty()) {
                _fileDialogExportPlot->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            } else {
                getConfig().setLastFileExportPath(result[0]);
                exp.state = CONFIRMATION_DIALOG;
            }
        }
    }

    if(exp.state==CONFIRMATION_DIALOG) {
        if(osFileExists(_fileDialogExportPlot->getSelection()[0])) {
            ImGui::OpenPopup("File already exists##PlotFileAlreadyExists");
        } else {
            exp.state = EFFECTIVE_SAVE;
        }

        if(ImGui::BeginPopupModal("File already exists##PlotFileAlreadyExists", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please confirm the file overwrite\n\n");
            ImGui::Separator();
            if(ImGui::Button("Yes", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter)) {
                exp.state = EFFECTIVE_SAVE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                _fileDialogExportPlot->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
            }
            ImGui::EndPopup();
        }
    }

    if(exp.state==EFFECTIVE_SAVE) {

        // Task initialization
        if(exp.fileHandle==0) {

            // Open the export file
            const bsString& filename = _fileDialogExportPlot->getSelection()[0];
            exp.fileHandle = osFileOpen(filename, "w");
            if(!exp.fileHandle) {
                notifyErrorForDisplay(ERROR_GENERIC, bsString("Unable to open the file for writing: ") + filename);
                _fileDialogExportPlot->clearSelection();
                exp.state = IDLE;
                _isExportOnGoing = false;
                _backgroundComputationInUse = false;
                return;
            }
            ImGui::OpenPopup("In progress##WaitExportPlot");

            // Initialize the relevant iterator and write the legend (not very "CSV" compatible)
            cmRecord::Elem& elem = _record->elems[exp.elemIdx];
            int eType = elem.flags&PL_FLAG_TYPE_MASK;
            if(eType==PL_FLAG_TYPE_MARKER) {
                fprintf(exp.fileHandle, "# Date (ns), marker text   /   Marker '%s' from thread '%s' from app '%s'\n",
                        _record->getString(elem.nameIdx).value.toChar(), _record->getString(_record->threads[elem.threadId].nameIdx).value.toChar(),
                        _record->appName.toChar());
                exp.itMarker.init (_record, exp.elemIdx, exp.startTimeNs, 0.);
            }
            else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
                fprintf(exp.fileHandle, "# Date (ns), notifier thread name   /   Lock '%s' notification from app '%s'\n",
                        _record->getString(elem.nameIdx).value.toChar(), _record->appName.toChar());
                exp.itLockNtf.init(_record, elem.nameIdx, exp.startTimeNs, 0.);
            }
            else if(eType==PL_FLAG_TYPE_LOCK_ACQUIRED) {
                fprintf(exp.fileHandle, "# Date (ns), acquiring thread name, usage duration (ns)   /   Lock '%s' usage from app '%s'\n",
                        _record->getString(elem.nameIdx).value.toChar(), _record->appName.toChar());
                exp.itLockUse.init(_record, elem.threadId, elem.nameIdx, exp.startTimeNs, 0.);
            }
            else {
                fprintf(exp.fileHandle, "# Date (ns), event value (%s)   /   Event '%s' from thread '%s' from app '%s'\n",
                        getUnitFromFlags(elem.flags), _record->getString(elem.nameIdx).value.toChar(),
                        _record->getString(_record->threads[elem.threadId].nameIdx).value.toChar(),
                        _record->appName.toChar());
                exp.itGeneric.init(_record, exp.elemIdx, exp.startTimeNs, 0.);
            }
        }

        // Compute during a slice of time
        bsUs_t endComputationTimeUs = bsGetClockUs() + vwConst::COMPUTATION_TIME_SLICE_US; // Time slice of computation
        cmRecord::Elem& elem = _record->elems[exp.elemIdx];
        int eType = elem.flags&PL_FLAG_TYPE_MASK;

        cmRecord::Evt evt;
        s64  lastDate = exp.startTimeNs;
        bool isIteratorOk = false, isCoarseScope = false;
        s64  ptTimeNs; double ptValue;

        if(_record) {

            if(eType==PL_FLAG_TYPE_MARKER) {
                while((isIteratorOk=exp.itMarker.getNextMarker(isCoarseScope, evt))) {
                    fprintf(exp.fileHandle, "%" PRId64 ",%s\n", evt.vS64,
                            _record->getString(evt.filenameIdx).value.toChar());
                    lastDate = evt.vS64;
                    if(lastDate>exp.endTimeNs || bsGetClockUs()>endComputationTimeUs) break;
                }
            }
            else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
                while((isIteratorOk=exp.itLockNtf.getNextLock(isCoarseScope, evt))) {
                    fprintf(exp.fileHandle, "%" PRId64 ",%s\n", evt.vS64,
                            _record->getString(_record->threads[evt.threadId].nameIdx).value.toChar());
                    lastDate = evt.vS64;
                    if(lastDate>exp.endTimeNs || bsGetClockUs()>endComputationTimeUs) break;
                }
            }
            else if(eType==PL_FLAG_TYPE_LOCK_ACQUIRED) {
                while((isIteratorOk=exp.itLockUse.getNextLock(ptTimeNs, ptValue, evt))) {
                    fprintf(exp.fileHandle, "%" PRId64 ",%s,%" PRId64 "\n", evt.vS64,
                            _record->getString(_record->threads[evt.threadId].nameIdx).value.toChar(), (s64)ptValue);
                    lastDate = evt.vS64;
                    if(lastDate>exp.endTimeNs || bsGetClockUs()>endComputationTimeUs) break;
                }
            }
            else {
                bool isHexa = _record->getString(elem.nameIdx).isHexa;
                while((isIteratorOk=(exp.itGeneric.getNextPoint(ptTimeNs, ptValue, evt)!=PL_INVALID))) {
                    fprintf(exp.fileHandle, "%" PRId64 ",%s\n", ptTimeNs, getValueAsChar(elem.flags, ptValue, 0., isHexa, 0, false));
                    lastDate = ptTimeNs;
                    if(lastDate>exp.endTimeNs || bsGetClockUs()>endComputationTimeUs) break;
                }
            }
        }

        // Computations are finished?
        int computationLevel = 0;
        if(!_record || !isIteratorOk) computationLevel = 100;
        else computationLevel = bsMinMax((int)(100.*((double)(lastDate-exp.startTimeNs)/(double)(exp.endTimeNs-exp.startTimeNs))), 1, 100);
        dirty();  // No idle

        bool openPopupModal = true;
        if(ImGui::BeginPopupModal("In progress##WaitExportPlot",
                                  &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
            char tmpStr[128];
            ImGui::TextColored(vwConst::gold, "Exporting in text...");
            snprintf(tmpStr, sizeof(tmpStr), "%d %%", computationLevel);
            ImGui::ProgressBar(0.01f*computationLevel, ImVec2(-1,ImGui::GetTextLineHeight()), tmpStr);
            if(computationLevel==100) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(!openPopupModal) computationLevel = 100;  // Cancelled by used

        // End of computation
        if(computationLevel>=100) {
            fclose(exp.fileHandle);
            exp.fileHandle = 0;
            exp.state = IDLE;
            _isExportOnGoing = false;
            _backgroundComputationInUse = false;
        }
    } // End of effective saving per chunk
}


void
vwMain::handleExports(void)
{
    // Key triggerred export: screen capture
    if(!_isExportOnGoing && _exportScreenshot.state==IDLE && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_P)) {
        _exportScreenshot.free(); // Ensure that previous capture is released
        if(_platform->captureScreen(&_exportScreenshot.width, &_exportScreenshot.height, &_exportScreenshot.buffer)) {
            bsString filenameProposal = osGetDirname(getConfig().getLastFileExportScreenshotPath())+bsString(PL_DIR_SEP)+(_record? _record->appName+".png" : bsString("Default.png"));
            _fileDialogExportScreenshot->open(filenameProposal);
            _exportScreenshot.state = FILE_DIALOG;
            _isExportOnGoing = true;
            plMarker("menu", "Open screenshot export file dialog");
        }
    }

    // Worth working?
    if(!_isExportOnGoing) return;

    handleExportCTF();
    handleExportText();
    handleExportPlot();
    handleExportScreenshot();
}
