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


// This file implements the base windows and display component of the application.
// record, settings, catalog, console, help, about, menu bar...

// External
#include "imgui.h"
#include "palanteer.h"

// Internal
#include "cmCnx.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwPlatform.h"
#include "vwFileDialog.h"
#include "vwConfig.h"


void
vwMain::drawMainMenuBar(void)
{
    if(_uniqueIdFullScreen>=0) return;
    bool doOpenSaveTemplate = false;

    if(ImGui::BeginMenuBar()) {
        if(ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Import Palanteer file", NULL, false, _underRecordAppIdx<0)) {
                _fileDialogImport->open(getConfig().getLastFileImportPath(), getConfig().isMultiStream()? cmConst::MAX_STREAM_QTY : 1);
                plLogInfo("menu", "Open import file dialog");
            }
            else if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
                if(!getConfig().isMultiStream()) {
                    ImGui::SetTooltip("Import a Palanteer .pltraw file");
                } else {
                    ImGui::SetTooltip("Import up to %d Palanteer .pltraw files\nunder the application name: %s",
                                      cmConst::MAX_STREAM_QTY, getConfig().getMultiStreamAppName().toChar());
                }
            }

            if(ImGui::MenuItem("Clear", NULL, false, _underDisplayAppIdx>=0)) {
                _doClearRecord = true;
                getConfig().setLastLoadedRecordPath("");
            }

            ImGui::Separator();
            if(ImGui::MenuItem("Export as Chrome Trace Format", NULL, false,
                               !_backgroundComputationInUse && !_isExportOnGoing && _underDisplayAppIdx>=0)) {
                initiateExportCTF();
                plLogInfo("menu", "Open Chrome Trace Format export file dialog");
            }

            ImGui::Separator();
            if(ImGui::MenuItem("Quit")) {
                _platform->quit();
                plLogInfo("menu", "Quit");
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Navigation")) {
            bool state;
            state = getConfig().getWindowCatalogVisibility();
            if(ImGui::MenuItem("Catalog", NULL, &state)) {
                getConfig().setWindowCatalogVisibility(state);
                _catalogWindow.isWindowSelected = true;
                plLogInfo("menu", "Change catalog view visibility");
            }
            state = getConfig().getWindowRecordVisibility();
            if(ImGui::MenuItem("Infos on record", NULL, &state)) {
                getConfig().setWindowRecordVisibility(state);
                _recordWindow.isWindowSelected = true;
                plLogInfo("menu", "Change record view visibility");
            }
            state = getConfig().getWindowSettingsVisibility();
            if(ImGui::MenuItem("Settings", NULL, &state)) {
                getConfig().setWindowSettingsVisibility(state);
                _settingsWindow.isWindowSelected = true;
                plLogInfo("menu", "Change settings view visibility");
            }
            state = getConfig().getWindowSearchVisibility();
            if(ImGui::MenuItem("Search", NULL, &state)) {
                getConfig().setWindowSearchVisibility(state);
                _search.isWindowSelected = true;
                plLogInfo("menu", "Change search view visibility");
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Views", !!_record)) {

            if(ImGui::Selectable("New timeline"       , false, (_timelines.size()>=3)?    ImGuiSelectableFlags_Disabled:0))
                addTimeline(getId());
            if(ImGui::Selectable("New memory timeline", false, (_memTimelines.size()>=3)? ImGuiSelectableFlags_Disabled:0))
                addMemoryTimeline(getId());
            if(ImGui::Selectable("New log view",        false, (_logViews.size()>=3)? ImGuiSelectableFlags_Disabled:0))
                addLog(getId());
            ImGui::Separator();

            // Template workspaces
            if(ImGui::MenuItem("Save workspace as template layout")) {
                doOpenSaveTemplate = true;
            }
            if(ImGui::BeginMenu("Apply workspace template", !getConfig().getTemplateLayouts().empty())) {
                static char localRenameBuffer[64];
                // Loop on available templates
                for(vwConfig::ScreenLayout& tl : getConfig().getTemplateLayouts()) {
                    if(ImGui::MenuItem(tl.name.toChar())) _screenLayoutToApply = tl;
                    ImGui::PushID(tl.name.toChar());
                    if(ImGui::IsItemHovered() && ImGui::IsMouseReleased(2)) {
                        ImGui::OpenPopup("Workspace template");
                        snprintf(localRenameBuffer, sizeof(localRenameBuffer), "%s", tl.name.toChar());
                    }
                    if(ImGui::BeginPopup("Workspace template", ImGuiWindowFlags_AlwaysAutoResize)) {
                        // Renaming
                        ImGui::Text("Rename  "); ImGui::SameLine();
                        ImGui::SetNextItemWidth(150);
                        bool doCloseAndSave = ImGui::InputText("##templateName", &localRenameBuffer[0], sizeof(localRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        if(doCloseAndSave || ImGui::SmallButton("OK")) {
                            bsString name(localRenameBuffer); name.strip();
                            if(!name.empty()) {
                                // Already exists?
                                bool alreadyExists = false;
                                for(const vwConfig::ScreenLayout& tl2 : getConfig().getTemplateLayouts()) {
                                    if(tl2.name!=name) continue;
                                    alreadyExists = true; break;
                                }
                                if(!alreadyExists) {
                                    tl.name = name;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                        // Update content
                        if(ImGui::MenuItem("Replace with current")) {
                            _doSaveTemplateLayoutName = tl.name;
                        }
                        // Delete
                        if(ImGui::MenuItem("Delete template")) {
                            getConfig().getTemplateLayouts().erase(&tl);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        } // End of "Views" menu


        constexpr int maxAppNameSize = 64;
        static bool isMultiStreamMenuOpen = false;
        static char localBuffer[maxAppNameSize];
        if(getConfig().isMultiStream()) {
            const bsString& multiStreamAppName = getConfig().getMultiStreamAppName();
            if(ImGui::BeginMenu("Multistream mode")) {
                if(!isMultiStreamMenuOpen) {
                    isMultiStreamMenuOpen = true;
                    memcpy(localBuffer, multiStreamAppName.toChar(), bsMin(multiStreamAppName.size()+1, maxAppNameSize));
                    localBuffer[maxAppNameSize-1] = '\0';
                }
                // Input of the multi stream application name
                bool isChanged = (strncmp(multiStreamAppName.toChar(), localBuffer, maxAppNameSize)!=0);
                if(isChanged) ImGui::PushStyleColor(ImGuiCol_FrameBg, vwConst::darkBlue);
                ImGui::Text("Aggregated app name:"); ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                bool doCloseAndSave = ImGui::InputText("##AppName", &localBuffer[0], maxAppNameSize, ImGuiInputTextFlags_EnterReturnsTrue);
                if(isChanged) ImGui::PopStyleColor();
                ImGui::SameLine();
                if(doCloseAndSave || ImGui::SmallButton("OK")) {
                    getConfig().setStreamConfig(true, bsString(localBuffer));
                    plLogInfo("menu", "Changed record nickname");
                    isMultiStreamMenuOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Separator();
                if(ImGui::MenuItem("Switch to monostream")) {
                    plLogInfo("menu", "Switch to monostream");
                    getConfig().setStreamConfig(false, multiStreamAppName);
                }
                ImGui::EndMenu();
            }
            else isMultiStreamMenuOpen = false;
            if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
                ImGui::SetTooltip("Future recordings accept inputs from up to %d streams/process simultaneously.\n"
                                  "The name of the aggregated record is '%s' (configured in this menu).\nThe name of individual streams is ignored.",
                                  cmConst::MAX_STREAM_QTY, multiStreamAppName.toChar());
            }
        }
        else {
            isMultiStreamMenuOpen = false;
            if(ImGui::BeginMenu("Monostream mode")) {
                if(ImGui::MenuItem("Switch to multistream")) {
                    plLogInfo("menu", "Switch to multistream");
                    getConfig().setStreamConfig(true, getConfig().getMultiStreamAppName());
                }
                ImGui::EndMenu();
            }
            if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
                ImGui::SetTooltip("Future recordings accept inputs from only one stream/process.\nThe name of the record is the one provided dynamically by the application.");
            }
        }

        if(ImGui::BeginMenu("Help")) {
            if(ImGui::MenuItem("Get started" )) {
                plLogInfo("menu", "Show help");
                _showHelp  = true;
            }
            bool state = getConfig().getWindowConsoleVisibility();
            if(ImGui::MenuItem("Console", NULL, &state)) {
                getConfig().setWindowConsoleVisibility(state);
                plLogInfo("menu", "Change log console view visibility");
            }
            ImGui::Separator();
            if(ImGui::MenuItem("About")) {
                plLogInfo("menu", "Show about");
                _showAbout = true;
            }
            ImGui::EndMenu();
        }

#if 0
        // Draw the FPS at the top right
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SameLine(ImGui::GetWindowWidth()-ImGui::CalcTextSize("FPS: 999 / 999 ").x-style.WindowPadding.x-style.ItemSpacing.x);
        ImGui::Text("FPS: %-3d / %-3d", (int)(1000000/bsMax(1001LL, _platform->getLastUpdateDuration())),
                    (int)(1000000/bsMax(1001LL, _platform->getLastRenderingDuration())));
#endif
        ImGui::EndMenuBar();
    }  // End of the menu bar


    // Workspace dialog popup
    static char localBuffer[64];
    if(doOpenSaveTemplate) {
        ImGui::OpenPopup("Save workspace template as ...");
        localBuffer[0] = 0;
    }
    if(ImGui::BeginPopup("Save workspace template as ...", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Please provide a name for this template workspace");
        ImGui::SetNextItemWidth(150);
        bool doCloseAndSave = ImGui::InputText("##templateName", &localBuffer[0], sizeof(localBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if(doCloseAndSave || ImGui::SmallButton("OK")) {
            bsString name(localBuffer); name.strip();
            if(!name.empty()) {
                // Already exists?
                bool alreadyExists = false;
                for(const vwConfig::ScreenLayout& tl : getConfig().getTemplateLayouts()) {
                    if(tl.name!=name) continue;
                    alreadyExists = true; break;
                }
                if(!alreadyExists) {
                    _doSaveTemplateLayoutName = name;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // Handle the import file dialog
    if(_fileDialogImport->draw()) dirty();
    if(_fileDialogImport->hasSelection()) {
        const bsVec<bsString>& result = _fileDialogImport->getSelection();
        if(!result.empty()) {
            getConfig().setLastFileImportPath(result[0]);
            _clientCnx->injectFiles(result);
        }
        _fileDialogImport->clearSelection();
    }
}


void
vwMain::drawSettings(void)
{
    constexpr float sliderWidth = 150.f;
    static int draggedFontSize  = -1;
    float titleWidth = ImGui::CalcTextSize("Horizontal wheel inversion").x+0.3f*sliderWidth;

    SettingsWindow& sw = _settingsWindow;
    if(!getConfig().getWindowSettingsVisibility() || (_uniqueIdFullScreen>=0 && sw.uniqueId!=_uniqueIdFullScreen)) return;

    static bool isDocked = false;
    if(!isDocked) {
        isDocked = true;
        selectBestDockLocation(true, false);
    }

    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "Settings###%d", sw.uniqueId);
    bool isOpenWindow = true;
    if(!ImGui::Begin(tmpStr, &isOpenWindow, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if(!isOpenWindow) {
        setFullScreenView(-1);
        draggedFontSize = -1;
        getConfig().setWindowSettingsVisibility(false);
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if(ImGui::CollapsingHeader("Global") && ImGui::BeginTable("##tableNav", 2)) {
        ImGui::TableSetupColumn("",  ImGuiTableColumnFlags_WidthFixed, titleWidth);

        // Wheel inversions
        ImGui::TableNextColumn(); ImGui::Text("Horizontal wheel inversion");
        ImGui::TableNextColumn();
        bool wheelInversion = (getConfig().getHWheelInversion()<0);
        if(ImGui::Checkbox("##Hwheel inversion", &wheelInversion)) {
            getConfig().setHWheelInversion(wheelInversion);
            plLogInfo("menu", "Change horizontal wheel inversion");
        }
        wheelInversion = (getConfig().getVWheelInversion()<0);
        ImGui::TableNextColumn(); ImGui::Text("Vertical wheel inversion");
        ImGui::TableNextColumn();
        if(ImGui::Checkbox("##Vwheel inversion", &wheelInversion)) {
            getConfig().setVWheelInversion(wheelInversion);
            plLogInfo("menu", "Change vertical wheel inversion");
        }

        // Timeline vertical spacing
        ImGui::TableNextColumn(); ImGui::Text("Thread vertical spacing");
        ImGui::TableNextColumn();
        float timelineVSpacing = getConfig().getTimelineVSpacing();
        ImGui::SetNextItemWidth(sliderWidth);
        if(ImGui::SliderFloat("##Thread vspacing", &timelineVSpacing, 0., 3., "%.1f", ImGuiSliderFlags_ClampOnInput)) {
            getConfig().setTimelineVSpacing(timelineVSpacing);
        }

        // Font size
        ImGui::TableNextColumn(); ImGui::Text("Font size");
        ImGui::TableNextColumn();
        if(draggedFontSize<0) draggedFontSize = getConfig().getFontSize();
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderInt("##Font size", &draggedFontSize, vwConst::FONT_SIZE_MIN, vwConst::FONT_SIZE_MAX, "%d", ImGuiSliderFlags_ClampOnInput);
        if(draggedFontSize>=0 && !ImGui::IsMouseDown(0)) {
            if(draggedFontSize!=getConfig().getFontSize()) {
                getConfig().setFontSize(draggedFontSize);
                _platform->setNewFontSize(draggedFontSize);
                plLogInfo("menu", "Changed font size");
            }
            draggedFontSize = -1;
        }

        // Date format
        ImGui::TableNextColumn(); ImGui::Text("Date format");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The format of the date used in Text, Log and Search views.");
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(sliderWidth);
        int timeFormat = getConfig().getTimeFormat();
        if(ImGui::Combo("##DateFormat", &timeFormat, "ss.ns\0hh:mm:ss.ns\0\0")) {
            getConfig().setTimeFormat(timeFormat);
        }
        ImGui::PopItemWidth();

        // Cache size
        ImGui::TableNextColumn(); ImGui::Text("RAM cache size (MB)");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Applicable at next record loading");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(sliderWidth);
        float cacheMBytes = (float)getConfig().getCacheMBytes();
        if(ImGui::SliderFloat("##Cache size", &cacheMBytes, vwConst::CACHE_MB_MIN, vwConst::CACHE_MB_MAX, "%.0f",
                              ImGuiSliderFlags_ClampOnInput | ImGuiSliderFlags_Logarithmic)) {
            getConfig().setCacheMBytes((int)cacheMBytes);
            plLogInfo("menu", "Changed cache size");
        }

        // Record storage location
        ImGui::TableNextColumn(); ImGui::Text("Record storage location");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("A restart is needed for changes to be taken into account.\nNo automatic record transfer is performed.");
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(vwConst::gold, "%s%s", getConfig().getRecordStoragePath().toChar(), (getConfig().getRecordStoragePath()!=_storagePath)? "   (need restart)":"");
        ImGui::SameLine(0., 20.);
        if(ImGui::Button("Change")) {
            _fileDialogSelectRecord->open(getConfig().getRecordStoragePath());
            plLogInfo("menu", "Open record storage path selection file dialog");
        }

        ImGui::EndTable();

        // Some vertical spacing
        ImGui::Dummy(ImVec2(1, 0.5f*ImGui::GetTextLineHeight()));
    }
    else {
        draggedFontSize = -1;
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if(_record) {
        snprintf(tmpStr, sizeof(tmpStr), "Application - %s", _record->appName.toChar());
        if(ImGui::CollapsingHeader(tmpStr) && ImGui::BeginTable("##tableNav", 2)) {
            ImGui::TableSetupColumn("",  ImGuiTableColumnFlags_WidthFixed, titleWidth);

            // Thread colors
            ImGui::TableNextColumn(); ImGui::Text("Thread colors");
            ImGui::TableNextColumn();
            if(ImGui::Button("Randomize##rand threads")) {
                getConfig().randomizeThreadColors();
                plLogInfo("menu", "Randomize thread colors");
            }

            // Curve colors
            ImGui::TableNextColumn(); ImGui::Text("Curve colors");
            ImGui::TableNextColumn();
            if(ImGui::Button("Randomize##rand curves")) {
                getConfig().randomizeCurveColors();
                plLogInfo("menu", "Randomize curve colors");
            }

            // Lock latency limit
            ImGui::TableNextColumn(); ImGui::Text("Lock latency (µs)");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Defines what is a lock taken without waiting.\nThis impacts the highlight of waiting threads.");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(sliderWidth);
            float lockLatencyUs = (float)getConfig().getLockLatencyUs();
            if(ImGui::SliderFloat("##LockLatency", &lockLatencyUs, 0.f, vwConst::LOCK_LATENCY_LIMIT_MAX_US, "%.0f",
                                  ImGuiSliderFlags_ClampOnInput | ImGuiSliderFlags_Logarithmic)) {
                getConfig().setLockLatencyUs((int)lockLatencyUs);
                plLogInfo("menu", "Changed lock latency limit");
                for(auto& t : _timelines) t.isCacheDirty = true;
            }

            ImGui::EndTable();
        }
    }

    // Check full screen
    if(ImGui::IsWindowHovered() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(sw.uniqueId);
    }

    // Handle the record storage path selection file dialog
    if(_fileDialogSelectRecord->draw()) dirty();
    if(_fileDialogSelectRecord->hasSelection()) {
        getConfig().setRecordStoragePath(_fileDialogSelectRecord->getSelection()[0]);
        _fileDialogSelectRecord->clearSelection();
    }

    ImGui::End();
}


void
vwMain::drawHelp(void)
{
    static const char* helpStr = \
        "##Palanteer\n"
        "===\n"
        "#Palanteer#is composed of 3 parts:\n"
        "-the#instrumentation#library\n"
        "-the#viewer#\n"
        "-the#scripting#module\n"
        "\n"
        "This tool is the viewer and has two main roles:\n"
        "-#record#and store the events from the execution of an instrumented program\n"
        "-#display#records to enable debugging, profiling, optimizing speed and memory, check behavior correctness, etc...\n"
        "\n"
        "##Recording\n"
        "The 2 ways to create a record from an instrumented program are:\n"
        "-live by#remote connection#with the program launched in 'connected mode'\n"
        "-offline by#importing a .pltraw file#generated with a program launched in 'file storage' mode\n"
        "\n"
        "The viewer always listens so that launching your instrumented program in 'connected' mode is enough to connect both.\n"
        "If a direct connection is not possible nor desirable, the offline recording in file is the way to go. The event processing will occur at import time.\n"
        "Records are listed in the#'Catalog'#window, per program and in chronological order. A nickname can be provided to easily recall a particular one.\n"
        " \n"
        "##Views\n"
        "Once loaded, a record can be visualized through any of these views:\n"
        "-#Timeline#| Global and comprehensive display of the chronological execution of the program\n"
        "-#Memory#| Per thread chronological representation of the memory allocations and usage\n"
        "-#Text#| Per thread text hierarchy of the recorded events\n"
        "-#Plot#| Curve plot of any kind of event (instantaneous)\n"
        "-#Histogram#| Histogram of any event kind (need computations)\n"
        "-#Profile#| Per thread flame graph or array of timings, memory allocations or memory usage (need computations)\n"
        " \n"
        "##Workspaces\n"
        "The views arrangement, aka 'workspace', is adjustable simply by dragging window title bars or borders.\n"
        "The current workspace can be saved as a named 'template layout' in the 'View' menu and recalled later at any time.\n"
        "\n"
        "##Navigation\n"
        "If you had only one key to remember, it would be:\n"
        "-#H#| Dedicated help for the window under focus\n"
        "\n"
        "Unless not applicable or specified otherwise in the dedicated help window, the usual actions for navigation are:\n"
        "-#F key#| Toggle full view screen\n"
        "-#Ctrl-F key#| Text search view\n"
        "-#Ctrl-P key#| Capture screen and save into a PNG image\n"
        "-#Right mouse button dragging#| Move the visible part of the view\n"
        "-#Left/Right key#| Move horizontally\n"
        "-#Ctrl-Left/Right key#| Move horizontally faster\n"
        "-#Up/Down key#| Move vertically\n"
        "-#Mouse wheel#| Move vertically\n"
        "-#Middle mouse button dragging#| Measure/select a time range\n"
        "-#Ctrl-Up/Down key#| Time zoom\n"
        "-#Ctrl-Mouse wheel#| Time zoom\n"
        "-#Left mouse#| Time synchronize views of the same group\n"
        "-#Double left mouse click#| Time and range synchronize views of the same group\n"
        "-#Right mouse click#| Open a contextual menu\n"
        "-#Hover an item#| Display a tooltip with detailed information\n"
        "\n"
        "##Views synchronization\n"
        "Views can be 'associated' so that they  share the same time range and react to each other. This is called 'view synchronization'.\n"
        "This association is chosen in the top right combobox of the views\n"
        "\n"
        "By default, all views are associated with the#Group 1#. The#'Group 2'#provides a second shared focus.\n"
        "A view can also be#'Isolated'#and become independent of others.\n"
        "\n"
        ;
    if(!_showHelp) return;

    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w);
    if(ImGui::Begin("Help", &_showHelp, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        displayHelpText(helpStr);
    }
    ImGui::End();
}


void
vwMain::drawAbout(void)
{
    static constexpr char const* textDescr = "Look into it and have an omniscient picture of your program...";

    if(!_showAbout) return;
    float fontSize     = ImGui::GetFontSize();
    float bigTextWidth = ImGui::CalcTextSize(textDescr).x+4.f*fontSize;
    ImGui::SetNextWindowSize(ImVec2(bigTextWidth, fontSize*16.f));
    if(!ImGui::Begin("Palanteer - About", &_showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    float winX = ImGui::GetWindowPos().x;
    float winY = ImGui::GetWindowPos().y;
    float winWidth  = ImGui::GetWindowContentRegionMax().x;

    // Bold colored title
    ImU32 titleBg = IM_COL32(255, 200, 200, 255);
    ImU32 titleFg = IM_COL32(50, 150, 255, 255);
    float textWidth = ImGui::CalcTextSize("Palanteer").x;
    float x = winX+0.5f*(winWidth-2.f*textWidth), y = winY+2.f*fontSize;
    DRAWLIST->AddText(ImGui::GetFont(), 2.f*fontSize, ImVec2(x-0.1f*fontSize, y-0.1f*fontSize), titleBg, "Palanteer");
    DRAWLIST->AddText(ImGui::GetFont(), 2.f*fontSize, ImVec2(x, y), titleFg, "Palanteer");
    y += 2.f*fontSize;

#define TEXT_POSITION(text, lineSpan, coefScreenWidth, coefTextWidth)   \
    DRAWLIST->AddText(ImVec2(winX+(coefScreenWidth)*winWidth+(coefTextWidth)*ImGui::CalcTextSize(text).x, y), vwConst::uWhite, text); \
    y += (lineSpan)*fontSize

    // Version
    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "v%s", PALANTEER_VERSION);
    TEXT_POSITION(tmpStr, 2, 0.5f, -0.5f);

    // Description
    TEXT_POSITION(textDescr, 3, 0.5f, -0.5f);
    TEXT_POSITION("Palanteer is efficient, light, free and open source", 2, 0.5f, -0.5f);
    TEXT_POSITION("Copyright (c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>", 3, 0.5f, -0.5f);

    // Buttons
    ImGui::SetCursorPosY(fontSize*13.5f);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SetCursorPosX(0.2f*winWidth);
    bool doOpenLicense = false;
    if(ImGui::Button("License")) {
        doOpenLicense = true;
    }
    ImGui::SameLine(0.7f*winWidth);
    if(ImGui::Button("Close")) _showAbout = false;

    // License popup
    static constexpr char const* noteTextDescr = "NOTE: the instrumentation libraries are under the MIT license.\nYou do not have to open the source code of your program\n\n";
    bool openPopupModal = true;
    if(doOpenLicense) {
        ImGui::OpenPopup("Viewer license");
        ImGui::SetNextWindowSize(ImVec2(ImGui::CalcTextSize(noteTextDescr).x*1.2f+2.f*fontSize, fontSize*25.f));
    }
    if(ImGui::BeginPopupModal("Viewer license", &openPopupModal, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize)) {

        static char licenseText[1024] = \
            "This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version..\n\n"
            "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.\n\n"
            "You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.\n\n";

        ImGui::TextColored(vwConst::gold, noteTextDescr);
        ImGui::Text("The license below applies only to the viewer (this program):\n");

        ImGui::Spacing(); ImGui::Spacing();
        ImGui::BeginChild("license text", ImVec2(0,fontSize*14.f), true);
        ImGui::PushStyleColor(ImGuiCol_Text, vwConst::grey);
        ImGui::TextWrapped("%s", licenseText);
        ImGui::PopStyleColor();
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(0.7f*ImGui::GetWindowContentRegionMax().x, fontSize*22.5f));
        if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::End();
}


void
vwMain::drawErrorMsg(void)
{
#define DRAW_ERROR_DISPLAY_TEXT()                           \
    ImGui::PushStyleColor(ImGuiCol_Text, vwConst::red);     \
    ImGui::BulletText("%s", _safeErrorMsg.msg.toChar());    \
    ImGui::PopStyleColor()
#define DRAW_ERROR_DISPLAY_END()                                        \
    ImGui::SetCursorPosX(0.45f*ImGui::GetWindowContentRegionMax().x);   \
    if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();              \
    ImGui::EndPopup()

    bool isOneWindowOpen = false;
    bool isOpen = true;
    if(ImGui::BeginPopupModal("Load error", &isOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        isOneWindowOpen = true;
        ImGui::Text("An error occured while loading the record:");
        DRAW_ERROR_DISPLAY_TEXT();
        ImGui::Spacing(); ImGui::Spacing();
        ImGui::Text("This is usually due to");
        ImGui::BulletText("either a corrupted file");
        ImGui::BulletText("either an incompatible record version");
        ImGui::Spacing(); ImGui::Spacing();
        DRAW_ERROR_DISPLAY_END();
    }

    isOpen = true;
    if(ImGui::BeginPopupModal("Import error", &isOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        isOneWindowOpen = true;
        ImGui::Text("An error occured while importing a record:");
        DRAW_ERROR_DISPLAY_TEXT();
        DRAW_ERROR_DISPLAY_END();
    }

    isOpen = true;
    if(ImGui::BeginPopupModal("Error", &isOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        isOneWindowOpen = true;
        ImGui::Text("An error occured:");
        DRAW_ERROR_DISPLAY_TEXT();
        DRAW_ERROR_DISPLAY_END();
    }

    if(!isOneWindowOpen && !_safeErrorMsg.msg.empty()) {
        _safeErrorMsg.msg.clear(); // Unblocks the processing of other inter-thread messages
        plAssert(_actionMode==ERROR_DISPLAY, _actionMode);
        _actionMode = READY;
        plData("Action mode", plMakeString("Ready"));
    }
}


void
vwMain::logToConsole(cmLogKind kind, const bsString& msg)
{
    std::lock_guard<std::mutex> lk(_logConsole.logMx);
    time_t rawtime  = time(0);
    tm*    timeinfo = localtime(&rawtime);
    _logConsole.logs.push_back({kind, (s8)timeinfo->tm_hour, (s8)timeinfo->tm_min, (s8)timeinfo->tm_sec, msg});
}


void
vwMain::logToConsole(cmLogKind kind, const char* format, ...)
{
    // Format the string
    char tmpStr[256];
    va_list args;
    va_start(args, format);
    vsnprintf(tmpStr, sizeof(tmpStr), format, args);
    va_end(args);
    // Store
    std::lock_guard<std::mutex> lk(_logConsole.logMx);
    time_t rawtime  = time(0);
    tm*    timeinfo = localtime(&rawtime);
    _logConsole.logs.push_back({kind, (s8)timeinfo->tm_hour, (s8)timeinfo->tm_min, (s8)timeinfo->tm_sec, tmpStr});
}


void
vwMain::drawLogConsole(void)
{
    static ImVec4 colorArray[4] = { ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
        ImVec4(1.0f, 0.7f, 0.4f, 1.0f), ImVec4(1.0f, 0.3f, 0.3f, 1.0f) };
    LogConsole& lc = _logConsole;
    if(!getConfig().getWindowConsoleVisibility() || (_uniqueIdFullScreen>=0 && lc.uniqueId!=_uniqueIdFullScreen)) return;

    static bool isDocked = false;
    if(!isDocked) {
        isDocked = true;
        selectBestDockLocation(true, false);
    }

    char tmpStr[128];
    snprintf(tmpStr, sizeof(tmpStr), "Console###%d", lc.uniqueId);
    bool isOpenWindow = true;
    if(!ImGui::Begin(tmpStr, &isOpenWindow, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if(!isOpenWindow) {
        setFullScreenView(-1);
        getConfig().setWindowConsoleVisibility(false);
    }

    ImGui::BeginChild("LogRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    // Display output lines
    std::lock_guard<std::mutex> lk(lc.logMx);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
    float fontHeight = ImGui::GetTextLineHeightWithSpacing();
    int startIdx =  (int)(ImGui::GetScrollY()/fontHeight);
    int endIdx   = bsMin(lc.logs.size(), startIdx+1+ImGui::GetWindowSize().y/fontHeight);

    // Loop on logs
    for(int i=startIdx; i<endIdx; ++i) {
        const LogItem& log = lc.logs[i];
        ImGui::TextColored(colorArray[log.kind], "%02dh%02dm%02ds > %s",
                           log.hour, log.minute, log.second,log.text.toChar());
    }

    // Check full screen
    if(ImGui::IsWindowHovered() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_F)) {
        setFullScreenView(lc.uniqueId);
    }

    ImGui::SetCursorPosY(lc.logs.size()*fontHeight); // Set the cursor on the last line (even if not displayed)
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
}
