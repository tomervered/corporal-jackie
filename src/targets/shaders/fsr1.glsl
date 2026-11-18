// AMD FidelityFX Super Resolution 1.0 (EASU upscale + RCAS sharpen) as an mpv
// user shader. Runs on the luma plane only; mpv scales chroma to match.
// Only active when the target is larger than the video's own size.
//
// The app fills in the @...@ size expressions before handing the file to
// mpv: OUTPUT (the size the video is shown at) or a fixed target height,
// after which mpv's own scaler fits the result to the window.
//
// Ported for Corporal Jackie from AMD's ffx_fsr1.h (32-bit path), with the
// fast bit-trick reciprocals replaced by plain division and guards for the
// zero cases those tricks hid. AMD's notice, which this port keeps:
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//!HOOK LUMA
//!BIND HOOKED
//!SAVE FSR_EASU
//!DESC FSR 1 EASU (upscale)
//!WHEN @WHEN@
//!WIDTH @WIDTH@
//!HEIGHT @HEIGHT@
//!COMPONENTS 1

// One input texel, 'off' texels from the 'f' texel at 'fp'.
float easu_texel(vec2 fp, vec2 off)
{
    return HOOKED_tex((fp + off + vec2(0.5)) * HOOKED_pt).x;
}

// FsrEasuSetF: accumulates edge direction and length for one of the four
// texels around the output position, weighted bilinearly by 'w'.
//    a
//  b c d
//    e
void easu_set(inout vec2 dir, inout float len, float w,
              float lA, float lB, float lC, float lD, float lE)
{
    float lenX = max(abs(lD - lC), abs(lC - lB));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = lenX > 0.0 ? clamp(abs(dirX) / lenX, 0.0, 1.0) : 0.0;
    len += lenX * lenX * w;

    float lenY = max(abs(lE - lC), abs(lC - lA));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = lenY > 0.0 ? clamp(abs(dirY) / lenY, 0.0, 1.0) : 0.0;
    len += lenY * lenY * w;
}

// FsrEasuTapF: one tap of the rotated, stretched lanczos(2) approximation.
void easu_tap(inout float aC, inout float aW, vec2 off, vec2 dir, vec2 len2,
              float lob, float clp, float c)
{
    vec2 v = vec2(dot(off, dir), dot(off, vec2(-dir.y, dir.x))) * len2;
    float d2 = min(dot(v, v), clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

vec4 hook()
{
    // Position in input texels; 'f' is the texel up and left of it.
    vec2 pp = HOOKED_pos * HOOKED_size - vec2(0.5);
    vec2 fp = floor(pp);
    pp -= fp;

    // 12-tap kernel:
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    float b = easu_texel(fp, vec2( 0.0, -1.0));
    float c = easu_texel(fp, vec2( 1.0, -1.0));
    float e = easu_texel(fp, vec2(-1.0,  0.0));
    float f = easu_texel(fp, vec2( 0.0,  0.0));
    float g = easu_texel(fp, vec2( 1.0,  0.0));
    float h = easu_texel(fp, vec2( 2.0,  0.0));
    float i = easu_texel(fp, vec2(-1.0,  1.0));
    float j = easu_texel(fp, vec2( 0.0,  1.0));
    float k = easu_texel(fp, vec2( 1.0,  1.0));
    float l = easu_texel(fp, vec2( 2.0,  1.0));
    float n = easu_texel(fp, vec2( 0.0,  2.0));
    float o = easu_texel(fp, vec2( 1.0,  2.0));

    vec2 dir = vec2(0.0);
    float len = 0.0;
    easu_set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), b, e, f, g, j);
    easu_set(dir, len, pp.x * (1.0 - pp.y),         c, f, g, h, k);
    easu_set(dir, len, (1.0 - pp.x) * pp.y,         f, i, j, k, n);
    easu_set(dir, len, pp.x * pp.y,                 g, j, k, l, o);

    // Normalize the direction. AMD works on luma times two, so its 1/32768
    // threshold on the squared length is 1/131072 here.
    float dirR = dot(dir, dir);
    bool zro = dirR < 1.0 / 131072.0;
    dir = zro ? vec2(1.0, 0.0) : dir * inversesqrt(dirR);

    // Edge amount, 0 to 1, shaped with a square.
    len = len * 0.5;
    len *= len;
    // Stretch the kernel from 1.0 (horizontal/vertical) to sqrt(2) on diagonals.
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    float aC = 0.0;
    float aW = 0.0;
    easu_tap(aC, aW, vec2( 0.0, -1.0) - pp, dir, len2, lob, clp, b);
    easu_tap(aC, aW, vec2( 1.0, -1.0) - pp, dir, len2, lob, clp, c);
    easu_tap(aC, aW, vec2(-1.0,  1.0) - pp, dir, len2, lob, clp, i);
    easu_tap(aC, aW, vec2( 0.0,  1.0) - pp, dir, len2, lob, clp, j);
    easu_tap(aC, aW, vec2( 0.0,  0.0) - pp, dir, len2, lob, clp, f);
    easu_tap(aC, aW, vec2(-1.0,  0.0) - pp, dir, len2, lob, clp, e);
    easu_tap(aC, aW, vec2( 1.0,  1.0) - pp, dir, len2, lob, clp, k);
    easu_tap(aC, aW, vec2( 2.0,  1.0) - pp, dir, len2, lob, clp, l);
    easu_tap(aC, aW, vec2( 2.0,  0.0) - pp, dir, len2, lob, clp, h);
    easu_tap(aC, aW, vec2( 1.0,  0.0) - pp, dir, len2, lob, clp, g);
    easu_tap(aC, aW, vec2( 1.0,  2.0) - pp, dir, len2, lob, clp, o);
    easu_tap(aC, aW, vec2( 0.0,  2.0) - pp, dir, len2, lob, clp, n);

    // Normalize, and clamp to the 2x2 neighbourhood to remove ringing.
    float lo = min(min(f, g), min(j, k));
    float hi = max(max(f, g), max(j, k));
    return vec4(clamp(aC / aW, lo, hi), 0.0, 0.0, 1.0);
}

//!HOOK LUMA
//!BIND FSR_EASU
//!DESC FSR 1 RCAS (sharpen)
//!WHEN @WHEN@
//!WIDTH FSR_EASU.w
//!HEIGHT FSR_EASU.h
//!COMPONENTS 1

// Sharpness in stops: 0.0 is the strongest; each +1.0 halves it.
#define FSR_SHARPNESS 0.2
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

vec4 hook()
{
    //    b
    //  d e f
    //    h
    float b = FSR_EASU_texOff(vec2( 0.0, -1.0)).x;
    float d = FSR_EASU_texOff(vec2(-1.0,  0.0)).x;
    float e = FSR_EASU_texOff(vec2( 0.0,  0.0)).x;
    float f = FSR_EASU_texOff(vec2( 1.0,  0.0)).x;
    float h = FSR_EASU_texOff(vec2( 0.0,  1.0)).x;

    float mn4 = min(min(b, d), min(f, h));
    float mx4 = max(max(b, d), max(f, h));

    // Solve for the largest negative lobe that does not clip, as AMD does.
    float hitMin = min(mn4, e) / max(4.0 * mx4, 1e-6);
    float hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, -1e-6);
    float lobe = max(-hitMin, hitMax);
    lobe = max(-FSR_RCAS_LIMIT, min(lobe, 0.0)) * exp2(-FSR_SHARPNESS);

    float pix = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    return vec4(pix, 0.0, 0.0, 1.0);
}
