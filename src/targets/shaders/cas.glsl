// AMD FidelityFX Contrast Adaptive Sharpening (CAS) with its built-in
// upscale, as an mpv user shader: one pass, 12 texture reads, sharpening
// while it scales. Cheaper than FSR (two passes) and gentler on detail.
// Runs on the luma plane only; mpv scales chroma to match. Only active when
// the target is larger than the video's own size.
//
// The app fills in the @...@ size expressions before handing the file to
// mpv, as for fsr1.glsl.
//
// Ported for Corporal Jackie from AMD's ffx_cas.h (CasFilter, scaling path,
// exact reciprocals and square roots as with CAS_GO_SLOWER), on luma alone.
// AMD's notice, which this port keeps:
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//!HOOK LUMA
//!BIND HOOKED
//!DESC AMD CAS (sharpen and upscale)
//!WHEN @WHEN@
//!WIDTH @WIDTH@
//!HEIGHT @HEIGHT@
//!COMPONENTS 1

// 0 = least ringing, 1 = sharpest (AMD's own range).
#define CAS_SHARPNESS 0.4

float cas_texel(vec2 fp, vec2 off)
{
    return HOOKED_tex((fp + off + vec2(0.5)) * HOOKED_pt).x;
}

// How much one of the four texels around the position is sharpened, from
// the minimum and maximum of its cross (centre, up, left, right, down).
float cas_amp(float mn, float mx)
{
    return mx > 0.0 ? sqrt(clamp(min(mn, 1.0 - mx) / mx, 0.0, 1.0)) : 0.0;
}

vec4 hook()
{
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec2 pp = HOOKED_pos * HOOKED_size - vec2(0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    float b = cas_texel(fp, vec2( 0.0, -1.0));
    float c = cas_texel(fp, vec2( 1.0, -1.0));
    float e = cas_texel(fp, vec2(-1.0,  0.0));
    float f = cas_texel(fp, vec2( 0.0,  0.0));
    float g = cas_texel(fp, vec2( 1.0,  0.0));
    float h = cas_texel(fp, vec2( 2.0,  0.0));
    float i = cas_texel(fp, vec2(-1.0,  1.0));
    float j = cas_texel(fp, vec2( 0.0,  1.0));
    float k = cas_texel(fp, vec2( 1.0,  1.0));
    float l = cas_texel(fp, vec2( 2.0,  1.0));
    float n = cas_texel(fp, vec2( 0.0,  2.0));
    float o = cas_texel(fp, vec2( 1.0,  2.0));

    // Minimum and maximum of each of the four crosses (f, g, j, k at the centre).
    float mnf = min(min(min(b, e), f), min(g, j)), mxf = max(max(max(b, e), f), max(g, j));
    float mng = min(min(min(c, f), g), min(h, k)), mxg = max(max(max(c, f), g), max(h, k));
    float mnj = min(min(min(f, i), j), min(k, n)), mxj = max(max(max(f, i), j), max(k, n));
    float mnk = min(min(min(g, j), k), min(l, o)), mxk = max(max(max(g, j), k), max(l, o));

    // Filter shape: 0 w 0 / w 1 w / 0 w 0, w negative.
    float peak = -1.0 / mix(8.0, 5.0, clamp(CAS_SHARPNESS, 0.0, 1.0));
    float wf = cas_amp(mnf, mxf) * peak;
    float wg = cas_amp(mng, mxg) * peak;
    float wj = cas_amp(mnj, mxj) * peak;
    float wk = cas_amp(mnk, mxk) * peak;

    // Bilinear blend of the four results, thinning edges to hide it.
    const float thinB = 1.0 / 32.0;
    float s = (1.0 - pp.x) * (1.0 - pp.y) / (thinB + (mxf - mnf));
    float t =        pp.x  * (1.0 - pp.y) / (thinB + (mxg - mng));
    float u = (1.0 - pp.x) *        pp.y  / (thinB + (mxj - mnj));
    float v =        pp.x  *        pp.y  / (thinB + (mxk - mnk));

    float qbe = wf * s;
    float qch = wg * t;
    float qf = wg * t + wj * u + s;
    float qg = wf * s + wk * v + t;
    float qj = wf * s + wk * v + u;
    float qk = wg * t + wj * u + v;
    float qin = wj * u;
    float qlo = wk * v;

    float sum = 2.0 * qbe + 2.0 * qch + 2.0 * qin + 2.0 * qlo + qf + qg + qj + qk;
    float value = (b * qbe + e * qbe + c * qch + h * qch + i * qin + n * qin + l * qlo + o * qlo
                 + f * qf + g * qg + j * qj + k * qk);
    float result = sum != 0.0 ? value / sum : f;
    return vec4(clamp(result, 0.0, 1.0), 0.0, 0.0, 1.0);
}
