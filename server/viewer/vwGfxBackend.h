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


// Interface for graphic backends

void vwBackendInit(void);
void vwBackendInstallFont(const void* fontData, int fontDataSize, int fontSize);
bool vwBackendDraw(void);  // Return true is something has been drawn
bool vwCaptureScreen(int* width, int* height, u8** buffer);
void vwBackendUninit(void);
