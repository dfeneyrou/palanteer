// Palanteer recording library
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

// This file is a simple interface to the compression/decompression functionality through a 3rd party library

// Internal
#include "cmCompress.h"

// External
#include "zstd.h"

// Compression of the record file (per chunk)
// We first tried miniz (zlib compatible), but zstd was better on all criteria (compression ratio,
// was slightly better, compression and decompression speeds were at least 50% faster).
// Only the code size metric was degraded.

// Note that if the flag PL_NO_COMPRESSION is set, these function are not called
//  It is set in particular in debug build (for two reasons: big slow down, and compression is not under test)

#ifndef PL_GROUP_COMPR
#define PL_GROUP_COMPR 0
#endif

static ZSTD_CCtx* cmCompressor   = 0;
static ZSTD_DCtx* cmDecompressor = 0;

// Level 1 is the fastest, and the compression gain compared to 2-9 is negligible on such small chunks (~6KB).
// For instance, level 9 provides a ~10% gain for 3 times slower speed.
// Also dictionaries do not help, probably because there is no strong pattern (numerical values, with many increasing dates)
constexpr int cmCompressionLevel = 1;

void
cmInitChunkCompress(void)
{
    plgScope(COMPR, "cmInitChunkCompress (ZSTD)");
    plAssert(!cmDecompressor && !cmCompressor);
    cmCompressor   = ZSTD_createCCtx();
    cmDecompressor = ZSTD_createDCtx();
}


void
cmUninitChunkCompress(void)
{
    plgScope(COMPR, "cmUninitChunkCompress");
    ZSTD_freeCCtx(cmCompressor);   cmCompressor   = 0;
    ZSTD_freeDCtx(cmDecompressor); cmDecompressor = 0;
}


bool
cmCompressChunk(const u8* inBuffer, int inBufferSize, u8* outBuffer, int* outBufferSize)
{
    plgScope(COMPR, "compressChunk");
    plAssert(cmCompressor);

    size_t outSize = ZSTD_compressCCtx(cmCompressor, outBuffer, *outBufferSize,
                                       inBuffer, inBufferSize, cmCompressionLevel);
    plAssert(!ZSTD_isError(outSize), inBufferSize, outSize, ZSTD_getErrorName(outSize));
    *outBufferSize = (int)outSize;
    return true;
}


bool
cmDecompressChunk(const u8* inBuffer, int inBufferSize, u8* outBuffer, int* outBufferSize)
{
    plgScope(COMPR, "decompressChunk");
    plAssert(cmDecompressor);

    size_t outSize = ZSTD_decompressDCtx(cmDecompressor, outBuffer, *outBufferSize,
                                         inBuffer, inBufferSize);
    plAssert(!ZSTD_isError(outSize), inBufferSize, outSize, ZSTD_getErrorName(outSize));
    *outBufferSize = (int)outSize;
    return true;
}
