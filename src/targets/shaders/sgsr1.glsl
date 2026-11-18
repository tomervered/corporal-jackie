// Snapdragon Game Super Resolution 1 (SGSR) as an mpv user shader: one pass,
// 13 texture reads near edges and 5 elsewhere, so it costs less than FSR
// (two passes, 17 reads everywhere) and suits weak graphics chips. Runs on the luma plane
// only; mpv scales chroma to match. Only active when the target is larger
// than the video's own size.
//
// The app fills in the @...@ size expressions before handing the file to
// mpv, as for fsr1.glsl.
//
// Ported for Corporal Jackie from Qualcomm's sgsr1_shader_mobile.frag, working
// on luma instead of green, with its default edge threshold and sharpness. The texture gathers
// are written as plain reads, so it needs nothing beyond what mpv's own
// shaders use. Qualcomm's notice, which this port keeps:
//
// Copyright (c) 2023, 2025, Qualcomm Innovation Center, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// SPDX-License-Identifier: BSD-3-Clause

//!HOOK LUMA
//!BIND HOOKED
//!DESC Snapdragon GSR 1 (upscale)
//!WHEN @WHEN@
//!WIDTH @WIDTH@
//!HEIGHT @HEIGHT@
//!COMPONENTS 1

#define SGSR_EDGE_THRESHOLD (8.0 / 255.0)
#define SGSR_EDGE_SHARPNESS 2.0

// The input texel at 'f' + 'off', in whole texels.
float sgsr_texel(vec2 f, vec2 off)
{
    return HOOKED_tex((f + off + vec2(0.5)) * HOOKED_pt).x;
}

float sgsr_lanczos2(float x)
{
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

// Adds one texel's weight, and its weighted value, at distance (dx, dy).
void sgsr_weight(inout vec2 acc, float dx, float dy, float c, float stdev)
{
    float x = (dx * dx + dy * dy) * 0.55 + clamp(abs(c) * stdev, 0.0, 1.0);
    float w = sgsr_lanczos2(x);
    acc += vec2(w, w * c);
}

vec4 hook()
{
    float color = HOOKED_tex(HOOKED_pos).x;

    // Texel-centre position, the texel above-left of it, and the fraction.
    vec2 pos = HOOKED_pos * HOOKED_size - vec2(0.5);
    vec2 f = floor(pos);
    vec2 pl = pos - f;

    // The 2x2 texels around the position (a b / c d).
    float a = sgsr_texel(f, vec2(0.0, 0.0));
    float b = sgsr_texel(f, vec2(1.0, 0.0));
    float c = sgsr_texel(f, vec2(0.0, 1.0));
    float d = sgsr_texel(f, vec2(1.0, 1.0));

    // Flat areas keep the plain (bilinear) value.
    float edgeVote = abs(a - c) + abs(color - c) + abs(color - a);
    if (edgeVote <= SGSR_EDGE_THRESHOLD)
        return vec4(color, 0.0, 0.0, 1.0);

    // The ring of 8 texels around the 2x2.
    float l0 = sgsr_texel(f, vec2(-1.0, 0.0));
    float l1 = sgsr_texel(f, vec2(-1.0, 1.0));
    float r0 = sgsr_texel(f, vec2( 2.0, 0.0));
    float r1 = sgsr_texel(f, vec2( 2.0, 1.0));
    float u0 = sgsr_texel(f, vec2( 0.0, -1.0));
    float u1 = sgsr_texel(f, vec2( 1.0, -1.0));
    float d0 = sgsr_texel(f, vec2( 0.0, 2.0));
    float d1 = sgsr_texel(f, vec2( 1.0, 2.0));

    float mean = (a + b + c + d) * 0.25;
    a -= mean; b -= mean; c -= mean; d -= mean;
    l0 -= mean; l1 -= mean; r0 -= mean; r1 -= mean;
    u0 -= mean; u1 -= mean; d0 -= mean; d1 -= mean;
    float centred = color - mean;

    float sum = abs(a) + abs(b) + abs(c) + abs(d)
              + abs(l0) + abs(l1) + abs(r0) + abs(r1)
              + abs(u0) + abs(u1) + abs(d0) + abs(d1);
    float stdev = sum > 0.0 ? 2.181818 / sum : 0.0;

    vec2 acc = vec2(0.0);
    sgsr_weight(acc, pl.x,       pl.y + 1.0, u0, stdev);
    sgsr_weight(acc, pl.x - 1.0, pl.y + 1.0, u1, stdev);
    sgsr_weight(acc, pl.x - 1.0, pl.y - 2.0, d1, stdev);
    sgsr_weight(acc, pl.x,       pl.y - 2.0, d0, stdev);
    sgsr_weight(acc, pl.x + 1.0, pl.y - 1.0, l1, stdev);
    sgsr_weight(acc, pl.x,       pl.y - 1.0, c,  stdev);
    sgsr_weight(acc, pl.x,       pl.y,       a,  stdev);
    sgsr_weight(acc, pl.x + 1.0, pl.y,       l0, stdev);
    sgsr_weight(acc, pl.x - 1.0, pl.y - 1.0, d,  stdev);
    sgsr_weight(acc, pl.x - 2.0, pl.y - 1.0, r1, stdev);
    sgsr_weight(acc, pl.x - 2.0, pl.y,       r0, stdev);
    sgsr_weight(acc, pl.x - 1.0, pl.y,       b,  stdev);

    float finalY = acc.x != 0.0 ? acc.y / acc.x : centred;
    float maxY = max(max(a, b), max(c, d));
    float minY = min(min(a, b), min(c, d));
    finalY = clamp(SGSR_EDGE_SHARPNESS * finalY, minY, maxY);

    // Smooth high-contrast input.
    float deltaY = clamp(finalY - centred, -23.0 / 255.0, 23.0 / 255.0);
    return vec4(clamp(color + deltaY, 0.0, 1.0), 0.0, 0.0, 1.0);
}
