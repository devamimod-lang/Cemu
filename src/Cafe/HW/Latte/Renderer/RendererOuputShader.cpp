#include "Cafe/HW/Latte/Renderer/RendererOuputShader.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "config/ActiveSettings.h"

const std::string RendererOutputShader::s_copy_shader_source =
R"(
void outputShader()
{
	colorOut0 = vec4(texture(textureSrc, passUV).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_copy_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
	return float4(textureSrc.sample(samplr, in.uv).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source =
R"(
vec4 cubic(float x)
{
	float x2 = x * x;
	float x3 = x2 * x;
	vec4 w;
	w.x = -x3 + 3 * x2 - 3 * x + 1;
	w.y = 3 * x3 - 6 * x2 + 4;
	w.z = -3 * x3 + 3 * x2 + 3 * x + 1;
	w.w = x3;
	return w / 6.0;
}

vec4 bcFilter(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw - 0.5;
	vec2 pixelFrac = fract(pixel);
	vec2 pixelInt = pixel - pixelFrac;

	vec4 xcubic = cubic(pixelFrac.x);
	vec4 ycubic = cubic(pixelFrac.y);

	vec4 c = vec4(pixelInt.x - 0.5, pixelInt.x + 1.5, pixelInt.y - 0.5, pixelInt.y + 1.5);
	vec4 s = vec4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	vec4 offset = c + vec4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	vec4 sample0 = texture(textureSrc, vec2(offset.x, offset.z) * texelSize.xy);
	vec4 sample1 = texture(textureSrc, vec2(offset.y, offset.z) * texelSize.xy);
	vec4 sample2 = texture(textureSrc, vec2(offset.x, offset.w) * texelSize.xy);
	vec4 sample3 = texture(textureSrc, vec2(offset.y, offset.w) * texelSize.xy);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(bcFilter(passUV, texelSize).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

float4 cubic(float x) {
	float x2 = x * x;
	float x3 = x2 * x;
	float4 w;
	w.x = -x3 + 3 * x2 - 3 * x + 1;
	w.y = 3 * x3 - 6 * x2 + 4;
	w.z = -3 * x3 + 3 * x2 + 3 * x + 1;
	w.w = x3;
	return w / 6.0;
}

float4 bcFilter(texture2d<float> textureSrc, sampler samplr, float2 texcoord, float2 texscale) {
	float fx = fract(texcoord.x);
	float fy = fract(texcoord.y);
	texcoord.x -= fx;
	texcoord.y -= fy;

	float4 xcubic = cubic(fx);
	float4 ycubic = cubic(fy);

	float4 c = float4(texcoord.x - 0.5, texcoord.x + 1.5, texcoord.y - 0.5, texcoord.y + 1.5);
	float4 s = float4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	float4 offset = c + float4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	float4 sample0 = textureSrc.sample(samplr, float2(offset.x, offset.z) * texscale);
	float4 sample1 = textureSrc.sample(samplr, float2(offset.y, offset.z) * texscale);
	float4 sample2 = textureSrc.sample(samplr, float2(offset.x, offset.w) * texscale);
	float4 sample3 = textureSrc.sample(samplr, float2(offset.y, offset.w) * texscale);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
    float2 textureSrcResolution = float2(textureSrc.get_width(), textureSrc.get_height());
	return float4(bcFilter(textureSrc, samplr, in.uv * textureSrcResolution, float2(1.0, 1.0) / textureSrcResolution).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source =
R"(
// https://www.shadertoy.com/view/MllSzX

vec3 CubicHermite (vec3 A, vec3 B, vec3 C, vec3 D, float t)
{
	float t2 = t*t;
    float t3 = t*t*t;
    vec3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    vec3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    vec3 c = -A/2.0 + C/2.0;
   	vec3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


vec3 BicubicHermiteTexture(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw + 0.5;
	vec2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - vec2(texelSize.xy/2.0);

	vec4 doubleSize = texelSize*2.0;

	vec3 C00 = texture(textureSrc, pixel + vec2(-texelSize.x ,-texelSize.y)).rgb;
    vec3 C10 = texture(textureSrc, pixel + vec2( 0.0        ,-texelSize.y)).rgb;
    vec3 C20 = texture(textureSrc, pixel + vec2( texelSize.x ,-texelSize.y)).rgb;
    vec3 C30 = texture(textureSrc, pixel + vec2( doubleSize.x,-texelSize.y)).rgb;

    vec3 C01 = texture(textureSrc, pixel + vec2(-texelSize.x , 0.0)).rgb;
    vec3 C11 = texture(textureSrc, pixel + vec2( 0.0        , 0.0)).rgb;
    vec3 C21 = texture(textureSrc, pixel + vec2( texelSize.x , 0.0)).rgb;
    vec3 C31 = texture(textureSrc, pixel + vec2( doubleSize.x, 0.0)).rgb;

    vec3 C02 = texture(textureSrc, pixel + vec2(-texelSize.x , texelSize.y)).rgb;
    vec3 C12 = texture(textureSrc, pixel + vec2( 0.0        , texelSize.y)).rgb;
    vec3 C22 = texture(textureSrc, pixel + vec2( texelSize.x , texelSize.y)).rgb;
    vec3 C32 = texture(textureSrc, pixel + vec2( doubleSize.x, texelSize.y)).rgb;

    vec3 C03 = texture(textureSrc, pixel + vec2(-texelSize.x , doubleSize.y)).rgb;
    vec3 C13 = texture(textureSrc, pixel + vec2( 0.0        , doubleSize.y)).rgb;
    vec3 C23 = texture(textureSrc, pixel + vec2( texelSize.x , doubleSize.y)).rgb;
    vec3 C33 = texture(textureSrc, pixel + vec2( doubleSize.x, doubleSize.y)).rgb;

    vec3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    vec3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    vec3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    vec3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(BicubicHermiteTexture(passUV, texelSize), 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

// https://www.shadertoy.com/view/MllSzX

float3 CubicHermite(float3 A, float3 B, float3 C, float3 D, float t) {
	float t2 = t*t;
    float t3 = t*t*t;
    float3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    float3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    float3 c = -A/2.0 + C/2.0;
   	float3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


float3 BicubicHermiteTexture(texture2d<float> textureSrc, sampler samplr, float2 uv, float4 texelSize) {
	float2 pixel = uv*texelSize.zw + 0.5;
	float2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - float2(texelSize.xy/2.0);

	float4 doubleSize = texelSize*texelSize;

	float3 C00 = textureSrc.sample(samplr, pixel + float2(-texelSize.x ,-texelSize.y)).rgb;
    float3 C10 = textureSrc.sample(samplr, pixel + float2( 0.0        ,-texelSize.y)).rgb;
    float3 C20 = textureSrc.sample(samplr, pixel + float2( texelSize.x ,-texelSize.y)).rgb;
    float3 C30 = textureSrc.sample(samplr, pixel + float2( doubleSize.x,-texelSize.y)).rgb;

    float3 C01 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , 0.0)).rgb;
    float3 C11 = textureSrc.sample(samplr, pixel + float2( 0.0        , 0.0)).rgb;
    float3 C21 = textureSrc.sample(samplr, pixel + float2( texelSize.x , 0.0)).rgb;
    float3 C31 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, 0.0)).rgb;

    float3 C02 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , texelSize.y)).rgb;
    float3 C12 = textureSrc.sample(samplr, pixel + float2( 0.0        , texelSize.y)).rgb;
    float3 C22 = textureSrc.sample(samplr, pixel + float2( texelSize.x , texelSize.y)).rgb;
    float3 C32 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, texelSize.y)).rgb;

    float3 C03 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , doubleSize.y)).rgb;
    float3 C13 = textureSrc.sample(samplr, pixel + float2( 0.0        , doubleSize.y)).rgb;
    float3 C23 = textureSrc.sample(samplr, pixel + float2( texelSize.x , doubleSize.y)).rgb;
    float3 C33 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, doubleSize.y)).rgb;

    float3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    float3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    float3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    float3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]], constant float2& outputResolution [[buffer(0)]]) {
	float4 texelSize = float4(1.0 / outputResolution.xy, outputResolution.xy);
	return float4(BicubicHermiteTexture(textureSrc, samplr, in.uv, texelSize), 1.0);
}
)";
const std::string RendererOutputShader::s_fsr1_easu_shader_source =
R"( // FSR1 EASU (Edge-Adaptive Spatial Upsampling) - pass 1 of 2. Faithful GLSL
 // port of the AF1 (32-bit) path of AMD's ffx_fsr1.h (MIT license):
 // https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
 // Algorithm/constants kept verbatim; only the packed "con0..con3" constant
 // setup and the textureGather-based tap fetch (FsrEasuRF/GF/BF) are replaced
 // with direct per-tap texelFetch, since Cemu doesn't need the packed-constant
 // CPU/GPU-shared setup path and this avoids relying on matching AMD's own
 // gather4 component-ordering convention exactly. Reads the game's own
 // (pre-upscale) texture via textureSrc, writes the raw upscaled-but-unsharpened
 // image - RCAS (pass 2, s_fsr1_rcas_shader_source) reads THIS pass's output to
 // do the actual sharpening, since real RCAS needs already-upscaled neighbour
 // pixels that don't exist yet during EASU itself (this is why AMD's own FSR1
 // is always 2 passes, never fused into one - our earlier single-pass "fused"
 // attempt could never sharpen correctly for exactly this reason).
 // Uniforms from PrependFragmentPreamble: textureSrc, textureSrcResolution, outputResolution, passUV

 float ARcpF1(float x) { return 1.0 / x; }
 float APrxLoRcpF1(float a) { return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(a)); }
 float APrxLoRsqF1(float a) { return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(a) >> 1u)); }
 float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }
 float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }
 vec3 AMin3F3(vec3 x, vec3 y, vec3 z) { return min(x, min(y, z)); }
 vec3 AMax3F3(vec3 x, vec3 y, vec3 z) { return max(x, max(y, z)); }
 float ASatF1(float x) { return clamp(x, 0.0, 1.0); }

 // Filtering for a given tap (verbatim FsrEasuTapF).
 void FsrEasuTapF(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
     vec2 v;
     v.x = (off.x * ( dir.x)) + (off.y * dir.y);
     v.y = (off.x * (-dir.y)) + (off.y * dir.x);
     v *= len;
     float d2 = v.x * v.x + v.y * v.y;
     d2 = min(d2, clp);
     float wB = (2.0 / 5.0) * d2 + (-1.0);
     float wA = lob * d2 + (-1.0);
     wB *= wB;
     wA *= wA;
     wB = (25.0 / 16.0) * wB + (-(25.0 / 16.0 - 1.0));
     float w = wB * wA;
     aC += c * w; aW += w;
 }

 // Accumulate direction and length (verbatim FsrEasuSetF).
 void FsrEasuSetF(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV, float lA, float lB, float lC, float lD, float lE) {
     float w = 0.0;
     if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
     if (biT) w =        pp.x  * (1.0 - pp.y);
     if (biU) w = (1.0 - pp.x) *        pp.y;
     if (biV) w =        pp.x  *        pp.y;
     float dc = lD - lC;
     float cb = lC - lB;
     float lenX = max(abs(dc), abs(cb));
     lenX = APrxLoRcpF1(lenX);
     float dirX = lD - lB;
     dir.x += dirX * w;
     lenX = ASatF1(abs(dirX) * lenX);
     lenX *= lenX;
     len += lenX * w;
     float ec = lE - lC;
     float ca = lC - lA;
     float lenY = max(abs(ec), abs(ca));
     lenY = APrxLoRcpF1(lenY);
     float dirY = lE - lA;
     dir.y += dirY * w;
     lenY = ASatF1(abs(dirY) * lenY);
     lenY *= lenY;
     len += lenY * w;
 }

 void outputShader() {
     // ip = integer output pixel position (AMD's FsrEasuCon con0/con1/con2/con3
     // folded directly into textureSrcResolution/outputResolution, since Cemu
     // always upscales the full source viewport with no sub-rect offset).
     // Derived from passUV, NOT gl_FragCoord - render_upside_down flips passUV
     // in the vertex shader while leaving gl_Position (and therefore
     // gl_FragCoord) unchanged, so gl_FragCoord alone would silently ignore
     // the flip. EASU is always the pass that carries the flip (see
     // s_fsr1_easu_shader_ud) - every later pass (RCAS, then FXAA/SMAA) reads
     // gl_FragCoord directly, which is correct there since by that point the
     // flip is already baked into this pass's own output pixel contents.
     vec2 ip = floor(passUV * outputResolution);
     vec2 pp = ip * (textureSrcResolution / outputResolution) + (0.5 * (textureSrcResolution / outputResolution) - 0.5);
     vec2 fp = floor(pp);
     pp -= fp;
     ivec2 fpi = ivec2(fp);

     // 12-tap kernel, named exactly as AMD's own diagram:
     //    b c
     //  e f g h
     //  i j k l
     //    n o
     vec3 bC = texelFetch(textureSrc, fpi + ivec2( 0,-1), 0).rgb;
     vec3 cC = texelFetch(textureSrc, fpi + ivec2( 1,-1), 0).rgb;
     vec3 eC = texelFetch(textureSrc, fpi + ivec2(-1, 0), 0).rgb;
     vec3 fC = texelFetch(textureSrc, fpi + ivec2( 0, 0), 0).rgb;
     vec3 gC = texelFetch(textureSrc, fpi + ivec2( 1, 0), 0).rgb;
     vec3 hC = texelFetch(textureSrc, fpi + ivec2( 2, 0), 0).rgb;
     vec3 iC = texelFetch(textureSrc, fpi + ivec2(-1, 1), 0).rgb;
     vec3 jC = texelFetch(textureSrc, fpi + ivec2( 0, 1), 0).rgb;
     vec3 kC = texelFetch(textureSrc, fpi + ivec2( 1, 1), 0).rgb;
     vec3 lC = texelFetch(textureSrc, fpi + ivec2( 2, 1), 0).rgb;
     vec3 nC = texelFetch(textureSrc, fpi + ivec2( 0, 2), 0).rgb;
     vec3 oC = texelFetch(textureSrc, fpi + ivec2( 1, 2), 0).rgb;

     // Simplest multi-channel approximate luma possible (luma times 2).
     float bL = bC.b * 0.5 + (bC.r * 0.5 + bC.g);
     float cL = cC.b * 0.5 + (cC.r * 0.5 + cC.g);
     float eL = eC.b * 0.5 + (eC.r * 0.5 + eC.g);
     float fL = fC.b * 0.5 + (fC.r * 0.5 + fC.g);
     float gL = gC.b * 0.5 + (gC.r * 0.5 + gC.g);
     float hL = hC.b * 0.5 + (hC.r * 0.5 + hC.g);
     float iL = iC.b * 0.5 + (iC.r * 0.5 + iC.g);
     float jL = jC.b * 0.5 + (jC.r * 0.5 + jC.g);
     float kL = kC.b * 0.5 + (kC.r * 0.5 + kC.g);
     float lL = lC.b * 0.5 + (lC.r * 0.5 + lC.g);
     float nL = nC.b * 0.5 + (nC.r * 0.5 + nC.g);
     float oL = oC.b * 0.5 + (oC.r * 0.5 + oC.g);

     // Accumulate for bilinear interpolation.
     vec2 dir = vec2(0.0);
     float len = 0.0;
     FsrEasuSetF(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
     FsrEasuSetF(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
     FsrEasuSetF(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
     FsrEasuSetF(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);

     // Normalize with approximation, and cleanup close to zero.
     vec2 dir2 = dir * dir;
     float dirR = dir2.x + dir2.y;
     bool zro = dirR < (1.0 / 32768.0);
     dirR = APrxLoRsqF1(dirR);
     dirR = zro ? 1.0 : dirR;
     dir.x = zro ? 1.0 : dir.x;
     dir *= vec2(dirR);
     // Transform from {0 to 2} to {0 to 1} range, and shape with square.
     len = len * 0.5;
     len *= len;
     // Stretch kernel {1.0 vert|horz, to sqrt(2.0) on diagonal}.
     float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
     vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
     float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
     float clp = APrxLoRcpF1(lob);

     // Accumulation mixed with min/max of the 4 nearest (f,g,j,k) for dering.
     vec3 min4 = min(AMin3F3(fC, gC, jC), kC);
     vec3 max4 = max(AMax3F3(fC, gC, jC), kC);
     vec3 aC = vec3(0.0);
     float aW = 0.0;
     FsrEasuTapF(aC, aW, vec2( 0.0,-1.0) - pp, dir, len2, lob, clp, bC);
     FsrEasuTapF(aC, aW, vec2( 1.0,-1.0) - pp, dir, len2, lob, clp, cC);
     FsrEasuTapF(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, iC);
     FsrEasuTapF(aC, aW, vec2( 0.0, 1.0) - pp, dir, len2, lob, clp, jC);
     FsrEasuTapF(aC, aW, vec2( 0.0, 0.0) - pp, dir, len2, lob, clp, fC);
     FsrEasuTapF(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, eC);
     FsrEasuTapF(aC, aW, vec2( 1.0, 1.0) - pp, dir, len2, lob, clp, kC);
     FsrEasuTapF(aC, aW, vec2( 2.0, 1.0) - pp, dir, len2, lob, clp, lC);
     FsrEasuTapF(aC, aW, vec2( 2.0, 0.0) - pp, dir, len2, lob, clp, hC);
     FsrEasuTapF(aC, aW, vec2( 1.0, 0.0) - pp, dir, len2, lob, clp, gC);
     FsrEasuTapF(aC, aW, vec2( 1.0, 2.0) - pp, dir, len2, lob, clp, oC);
     FsrEasuTapF(aC, aW, vec2( 0.0, 2.0) - pp, dir, len2, lob, clp, nC);

     vec3 pix = min(max4, max(min4, aC * vec3(ARcpF1(aW))));
     colorOut0 = vec4(pix, 1.0);
 }
)";

const std::string RendererOutputShader::s_fsr1_rcas_shader_source =
R"( // FSR1 RCAS (Robust Contrast-Adaptive Sharpening) - pass 2 of 2. Faithful
 // GLSL port of the AF1 (32-bit) path of AMD's ffx_fsr1.h (MIT license), same
 // reference as s_fsr1_easu_shader_source. Reads THIS FRAME's already-upscaled
 // (but unsharpened) image, written by the EASU pass into the same
 // intermediate target FXAA/SMAA already reuse as their own FSR1 input -
 // see DrawBackbufferQuadFsr1/DrawBackbufferQuadTwoPass/DrawBackbufferQuadFsr1Smaa.
 // Uniforms from PrependFragmentPreamble: textureSrc, outputResolution, passUV

 float ARcpF1(float x) { return 1.0 / x; }
 float APrxMedRcpF1(float a) { float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(a)); return b * (-b * a + 2.0); }
 float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }
 float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }
 float ASatF1(float x) { return clamp(x, 0.0, 1.0); }
 // (0.25 - (1.0/16.0)), AMD's own FSR_RCAS_LIMIT - clamps the max sharpening
 // strength so the resolve rcp below never divides by a near-zero value.
 #define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))
 // Sharpness in AMD's "stops" scale (0.0 = sharpest, higher = softer) - no UI
 // control for this yet, 0.2 matches the commonly-used default in other FSR1
 // integrations.
 #define FSR_RCAS_SHARPNESS 0.2

 void outputShader() {
     // Derived from passUV (quad-relative, 0..outputResolution), NOT
     // gl_FragCoord (framebuffer-absolute) - when this pass writes directly
     // to the real backbuffer with letterboxing (imageX/imageY != 0 in
     // DrawBackbufferQuadFsr1), gl_FragCoord would be offset by that amount
     // relative to the intermediate texture's own (0,0)-based texel grid,
     // reading the wrong neighbourhood. Same reasoning FXAA/SMAA's shaders
     // already use passUV for instead of gl_FragCoord.
     ivec2 sp = ivec2(floor(passUV * outputResolution));
     // Algorithm uses minimal 3x3 pixel neighborhood, cross-shaped:
     //    b
     //  d e f
     //    h
     vec3 bC = texelFetch(textureSrc, sp + ivec2( 0,-1), 0).rgb;
     vec3 dC = texelFetch(textureSrc, sp + ivec2(-1, 0), 0).rgb;
     vec3 eC = texelFetch(textureSrc, sp, 0).rgb;
     vec3 fC = texelFetch(textureSrc, sp + ivec2( 1, 0), 0).rgb;
     vec3 hC = texelFetch(textureSrc, sp + ivec2( 0, 1), 0).rgb;

     // Luma times 2.
     float bL = bC.b * 0.5 + (bC.r * 0.5 + bC.g);
     float dL = dC.b * 0.5 + (dC.r * 0.5 + dC.g);
     float eL = eC.b * 0.5 + (eC.r * 0.5 + eC.g);
     float fL = fC.b * 0.5 + (fC.r * 0.5 + fC.g);
     float hL = hC.b * 0.5 + (hC.r * 0.5 + hC.g);

     // Min and max of ring.
     float mn4R = min(AMin3F1(bC.r, dC.r, fC.r), hC.r);
     float mn4G = min(AMin3F1(bC.g, dC.g, fC.g), hC.g);
     float mn4B = min(AMin3F1(bC.b, dC.b, fC.b), hC.b);
     float mx4R = max(AMax3F1(bC.r, dC.r, fC.r), hC.r);
     float mx4G = max(AMax3F1(bC.g, dC.g, fC.g), hC.g);
     float mx4B = max(AMax3F1(bC.b, dC.b, fC.b), hC.b);
     // Immediate constants for peak range.
     vec2 peakC = vec2(1.0, -1.0 * 4.0);
     // Limiters, these need to be high precision RCPs.
     float hitMinR = min(mn4R, eC.r) * ARcpF1(4.0 * mx4R);
     float hitMinG = min(mn4G, eC.g) * ARcpF1(4.0 * mx4G);
     float hitMinB = min(mn4B, eC.b) * ARcpF1(4.0 * mx4B);
     float hitMaxR = (peakC.x - max(mx4R, eC.r)) * ARcpF1(4.0 * mn4R + peakC.y);
     float hitMaxG = (peakC.x - max(mx4G, eC.g)) * ARcpF1(4.0 * mn4G + peakC.y);
     float hitMaxB = (peakC.x - max(mx4B, eC.b)) * ARcpF1(4.0 * mn4B + peakC.y);
     float lobeR = max(-hitMinR, hitMaxR);
     float lobeG = max(-hitMinG, hitMaxG);
     float lobeB = max(-hitMinB, hitMaxB);
     float sharpnessScale = exp2(-FSR_RCAS_SHARPNESS);
     float lobe = max(-FSR_RCAS_LIMIT, min(AMax3F1(lobeR, lobeG, lobeB), 0.0)) * sharpnessScale;
     // Resolve, which needs the medium precision rcp approximation to avoid
     // visible tonality changes.
     float rcpL = APrxMedRcpF1(4.0 * lobe + 1.0);
     vec3 pix;
     pix.r = (lobe * bC.r + lobe * dC.r + lobe * hC.r + lobe * fC.r + eC.r) * rcpL;
     pix.g = (lobe * bC.g + lobe * dC.g + lobe * hC.g + lobe * fC.g + eC.g) * rcpL;
     pix.b = (lobe * bC.b + lobe * dC.b + lobe * hC.b + lobe * fC.b + eC.b) * rcpL;
     colorOut0 = vec4(pix, 1.0);
 }
)";

const std::string RendererOutputShader::s_fxaa_shader_source =
R"( // FXAA 3.11 - NVIDIA TIMOTHY LOTTES, port to Cemu output shader
 // Reference: https://github.com/NVIDIAGameWorks/GraphicsSamples/blob/master/samples/es3-kepler/FXAA/FXAA3_11.h
 // Quality preset 39 (high quality, 12 taps), luma = green-as-luma = 0

 #define FXAA_SPAN_MAX 8.0
 #define FXAA_REDUCE_MUL 1.0/8.0
 #define FXAA_REDUCE_MIN 1.0/128.0
 #define FXAA_SUBPIX_MAX 0.75
 #define FXAA_SUBPIX_TRIM 0.25
 #define FXAA_SUBPIX_TRIM_SCALE (1.0/(1.0 - FXAA_SUBPIX_TRIM))

 float FxaaLuma(vec3 rgb) { return dot(rgb, vec3(0.299, 0.587, 0.114)); }

 void outputShader() {
     vec2 rcpFrame = 1.0 / outputResolution;
     vec3 rgbNW = texture(textureSrc, passUV + vec2(-1.0,-1.0) * rcpFrame).xyz;
     vec3 rgbNE = texture(textureSrc, passUV + vec2( 1.0,-1.0) * rcpFrame).xyz;
     vec3 rgbSW = texture(textureSrc, passUV + vec2(-1.0, 1.0) * rcpFrame).xyz;
     vec3 rgbSE = texture(textureSrc, passUV + vec2( 1.0, 1.0) * rcpFrame).xyz;
     vec3 rgbM  = texture(textureSrc, passUV).xyz;

     float lumaNW = FxaaLuma(rgbNW);
     float lumaNE = FxaaLuma(rgbNE);
     float lumaSW = FxaaLuma(rgbSW);
     float lumaSE = FxaaLuma(rgbSE);
     float lumaM  = FxaaLuma(rgbM);

     float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
     float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

     vec2 dir;
     dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
     dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

     float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
     float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
     dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX)) * rcpFrame;

     vec3 rgbA = 0.5 * (
         texture(textureSrc, passUV + dir * (1.0/3.0 - 0.5)).xyz +
         texture(textureSrc, passUV + dir * (2.0/3.0 - 0.5)).xyz);
     vec3 rgbB = rgbA * 0.5 + 0.25 * (
         texture(textureSrc, passUV + dir * -0.5).xyz +
         texture(textureSrc, passUV + dir *  0.5).xyz);

     float lumaB = FxaaLuma(rgbB);
     float rangeMin = lumaMin * 0.5;
     float rangeMax = lumaMax * 0.5;
     // choose
     vec3 rgbOut;
     if ((lumaB < lumaMin) || (lumaB > lumaMax))
         rgbOut = rgbA;
     else
         rgbOut = rgbB;

     // subpixel
     float lumaAve = (lumaNW + lumaNE + lumaSW + lumaSE) * 0.25;
     float subpix = clamp(abs(lumaAve - lumaM) / (lumaMax - lumaMin + 0.0001), 0.0, 1.0);
     subpix = subpix * subpix * FXAA_SUBPIX_MAX;
     rgbOut = mix(rgbOut, rgbM, subpix * FXAA_SUBPIX_TRIM_SCALE);

     colorOut0 = vec4(rgbOut, 1.0);
 }
)";

const std::string RendererOutputShader::s_smaa_edge_shader_body =
R"(
// SMAA (Subpixel Morphological Antialiasing) - Pass 1 of 3: luma edge
// detection. Near-verbatim port of SMAALumaEdgeDetectionPS from the public
// reference implementation, https://github.com/iryoku/smaa. SMAA_THRESHOLD is
// the only constant that varies by quality preset here - prepended by
// BuildSmaaEdgeShaderSource, not defined in this shared body. Reads the
// FSR1-output intermediate texture (bound as textureSrc) and writes a
// 2-channel edges mask (R=west edge, G=north edge). The edges render target
// MUST be cleared to (0,0,0,0) before this pass runs - pixels with no
// detected edge are left unwritten via `discard`, relying on that clear.
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASamplePoint(tex, coord) texture(tex, coord)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define SMAA_PREDICATION 0
#define mad(a, b, c) (a * b + c)
#define float2 vec2
#define float3 vec3
#define float4 vec4

#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0

vec4 rtMetrics;

float2 SMAALumaEdgeDetectionPS(float2 texcoord,
                               float4 offset[3],
                               SMAATexture2D(textureSrc)
                               #if SMAA_PREDICATION
                               , SMAATexture2D(predicationTex)
                               #endif
                               ) {
    // Calculate the threshold:
    #if SMAA_PREDICATION
    float2 threshold = SMAACalculatePredicatedThreshold(texcoord, offset, SMAATexturePass2D(predicationTex));
    #else
    float2 threshold = float2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    #endif

    // Calculate lumas:
    float3 weights = float3(0.2126, 0.7152, 0.0722);
    float L = dot(SMAASamplePoint(textureSrc, texcoord).rgb, weights);

    float Lleft = dot(SMAASamplePoint(textureSrc, offset[0].xy).rgb, weights);
    float Ltop  = dot(SMAASamplePoint(textureSrc, offset[0].zw).rgb, weights);

    // We do the usual threshold:
    float4 delta;
    delta.xy = abs(L - float2(Lleft, Ltop));
    float2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        discard;

    // Calculate right and bottom deltas:
    float Lright = dot(SMAASamplePoint(textureSrc, offset[1].xy).rgb, weights);
    float Lbottom  = dot(SMAASamplePoint(textureSrc, offset[1].zw).rgb, weights);
    delta.zw = abs(L - float2(Lright, Lbottom));

    // Calculate the maximum delta in the direct neighborhood:
    float2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    float Lleftleft = dot(SMAASamplePoint(textureSrc, offset[2].xy).rgb, weights);
    float Ltoptop = dot(SMAASamplePoint(textureSrc, offset[2].zw).rgb, weights);
    delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return edges;
}

void outputShader()
{
	rtMetrics = vec4(1.0 / outputResolution, outputResolution);
	vec2 texcoord = passUV;

	vec4 offsetArr[3];
	offsetArr[0] = mad(rtMetrics.xyxy, vec4(-1.0, 0.0, 0.0, -1.0), texcoord.xyxy);
	offsetArr[1] = mad(rtMetrics.xyxy, vec4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy);
	offsetArr[2] = mad(rtMetrics.xyxy, vec4(-2.0, 0.0, 0.0, -2.0), texcoord.xyxy);

	vec2 edges = SMAALumaEdgeDetectionPS(texcoord, offsetArr, textureSrc);
	colorOut0 = vec4(edges, 0.0, 1.0);
}

)";

const std::string RendererOutputShader::s_smaa_blend_shader_body =
R"(
// SMAA - Pass 2 of 3: blending weight calculation. Near-verbatim port of
// SMAABlendingWeightCalculationPS (plus its diagonal search, horizontal/
// vertical search, area lookup and corner-detection helper functions) from
// https://github.com/iryoku/smaa. SMAA_MAX_SEARCH_STEPS/MAX_SEARCH_STEPS_DIAG/
// CORNER_ROUNDING/DISABLE_DIAG_DETECTION/DISABLE_CORNER_DETECTION all vary by
// quality preset - prepended by BuildSmaaBlendShaderSource, not defined in
// this shared body. textureSrc = the edges mask from pass 1. textureSrc2 =
// the static area lookup texture, textureSrc3 = the static search lookup
// texture (see SMAALookupTextures.h - both loaded once at startup, never
// change). subsampleIndices is always 0 here - it only matters for SMAA
// T2x/S2x/4x (temporal/spatial supersampling), which this port doesn't
// implement (SMAA 1x only, matching FXAA's scope above). Like the edges
// target, this pass's own blend-weight render target isn't fully covered by
// every invocation in the same sense, but every pixel here DOES get written
// (no discard) so it doesn't need a clear.
// Declared here (not in the shared PrependFragmentPreamble) since this is the
// only pass besides neighborhood-blending that actually samples textureSrc2/3
// - see m_smaaBlendCalcDescriptorSet in EnsureSmaaIntermediateTargets for the
// matching descriptor writes.
layout(binding = 2) uniform sampler2D textureSrc2;
layout(binding = 3) uniform sampler2D textureSrc3;

#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASample(tex, coord) texture(tex, coord)
#define SMAASampleLevelZero(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) textureLodOffset(tex, coord, 0.0, offset)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) (a * b + c)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define bool2 bvec2
#define bool4 bvec4

// SMAA_MAX_SEARCH_STEPS/MAX_SEARCH_STEPS_DIAG/CORNER_ROUNDING are prepended
// by BuildSmaaBlendShaderSource (quality-dependent) - CORNER_ROUNDING_NORM's
// own definition just references CORNER_ROUNDING textually, so it's safe to
// define unconditionally here even for presets that never define
// CORNER_ROUNDING (SMAA_DISABLE_CORNER_DETECTION guards every actual use).
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)
#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)
#define SMAA_AREATEX_SELECT(sample) sample.rg
#define SMAA_SEARCHTEX_SELECT(sample) sample.r

vec4 rtMetrics;

/**
 * Conditional move:
 */
void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    SMAA_FLATTEN if (cond.x) variable.x = value.x;
    SMAA_FLATTEN if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

// Diagonal Search Functions

#if !defined(SMAA_DISABLE_DIAG_DETECTION)

/**
 * Allows to decode two binary values from a bilinear-filtered access.
 */
float2 SMAADecodeDiagBilinearAccess(float2 e) {
    // Bilinear access for fetching 'e' have a 0.25 offset, and we are
    // interested in the R and G edges:
    //
    // +---G---+-------+
    // |   x o R   x   |
    // +-------+-------+
    //
    // Then, if one of these edge is enabled:
    //   Red:   (0.75 * X + 0.25 * 1) => 0.25 or 1.0
    //   Green: (0.75 * 1 + 0.25 * X) => 0.75 or 1.0
    //
    // This function will unpack the values (mad + mul + round):
    // wolframalpha.com: round(x * abs(5 * x - 5 * 0.75)) plot 0 to 1
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

/**
 * These functions allows to perform diagonal pattern searches.
 */
float2 SMAASearchDiag1(SMAATexture2D(textureSrc), float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    float3 t = float3(rtMetrics.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
           coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = SMAASampleLevelZero(textureSrc, coord.xy).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(SMAATexture2D(textureSrc), float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * rtMetrics.x; // See @SearchDiag2Optimization
    float3 t = float3(rtMetrics.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
           coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);

        // @SearchDiag2Optimization
        // Fetch both edges at once using bilinear filtering:
        e = SMAASampleLevelZero(textureSrc, coord.xy).rg;
        e = SMAADecodeDiagBilinearAccess(e);

        // Non-optimized version:
        // e.g = SMAASampleLevelZero(textureSrc, coord.xy).g;
        // e.r = SMAASampleLevelZeroOffset(textureSrc, coord.xy, int2(1, 0)).r;

        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

/** 
 * Similar to SMAAArea, this calculates the area corresponding to a certain
 * diagonal distance and crossing edges 'e'.
 */
float2 SMAAAreaDiag(SMAATexture2D(textureSrc2), float2 dist, float2 e, float offset) {
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);

    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Diagonal areas are on the second half of the texture:
    texcoord.x += 0.5;

    // Move to proper place, according to the subpixel offset:
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    // Do it!
    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(textureSrc2, texcoord));
}

/**
 * This searches for diagonal patterns and returns the corresponding weights.
 */
float2 SMAACalculateDiagWeights(SMAATexture2D(textureSrc), SMAATexture2D(textureSrc2), float2 texcoord, float2 e, float4 subsampleIndices) {
    float2 weights = float2(0.0, 0.0);

    // Search for the line ends:
    float4 d;
    float2 end;
    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(SMAATexturePass2D(textureSrc), texcoord, float2(-1.0,  1.0), end);
        d.x += float(end.y > 0.9);
    } else
        d.xz = float2(0.0, 0.0);
    d.yw = SMAASearchDiag1(SMAATexturePass2D(textureSrc), texcoord, float2(1.0, -1.0), end);

    SMAA_BRANCH
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), rtMetrics.xyxy, texcoord.xyxy);
        float4 c;
        c.xy = SMAASampleLevelZeroOffset(textureSrc, coords.xy, int2(-1,  0)).rg;
        c.zw = SMAASampleLevelZeroOffset(textureSrc, coords.zw, int2( 1,  0)).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        // Non-optimized version:
        // float4 coords = mad(float4(-d.x, d.x, d.y, -d.y), rtMetrics.xyxy, texcoord.xyxy);
        // float4 c;
        // c.x = SMAASampleLevelZeroOffset(textureSrc, coords.xy, int2(-1,  0)).g;
        // c.y = SMAASampleLevelZeroOffset(textureSrc, coords.xy, int2( 0,  0)).r;
        // c.z = SMAASampleLevelZeroOffset(textureSrc, coords.zw, int2( 1,  0)).g;
        // c.w = SMAASampleLevelZeroOffset(textureSrc, coords.zw, int2( 1, -1)).r;

        // Merge crossing edges at each side into a single value:
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(SMAATexturePass2D(textureSrc2), d.xy, cc, subsampleIndices.z);
    }

    // Search for the line ends:
    d.xz = SMAASearchDiag2(SMAATexturePass2D(textureSrc), texcoord, float2(-1.0, -1.0), end);
    if (SMAASampleLevelZeroOffset(textureSrc, texcoord, int2(1, 0)).r > 0.0) {
        d.yw = SMAASearchDiag2(SMAATexturePass2D(textureSrc), texcoord, float2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else
        d.yw = float2(0.0, 0.0);

    SMAA_BRANCH
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), rtMetrics.xyxy, texcoord.xyxy);
        float4 c;
        c.x  = SMAASampleLevelZeroOffset(textureSrc, coords.xy, int2(-1,  0)).g;
        c.y  = SMAASampleLevelZeroOffset(textureSrc, coords.xy, int2( 0, -1)).r;
        c.zw = SMAASampleLevelZeroOffset(textureSrc, coords.zw, int2( 1,  0)).gr;
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(SMAATexturePass2D(textureSrc2), d.xy, cc, subsampleIndices.w).gr;
    }

    return weights;
}
#endif

//-----------------------------------------------------------------------------
// Horizontal/Vertical Search Functions

/**
 * This allows to determine how much length should we add in the last step
 * of the searches. It takes the bilinearly interpolated edge (see 
 * @PSEUDO_GATHER4), and adds 0, 1 or 2, depending on which edges and
 * crossing edges are active.
 */
float SMAASearchLength(SMAATexture2D(textureSrc3), float2 e, float offset) {
    // The texture is flipped vertically, with left and right cases taking half
    // of the space horizontally:
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);

    // Scale and bias to access texel centers:
    scale += float2(-1.0,  1.0);
    bias  += float2( 0.5, -0.5);

    // Convert from pixel coordinates to texcoords:
    // (We use SMAA_SEARCHTEX_PACKED_SIZE because the texture is cropped)
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    // Lookup the search texture:
    return SMAA_SEARCHTEX_SELECT(SMAASampleLevelZero(textureSrc3, mad(scale, e, bias)));
}

/**
 * Horizontal/vertical search functions for the 2nd pass.
 */
float SMAASearchXLeft(SMAATexture2D(textureSrc), SMAATexture2D(textureSrc3), float2 texcoord, float end) {
    /**
     * @PSEUDO_GATHER4
     * This texcoord has been offset by (-0.25, -0.125) in the vertex shader to
     * sample between edge, thus fetching four edges in a row.
     * Sampling with different offsets in each direction allows to disambiguate
     * which edges are active from the four fetched ones.
     */
    float2 e = float2(0.0, 1.0);
    while (texcoord.x > end && 
           e.g > 0.8281 && // Is there some edge not activated?
)" R"(           e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(textureSrc, texcoord).rg;
        texcoord = mad(-float2(2.0, 0.0), rtMetrics.xy, texcoord);
    }

    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(textureSrc3), e, 0.0), 3.25);
    return mad(rtMetrics.x, offset, texcoord.x);

    // Non-optimized version:
    // We correct the previous (-0.25, -0.125) offset we applied:
    // texcoord.x += 0.25 * rtMetrics.x;

    // The searches are bias by 1, so adjust the coords accordingly:
    // texcoord.x += rtMetrics.x;

    // Disambiguate the length added by the last step:
    // texcoord.x += 2.0 * rtMetrics.x; // Undo last step
    // texcoord.x -= rtMetrics.x * (255.0 / 127.0) * SMAASearchLength(SMAATexturePass2D(textureSrc3), e, 0.0);
    // return mad(rtMetrics.x, offset, texcoord.x);
}

float SMAASearchXRight(SMAATexture2D(textureSrc), SMAATexture2D(textureSrc3), float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    while (texcoord.x < end && 
           e.g > 0.8281 && // Is there some edge not activated?
           e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(textureSrc, texcoord).rg;
        texcoord = mad(float2(2.0, 0.0), rtMetrics.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(textureSrc3), e, 0.5), 3.25);
    return mad(-rtMetrics.x, offset, texcoord.x);
}

float SMAASearchYUp(SMAATexture2D(textureSrc), SMAATexture2D(textureSrc3), float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (texcoord.y > end && 
           e.r > 0.8281 && // Is there some edge not activated?
           e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(textureSrc, texcoord).rg;
        texcoord = mad(-float2(0.0, 2.0), rtMetrics.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(textureSrc3), e.gr, 0.0), 3.25);
    return mad(rtMetrics.y, offset, texcoord.y);
}

float SMAASearchYDown(SMAATexture2D(textureSrc), SMAATexture2D(textureSrc3), float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (texcoord.y < end && 
           e.r > 0.8281 && // Is there some edge not activated?
           e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(textureSrc, texcoord).rg;
        texcoord = mad(float2(0.0, 2.0), rtMetrics.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(textureSrc3), e.gr, 0.5), 3.25);
    return mad(-rtMetrics.y, offset, texcoord.y);
}

/** 
 * Ok, we have the distance and both crossing edges. So, what are the areas
 * at each side of current edge?
 */
float2 SMAAArea(SMAATexture2D(textureSrc2), float2 dist, float e1, float e2, float offset) {
    // Rounding prevents precision errors of bilinear filtering:
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist);
    
    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Move to proper place, according to the subpixel offset:
    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);

    // Do it!
    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(textureSrc2, texcoord));
}

//-----------------------------------------------------------------------------
// Corner Detection Functions

void SMAADetectHorizontalCornerPattern(SMAATexture2D(textureSrc), inout float2 weights, float4 texcoord, float2 d) {
    #if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(textureSrc, texcoord.xy, int2(0,  1)).r;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(textureSrc, texcoord.zw, int2(1,  1)).r;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(textureSrc, texcoord.xy, int2(0, -2)).r;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(textureSrc, texcoord.zw, int2(1, -2)).r;

    weights *= saturate(factor);
    #endif
}

void SMAADetectVerticalCornerPattern(SMAATexture2D(textureSrc), inout float2 weights, float4 texcoord, float2 d) {
    #if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(textureSrc, texcoord.xy, int2( 1, 0)).g;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(textureSrc, texcoord.zw, int2( 1, 1)).g;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(textureSrc, texcoord.xy, int2(-2, 0)).g;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(textureSrc, texcoord.zw, int2(-2, 1)).g;

    weights *= saturate(factor);
    #endif
}

//-----------------------------------------------------------------------------
// Blending Weight Calculation Pixel Shader (Second Pass)

float4 SMAABlendingWeightCalculationPS(float2 texcoord,
                                       float2 pixcoord,
                                       float4 offset[3],
                                       SMAATexture2D(textureSrc),
                                       SMAATexture2D(textureSrc2),
                                       SMAATexture2D(textureSrc3),
                                       float4 subsampleIndices) { // Just pass zero for SMAA 1x, see @SUBSAMPLE_INDICES.
    float4 weights = float4(0.0, 0.0, 0.0, 0.0);

    float2 e = SMAASample(textureSrc, texcoord).rg;

    SMAA_BRANCH
    if (e.g > 0.0) { // Edge at north
        #if !defined(SMAA_DISABLE_DIAG_DETECTION)
        // Diagonals have both north and west edges, so searching for them in
        // one of the boundaries is enough.
        weights.rg = SMAACalculateDiagWeights(SMAATexturePass2D(textureSrc), SMAATexturePass2D(textureSrc2), texcoord, e, subsampleIndices);

        // We give priority to diagonals, so if we find a diagonal we skip 
        // horizontal/vertical processing.
        SMAA_BRANCH
        if (weights.r == -weights.g) { // weights.r + weights.g == 0.0
        #endif

        float2 d;

        // Find the distance to the left:
        float3 coords;
        coords.x = SMAASearchXLeft(SMAATexturePass2D(textureSrc), SMAATexturePass2D(textureSrc3), offset[0].xy, offset[2].x);
        coords.y = offset[1].y; // offset[1].y = texcoord.y - 0.25 * rtMetrics.y (@CROSSING_OFFSET)
        d.x = coords.x;

        // Now fetch the left crossing edges, two at a time using bilinear
        // filtering. Sampling at -0.25 (see @CROSSING_OFFSET) enables to
        // discern what value each edge has:
        float e1 = SMAASampleLevelZero(textureSrc, coords.xy).r;

        // Find the distance to the right:
        coords.z = SMAASearchXRight(SMAATexturePass2D(textureSrc), SMAATexturePass2D(textureSrc3), offset[0].zw, offset[2].y);
        d.y = coords.z;

        // We want the distances to be in pixel units (doing this here allow to
        // better interleave arithmetic and memory accesses):
        d = abs(round(mad(rtMetrics.zz, d, -pixcoord.xx)));

        // SMAAArea below needs a sqrt, as the areas texture is compressed
        // quadratically:
        float2 sqrt_d = sqrt(d);

        // Fetch the right crossing edges:
        float e2 = SMAASampleLevelZeroOffset(textureSrc, coords.zy, int2(1, 0)).r;

        // Ok, we know how this pattern looks like, now it is time for getting
        // the actual area:
        weights.rg = SMAAArea(SMAATexturePass2D(textureSrc2), sqrt_d, e1, e2, subsampleIndices.y);

        // Fix corners:
        coords.y = texcoord.y;
        SMAADetectHorizontalCornerPattern(SMAATexturePass2D(textureSrc), weights.rg, coords.xyzy, d);

        #if !defined(SMAA_DISABLE_DIAG_DETECTION)
        } else
            e.r = 0.0; // Skip vertical processing.
        #endif
    }

    SMAA_BRANCH
    if (e.r > 0.0) { // Edge at west
        float2 d;

        // Find the distance to the top:
        float3 coords;
        coords.y = SMAASearchYUp(SMAATexturePass2D(textureSrc), SMAATexturePass2D(textureSrc3), offset[1].xy, offset[2].z);
        coords.x = offset[0].x; // offset[1].x = texcoord.x - 0.25 * rtMetrics.x;
        d.x = coords.y;

        // Fetch the top crossing edges:
        float e1 = SMAASampleLevelZero(textureSrc, coords.xy).g;

        // Find the distance to the bottom:
        coords.z = SMAASearchYDown(SMAATexturePass2D(textureSrc), SMAATexturePass2D(textureSrc3), offset[1].zw, offset[2].w);
        d.y = coords.z;

        // We want the distances to be in pixel units:
        d = abs(round(mad(rtMetrics.ww, d, -pixcoord.yy)));

        // SMAAArea below needs a sqrt, as the areas texture is compressed 
        // quadratically:
        float2 sqrt_d = sqrt(d);

        // Fetch the bottom crossing edges:
        float e2 = SMAASampleLevelZeroOffset(textureSrc, coords.xz, int2(0, 1)).g;

        // Get the area for this direction:
        weights.ba = SMAAArea(SMAATexturePass2D(textureSrc2), sqrt_d, e1, e2, subsampleIndices.x);

        // Fix corners:
        coords.x = texcoord.x;
        SMAADetectVerticalCornerPattern(SMAATexturePass2D(textureSrc), weights.ba, coords.xyxz, d);
    }

    return weights;
}

void outputShader()
{
	rtMetrics = vec4(1.0 / outputResolution, outputResolution);
	vec2 texcoord = passUV;
	vec2 pixcoord = texcoord * rtMetrics.zw;

	vec4 offsetArr[3];
	offsetArr[0] = mad(rtMetrics.xyxy, vec4(-0.25, -0.125,  1.25, -0.125), texcoord.xyxy);
	offsetArr[1] = mad(rtMetrics.xyxy, vec4(-0.125, -0.25, -0.125,  1.25), texcoord.xyxy);
	offsetArr[2] = mad(rtMetrics.xxyy,
	                vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
	                vec4(offsetArr[0].xz, offsetArr[1].yw));

	colorOut0 = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offsetArr, textureSrc, textureSrc2, textureSrc3, vec4(0.0));
}
)";

const std::string RendererOutputShader::s_smaa_neighborhood_shader_source =
R"(
// SMAA - Pass 3 of 3: neighborhood blending (final pass). Near-verbatim port
// of SMAANeighborhoodBlendingPS from https://github.com/iryoku/smaa.
// textureSrc = the FSR1-output intermediate texture (the same one pass 1
// read - sampled again here, unmodified by passes 1/2). textureSrc2 = the
// blend weights from pass 2. This is the only SMAA pass that writes to the
// real backbuffer/swapchain, so - unlike passes 1 and 2, whose gamma is
// neutralized on the C++ side since they produce data textures, not display
// colors - this pass gets the real display gamma correction applied by the
// shared fragment preamble after outputShader() returns.
// Declared here (not in the shared PrependFragmentPreamble) since this pass
// and the blend-calc pass are the only ones that sample textureSrc2 - see
// m_smaaNeighborhoodDescriptorSet in EnsureSmaaIntermediateTargets for the
// matching descriptor write. This pass never reads textureSrc3.
layout(binding = 2) uniform sampler2D textureSrc2;

#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASample(tex, coord) texture(tex, coord)
#define SMAASampleLevelZero(tex, coord) textureLod(tex, coord, 0.0)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define SMAA_REPROJECTION 0
#define mad(a, b, c) (a * b + c)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define bool2 bvec2
#define bool4 bvec4

vec4 rtMetrics;

/**
 * Conditional move:
 */
void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    SMAA_FLATTEN if (cond.x) variable.x = value.x;
    SMAA_FLATTEN if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

float4 SMAANeighborhoodBlendingPS(float2 texcoord,
                                  float4 offset,
                                  SMAATexture2D(textureSrc),
                                  SMAATexture2D(textureSrc2)
                                  #if SMAA_REPROJECTION
                                  , SMAATexture2D(velocityTex)
                                  #endif
                                  ) {
    // Fetch the blending weights for current pixel:
    float4 a;
    a.x = SMAASample(textureSrc2, offset.xy).a; // Right
    a.y = SMAASample(textureSrc2, offset.zw).g; // Top
    a.wz = SMAASample(textureSrc2, texcoord).xz; // Bottom / Left

    // Is there any blending weight with a value greater than 0.0?
    SMAA_BRANCH
    if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        float4 color = SMAASampleLevelZero(textureSrc, texcoord);

        #if SMAA_REPROJECTION
        float2 velocity = SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, texcoord));

        // Pack velocity into the alpha channel:
        color.a = sqrt(5.0 * length(velocity));
        #endif

        return color;
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

        // Calculate the blending offsets:
        float4 blendingOffset = float4(0.0, a.y, 0.0, a.w);
        float2 blendingWeight = a.yw;
        SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
        SMAAMovc(bool2(h, h), blendingWeight, a.xz);
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        // Calculate the texture coordinates:
        float4 blendingCoord = mad(blendingOffset, float4(rtMetrics.xy, -rtMetrics.xy), texcoord.xyxy);

        // We exploit bilinear filtering to mix current pixel with the chosen
        // neighbor:
        float4 color = blendingWeight.x * SMAASampleLevelZero(textureSrc, blendingCoord.xy);
        color += blendingWeight.y * SMAASampleLevelZero(textureSrc, blendingCoord.zw);

        #if SMAA_REPROJECTION
        // Antialias velocity for proper reprojection in a later stage:
        float2 velocity = blendingWeight.x * SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, blendingCoord.xy));
        velocity += blendingWeight.y * SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, blendingCoord.zw));

        // Pack velocity into the alpha channel:
        color.a = sqrt(5.0 * length(velocity));
        #endif

        return color;
    }
}

void outputShader()
{
	rtMetrics = vec4(1.0 / outputResolution, outputResolution);
	vec2 texcoord = passUV;
	vec4 offset = mad(rtMetrics.xyxy, vec4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy);

	colorOut0 = SMAANeighborhoodBlendingPS(texcoord, offset, textureSrc, textureSrc2);
}

)";

RendererOutputShader::RendererOutputShader(const std::string& vertex_source, const std::string& fragment_source)
{
    std::string finalFragmentSrc;
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		finalFragmentSrc = fragment_source;
		break;
#endif
	default:
		finalFragmentSrc = PrependFragmentPreamble(fragment_source);
		break;
	}

	m_vertex_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kVertex, 0, 0, vertex_source, false, false));
	m_fragment_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kFragment, 0, 0, finalFragmentSrc, false, false));

	m_vertex_shader->PreponeCompilation(true);
	m_fragment_shader->PreponeCompilation(true);

	if (!m_vertex_shader->WaitForCompiled())
		throw std::exception();

	if(!m_fragment_shader->WaitForCompiled())
		throw std::exception();

}

// Faro TAA - resolve pass. Runs BEFORE FSR1's EASU/RCAS, at the game's
// native/source resolution (see VulkanRenderer::DrawBackbufferQuadFsr1Taa's
// own doc comment for why - AMD's own FSR1 guidance says EASU expects an
// already-antialiased input, it's not an AA solution itself). textureSrc =
// current frame color (the game's own raw native-res frame, rendered with
// sub-pixel jitter baked into its vertex shaders - see SET_POSITION in
// LatteDecompilerEmitGLSLHeader.hpp). textureSrc2 = history (previous
// frame's resolved TAA output, same native resolution as textureSrc).
// Ported from Azahar's own TAA resolve (variance-clip in YCoCg +
// luma-difference anti-ghost, based on Intel's GameTechDev/TAA sample) -
// unlike Azahar, Cemu composites a single output image (no separate
// top/bottom screens sharing one history buffer), so passUV is already the
// correct, unambiguous coordinate for both textures - the coordinate-space
// bug that eventually got Azahar's own TAA removed doesn't apply to this
// architecture.
const std::string RendererOutputShader::s_taa_resolve_shader_source =
R"(
layout(binding = 2) uniform sampler2D textureSrc2;

float FaroLuma(vec3 c) {
	return dot(c, vec3(0.299, 0.587, 0.114));
}

// Intel GameTechDev/TAA: clipping in YCoCg (RGB clipping reddens edges).
vec3 RGB2YCoCg(vec3 c) {
	return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 YCoCg2RGB(vec3 c) {
	return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Karis 2014 ("High Quality Temporal Supersampling") AABB ray-clip: moves an
// out-of-box history sample along the ray from the box's own center through
// it, to exactly where that ray crosses the box surface - unlike a naive
// per-channel clamp (which independently clamps each of Y/Co/Cg and can
// therefore land on a color the box doesn't actually contain, e.g. a box
// corner that's nowhere near the real color distribution), this preserves
// hue/saturation relationships between channels and visibly reduces both
// ghosting and color-fringing at clipped pixels.
vec3 ClipAABB(vec3 boxMin, vec3 boxMax, vec3 historyColor) {
	vec3 boxCenter = 0.5 * (boxMax + boxMin);
	vec3 boxExtents = 0.5 * (boxMax - boxMin) + 1e-7; // epsilon avoids a divide-by-zero on a fully flat (zero-variance) box
	vec3 rayFromCenter = historyColor - boxCenter;
	vec3 unitSpace = rayFromCenter / boxExtents;
	vec3 absUnitSpace = abs(unitSpace);
	float maxUnit = max(absUnitSpace.x, max(absUnitSpace.y, absUnitSpace.z));
	if (maxUnit > 1.0)
		return boxCenter + rayFromCenter / maxUnit;
	return historyColor; // already inside the box - no clipping needed
}

// Intel GameTechDev/TAA BicubicSampling5: sharp history sample (plain bilinear
// would slowly blur the image over successive frames of accumulation).
vec3 SampleHistoryBicubic(vec2 uv) {
	vec2 texSize = textureSrcResolution; // history is native/source resolution, same as textureSrc
	vec2 st = uv * texSize - 0.5;
	vec2 f = fract(st);
	vec2 base = (floor(st) + 0.5) / texSize;
	vec2 rcp = 1.0 / texSize;
	vec2 t2 = f * f;
	vec2 t3 = t2 * f;
	float s = 0.5;
	vec2 w0 = -s * t3 + 2.0 * s * t2 - s * f;
	vec2 w1 = (2.0 - s) * t3 + (s - 3.0) * t2 + vec2(1.0);
	vec2 w2 = (s - 2.0) * t3 + (3.0 - 2.0 * s) * t2 + s * f;
	vec2 w3 = s * t3 - s * t2;
	vec2 s0 = w1 + w2;
	vec2 f0 = w2 / max(w1 + w2, vec2(1e-7));
	vec2 m0 = base + f0 * rcp;
	vec2 tc0 = base - rcp;
	vec2 tc3 = base + 2.0 * rcp;
	vec3 A = texture(textureSrc2, vec2(m0.x, tc0.y)).rgb;
	vec3 B = texture(textureSrc2, vec2(tc0.x, m0.y)).rgb;
	vec3 C = texture(textureSrc2, m0).rgb;
	vec3 D = texture(textureSrc2, vec2(tc3.x, m0.y)).rgb;
	vec3 E = texture(textureSrc2, vec2(m0.x, tc3.y)).rgb;
	return ((0.5 * (A + B) * w0.x + A * s0.x + 0.5 * (A + B) * w3.x) * w0.y +
	        (B * w0.x + C * s0.x + D * w3.x) * s0.y +
	        (0.5 * (B + E) * w0.x + E * s0.x + 0.5 * (D + E) * w3.x) * w3.y);
}

void outputShader() {
	vec2 uv = passUV;
	vec2 inv = 1.0 / textureSrcResolution;

	vec3 cM = texture(textureSrc, uv).rgb;
	if (!taaHistoryValid) {
		// First frame after boot/resize/reset - no history to blend with yet.
		colorOut0 = vec4(cM, 1.0);
		return;
	}

	vec3 cN = texture(textureSrc, uv + vec2(0.0, -inv.y)).rgb;
	vec3 cS = texture(textureSrc, uv + vec2(0.0, inv.y)).rgb;
	vec3 cE = texture(textureSrc, uv + vec2(inv.x, 0.0)).rgb;
	vec3 cW = texture(textureSrc, uv + vec2(-inv.x, 0.0)).rgb;
	vec3 cNE = texture(textureSrc, uv + vec2(inv.x, -inv.y)).rgb;
	vec3 cSE = texture(textureSrc, uv + vec2(inv.x, inv.y)).rgb;
	vec3 cNW = texture(textureSrc, uv + vec2(-inv.x, -inv.y)).rgb;
	vec3 cSW = texture(textureSrc, uv + vec2(-inv.x, inv.y)).rgb;

	// Variance clipping: mean +- gamma*sigma box in YCoCg (9 taps). Gamma
	// widens under motion (ghost > 0) to favor the sharper current frame.
	vec3 yM = RGB2YCoCg(cM), yN = RGB2YCoCg(cN), yS = RGB2YCoCg(cS);
	vec3 yE = RGB2YCoCg(cE), yW = RGB2YCoCg(cW), yNE = RGB2YCoCg(cNE);
	vec3 ySE = RGB2YCoCg(cSE), yNW = RGB2YCoCg(cNW), ySW = RGB2YCoCg(cSW);
	vec3 m1 = (yM + yN + yS + yE + yW + yNE + ySE + yNW + ySW) * (1.0 / 9.0);
	vec3 m2 = (yM * yM + yN * yN + yS * yS + yE * yE + yW * yW + yNE * yNE + ySE * ySE + yNW * yNW + ySW * ySW) * (1.0 / 9.0);

	vec3 hist = SampleHistoryBicubic(uv);
	vec3 vavg = (cM + cN + cS + cE + cW) * 0.2;
	float lHist = FaroLuma(hist);
	float lCur = FaroLuma(vavg);
	float lMin = min(FaroLuma(cM), min(min(FaroLuma(cN), FaroLuma(cS)), min(FaroLuma(cE), FaroLuma(cW))));
	float lMax = max(FaroLuma(cM), max(max(FaroLuma(cN), FaroLuma(cS)), max(FaroLuma(cE), FaroLuma(cW))));
	float lRange = max(0.02, lMax - lMin);
	float ghost = clamp(abs(lHist - lCur) / (lRange * 2.0), 0.0, 1.0);

	vec3 vbox = sqrt(max(m2 - m1 * m1, vec3(0.0))) * mix(2.0, 0.75, ghost);
	vec3 histClamped = YCoCg2RGB(ClipAABB(m1 - vbox, m1 + vbox, RGB2YCoCg(hist)));
	float blendCur = mix(0.10, 0.85, ghost * ghost);

	colorOut0 = vec4(mix(histClamped, cM, blendCur), 1.0);
}
)";


RendererOutputShader::OutputUniformVariables RendererOutputShader::FillUniformBlockBuffer(const LatteTextureView& texture_view, const Vector2i& output_res, const bool padView) const
{
	OutputUniformVariables vars;

	sint32 effectiveWidth, effectiveHeight;
	texture_view.baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, 0);
	vars.textureSrcResolution = {(float)effectiveWidth, (float)effectiveHeight};

	vars.nativeResolution = {(float)texture_view.baseTexture->width, (float)texture_view.baseTexture->height};
	vars.outputResolution = output_res;

	vars.applySRGBEncoding = padView ? LatteGPUState.drcBufferUsesSRGB : LatteGPUState.tvBufferUsesSRGB;
	vars.targetGamma = padView ? ActiveSettings::GetDRCGamma() : ActiveSettings::GetTVGamma();
	vars.displayGamma = GetConfig().userDisplayGamma;
	vars._padFaroGtao[0] = 0;
	vars._padFaroGtao[1] = 0;
	vars.faroGtaoParams[0] = 0.0f;
	vars.faroGtaoParams[1] = 0.0f;
	vars.faroGtaoParams[2] = 0.0f;
	vars.faroGtaoParams[3] = 0.0f;

	return vars;
}

RendererOutputShader* RendererOutputShader::s_copy_shader;
RendererOutputShader* RendererOutputShader::s_copy_shader_ud;

RendererOutputShader* RendererOutputShader::s_bicubic_shader;
RendererOutputShader* RendererOutputShader::s_bicubic_shader_ud;

RendererOutputShader* RendererOutputShader::s_hermit_shader;
RendererOutputShader* RendererOutputShader::s_hermit_shader_ud;

RendererOutputShader* RendererOutputShader::s_fsr1_easu_shader;
RendererOutputShader* RendererOutputShader::s_fsr1_easu_shader_ud;
RendererOutputShader* RendererOutputShader::s_fsr1_rcas_shader;

RendererOutputShader* RendererOutputShader::s_fxaa_shader;

RendererOutputShader* RendererOutputShader::s_smaa_edge_shader[RendererOutputShader::kSmaaQualityCount];
RendererOutputShader* RendererOutputShader::s_smaa_blend_shader[RendererOutputShader::kSmaaQualityCount];
RendererOutputShader* RendererOutputShader::s_smaa_neighborhood_shader;

RendererOutputShader* RendererOutputShader::s_taa_resolve_shader;

RendererOutputShader* RendererOutputShader::s_gtao_shader[RendererOutputShader::kGtaoQualityCount];
RendererOutputShader* RendererOutputShader::s_gi_shader;
RendererOutputShader* RendererOutputShader::s_gi_blur_shader;
RendererOutputShader* RendererOutputShader::s_ssr_flatness_shader;
RendererOutputShader* RendererOutputShader::s_ssr_shader;

std::string RendererOutputShader::GetOpenGlVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#version 420
layout(location = 0) smooth out vec2 passUV;

out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexID;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

		vertex_source <<
			R"(	passUV = vUV;
	gl_Position = vec4(vPos, 0.0, 1.0);
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::GetVulkanVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#version 450
layout(location = 0) out vec2 passUV;

out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexIndex;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

		vertex_source <<
			R"(	passUV = vUV;
	gl_Position = vec4(vPos, 0.0, 1.0);
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::GetMetalVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut main0(ushort vid [[vertex_id]]) {
	VertexOut out;
	float2 pos;
	if (vid == 0) pos = float2(-1.0, -3.0);
	else if (vid == 1) pos = float2(-1.0, 1.0);
	else if (vid == 2) pos = float2(3.0, 1.0);
	out.uv = pos * 0.5 + 0.5;
	out.uv.y = 1.0 - out.uv.y;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	pos.y = -pos.y;
	)";
		}

		vertex_source <<
			R"(	out.position = float4(pos, 0.0, 1.0);
	return out;
}
)";
		return vertex_source.str();
}

// smaaQuality is CemuConfig::SmaaQuality's underlying int (0=kSmaaLow,
// 1=kSmaaMedium, 2=kSmaaHigh, 3=kSmaaUltra) - not including CemuConfig.h here
// to avoid coupling the shader-source layer to the config layer; the caller
// (VulkanRenderer::InitializeStatic) already has GetConfig() in scope.
std::string RendererOutputShader::BuildSmaaEdgeShaderSource(sint32 smaaQuality)
{
	// Matches the public reference's own SMAA_PRESET_LOW/MEDIUM/HIGH/ULTRA
	// SMAA_THRESHOLD values exactly (see SMAA.hlsl's "Presets" section).
	float threshold;
	switch (smaaQuality)
	{
	case 0: threshold = 0.15f; break; // kSmaaLow
	case 1: threshold = 0.1f; break;  // kSmaaMedium
	case 3: threshold = 0.05f; break; // kSmaaUltra
	case 2: default: threshold = 0.1f; break; // kSmaaHigh
	}
	// std::to_string() uses the current C locale for the decimal point, which
	// breaks this on any system with a comma-decimal locale (e.g. Spanish) -
	// "0,15" instead of "0.15" makes SMAA_THRESHOLD expand into 2 tokens,
	// turning float2(SMAA_THRESHOLD, SMAA_THRESHOLD) into an invalid 4-arg
	// call. fmt::format is locale-independent and always uses '.'.
	return fmt::format("#define SMAA_THRESHOLD {}\n", threshold) + s_smaa_edge_shader_body;
}

// Faro GTAO: horizon-based ambient occlusion (Jimenez et al. 2016 technique
// family, clean-room GLSL core - not a copy of any vendor sample).
// Reads textureSrc (upscaled color, binding 0 via preamble) + textureDepth
// (native scene depth, same UVs, binding 2 declared here like SMAA does for
// its own extra samplers). Writes AO-multiplied color; downstream passes
// (FXAA/SMAA/TAA) run unchanged on the result.
// View-space reconstruction assumes 60-degree vertical FOV (ProjectionFovY
// below) - radius absorbs the error, same tradeoff ReShade SSAO shaders make
// with their radius sliders.
static const std::string s_gtao_shader_body = R"(
// textureDepth must be linear-filtered, clamped (see Vulkan descriptor write).
layout(binding = 2) uniform sampler2D textureDepth;

float GtaoLinearize(float d)
{
	// N=1.0 fixed (ReShade GetLinearizedDepth convention), FAR from uniform.
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

float GtaoHash(vec2 p)
{
	// Interleaved gradient noise - rotates the sample kernel per pixel, no texture needed.
	vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(m.z * fract(dot(p, m.xy)));
}

void outputShader()
{
	vec3 color = texture(textureSrc, passUV).xyz;
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	if (intensity <= 0.0)
	{
		colorOut0 = vec4(color, 1.0);
		return;
	}
	// faroGtaoParams.y: dimensionless RELATIVE depth-difference threshold
	// (fraction of the center pixel's own linearized depth) - NOT an
	// absolute view-space unit. Debugged extensively (3 rounds of false-
	// color visualization): a raw-depth dump confirmed the depth texture
	// carries real per-pixel scene data (character/foliage/horizon all
	// visible), yet occ stayed ~0 everywhere with the old absolute-unit
	// threshold - because GtaoLinearize's near-field output changes only
	// ~1:1 with the raw [0,1] depth value (see its own derivative near
	// d=0), so genuine near-field depth deltas are tiny compared to any
	// plausible absolute constant when there's no real camera far plane to
	// calibrate against (see FSR2_TAA_investigacion.md). A RELATIVE
	// threshold sidesteps that entirely - it's scale-invariant to whatever
	// units GtaoLinearize happens to produce.
	float relThreshold = max(faroGtaoParams.y, 1e-4);
	vec2 outRes = outputResolution;
	// clamp() alone does not reliably sanitize NaN on all GPUs/drivers (IEEE
	// min/max with a NaN operand is implementation-defined, not guaranteed
	// to fall back to the non-NaN operand) - min() then max() in this
	// specific order is the standard safe idiom, since it forces the NaN
	// through a comparison against a known-good value each step. Confirmed
	// necessary via testing: the AO computation running in full but its
	// result being discarded showed a clean image, while actually applying
	// the (uninspected) result to color produced severe per-channel color
	// corruption - the signature of a NaN reaching colorOut0 and each color
	// channel's UNORM conversion handling that bit pattern differently.
	float depth = min(max(textureLod(textureDepth, passUV, 0.5).x, 0.0), 1.0);
	float viewZ = GtaoLinearize(depth);
	// faroGtaoParams.z: fixed screen-space march radius in pixels (a
	// pseudo-view-space reconstruction used to derive this from an
	// absolute radius previously - removed along with the absolute
	// threshold above, same root cause).
	float marchPx = max(faroGtaoParams.z, 1.0);
	float rot = GtaoHash(gl_FragCoord.xy) * 3.14159265;
	float occ = 0.0;
	float wsum = 0.0;
	// relScale is viewZ-relative, so it naturally grows with distance -
	// same absolute march radius in pixels then corresponds to a
	// proportionally larger depth range at greater depths, matching how
	// perspective actually foreshortens a fixed real-world AO radius.
	float relScale = max(viewZ * relThreshold, 1e-5);
	for (int d = 0; d < FARO_GTAO_DIRS; d++)
	{
		float ang = rot + float(d) * 3.14159265 / float(FARO_GTAO_DIRS);
		vec2 dirPx = vec2(cos(ang), sin(ang));
		for (int s = 1; s <= FARO_GTAO_STEPS; s++)
		{
			float t = (float(s) - 0.5) / float(FARO_GTAO_STEPS);
			vec2 suv = passUV + dirPx * (t * marchPx) / outRes;
			float sz = GtaoLinearize(min(max(textureLod(textureDepth, suv, 0.5).x, 0.0), 1.0));
			// Bilateral weight: reject samples across a true depth
			// discontinuity (character/object silhouette), using a wider
			// window than the occlusion threshold itself so genuine
			// creases still count fully.
			float w = clamp(1.0 - abs(sz - viewZ) / (relScale * 3.0), 0.0, 1.0);
			occ += clamp((viewZ - sz) / relScale, 0.0, 1.0) * w;
			wsum += w;
		}
	}
	// Same min()-then-max() NaN-safe idiom as depth/sz above - occ/wsum are
	// each individually finite by construction, but ao's own clamp() is the
	// last line of defense before this reaches colorOut0.
	float ao = 1.0 - min(max(occ / max(wsum, 1e-4), 0.0), 1.0);
	// Distance fade: non-linearized depth's precision collapses far from
	// the camera (see GtaoLinearize's own comment - most of the raw [0,1]
	// depth range is spent on nearby geometry), so at long range the
	// "occlusion" this technique detects is dominated by depth-quantization
	// noise rather than real creases - confirmed via testing: distant
	// buildings/terrain showed clearly incorrect darkening despite nothing
	// actually being occluded there. Fades intensity to 0 well before the
	// (already made-up, see faroGtaoParams.w's own comment) far plane,
	// since only near/mid-range creases are trustworthy with this
	// technique - same "only near field is reliable" reasoning used
	// throughout this file's depth-based heuristics.
	float distFade = 1.0 - smoothstep(30.0, 120.0, viewZ);
	colorOut0 = vec4(color * mix(1.0, ao, intensity * distFade), 1.0);
}
)";

std::string RendererOutputShader::BuildGtaoShaderSource(sint32 gtaoQuality)
{
	// Sample counts per quality preset (dirs x steps). No float defines, so
	// the fmt locale pitfall from BuildSmaaEdgeShaderSource doesn't apply.
	int dirs, steps;
	switch (gtaoQuality)
	{
	case 0: dirs = 6; steps = 6; break; // Low - closer to ReShade
	case 2: dirs = 12; steps = 12; break; // High - closer to ReShade
	case 1: // Medium
	default: dirs = 8; steps = 8; break;
	}
	return fmt::format("#define FARO_GTAO_DIRS {}\n#define FARO_GTAO_STEPS {}\n", dirs, steps) + s_gtao_shader_body;
}

// Faro GI: reuses GTAO's exact directional/step depth-sampling kernel (same
// NaN-safe depth clamping, relative threshold, distance fade - see GTAO's
// own comments for why each of those exists) but accumulates a weighted
// average of nearby color instead of computing occlusion. Only samples that
// are CLOSER to the camera than the center pixel count (same directionality
// as GTAO's own occ term - a nearer surface could plausibly bounce light
// onto this one), weighted the same bilateral way to avoid bleeding across
// true depth discontinuities. No G-buffer normals available to this
// codebase (see FSR2_TAA_investigacion.md), so this is a crude screen-space
// color-bleed approximation, not physically based GI. Single fixed sample
// count (no quality tiers, unlike GTAO) - GI is already markedly more
// expensive per-sample (a vec3 texture fetch/accumulate vs GTAO's scalar
// depth compare) so this keeps the first version's GPU cost bounded.
// DISABLED - see wantGi in VulkanRenderer.cpp for why this is hard-gated
// off rather than invoked.
static const std::string s_gi_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;

float GiLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

float GiHash(vec2 p)
{
	vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(m.z * fract(dot(p, m.xy)));
}

// Reconstructs a pseudo-view-space position from linearized depth, same
// 60-degree-vertical-FOV assumption already used throughout this file's
// depth-only heuristics (no real camera projection matrix available - see
// FSR2_TAA_investigacion.md). Only used here (unlike GTAO/AO, which never
// needed real positions, just relative depth) because deriving a surface
// normal via screen-space derivatives genuinely requires reconstructing
// actual 3D-ish positions first.
vec3 GiReconstructPos(vec2 uv, float viewZ, float aspect)
{
	float tanFov = 0.57735027;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * tanFov * aspect, ndc.y * tanFov, 1.0) * viewZ;
}

// Neighboring-depth-tap normal reconstruction - central differences (ALWAYS
// both sides, never a per-pixel side pick) - same fix already applied to
// SSR's own SsrReconstructNormal (see that function's doc comment for the
// full history): the old smallest-difference-pick version flipped which
// side it used pixel-to-pixel wherever both sides were nearly equidistant -
// exactly the flat-terrain/flat-water case - producing a discontinuous
// per-pixel normal. Central differences smear at most 1px at true
// silhouettes, which is strictly better than a per-pixel flip-flopping
// normal.
vec3 GiReconstructNormal(vec2 uv, vec3 centerPos, float viewZ, float aspect, vec2 texelSize)
{
	float dL = GiLinearize(min(max(texture(textureDepth, uv - vec2(texelSize.x, 0.0)).x, 0.0), 1.0));
	float dR = GiLinearize(min(max(texture(textureDepth, uv + vec2(texelSize.x, 0.0)).x, 0.0), 1.0));
	float dD = GiLinearize(min(max(texture(textureDepth, uv - vec2(0.0, texelSize.y)).x, 0.0), 1.0));
	float dU = GiLinearize(min(max(texture(textureDepth, uv + vec2(0.0, texelSize.y)).x, 0.0), 1.0));

	vec3 posL = GiReconstructPos(uv - vec2(texelSize.x, 0.0), dL, aspect);
	vec3 posR = GiReconstructPos(uv + vec2(texelSize.x, 0.0), dR, aspect);
	vec3 posD = GiReconstructPos(uv - vec2(0.0, texelSize.y), dD, aspect);
	vec3 posU = GiReconstructPos(uv + vec2(0.0, texelSize.y), dU, aspect);

	vec3 ddx = posR - posL;
	vec3 ddy = posU - posD;
	return normalize(cross(ddx, ddy));
}

void outputShader()
{
	// Writes ONLY the raw bounce-color term (no scene color mixed in) -
	// the blur pass that follows this one composites it onto the ORIGINAL
	// sharp color afterward.
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	if (intensity <= 0.0)
	{
		colorOut0 = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	float relThreshold = max(faroGtaoParams.y, 1e-4);
	vec2 outRes = outputResolution;
	float aspect = outRes.x / max(outRes.y, 1.0);
	float depth = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	float viewZ = GiLinearize(depth);
	vec3 centerPos = GiReconstructPos(passUV, viewZ, aspect);
	vec3 normal = GiReconstructNormal(passUV, centerPos, viewZ, aspect, 1.0 / outRes);
	float marchPx = max(faroGtaoParams.z, 1.0);
	float rot = GiHash(gl_FragCoord.xy) * 3.14159265;
	vec3 bounce = vec3(0.0);
	float wsum = 0.0;
	float relScale = max(viewZ * relThreshold, 1e-5);
	const int FARO_GI_DIRS = 6;
	const int FARO_GI_STEPS = 8;
	for (int d = 0; d < FARO_GI_DIRS; d++)
	{
		float ang = rot + float(d) * 3.14159265 / float(FARO_GI_DIRS);
		vec2 dirPx = vec2(cos(ang), sin(ang));
		// Running horizon (Bavoil/HBAO 2008, Jimenez GTAO, Mayaux HBIL
		// 2018, and the "visibility bitmask" paper - Cordonnier et al.
		// 2023 - all share this core idea): track the highest "elevation
		// above the surface's own tangent plane" seen so far along this
		// ray. A sample only contributes light if it RAISES this running
		// horizon, i.e. it is not already hidden behind a nearer, steeper
		// sample this same ray already counted - avoids double-counting
		// light from stacked occluders.
		float maxSin = 0.0;
		for (int s = 1; s <= FARO_GI_STEPS; s++)
		{
			float t = (float(s) - 0.5) / float(FARO_GI_STEPS);
			vec2 suv = passUV + dirPx * (t * marchPx) / outRes;
			float sd = min(max(texture(textureDepth, suv).x, 0.0), 1.0);
			float sz = GiLinearize(sd);
			if (sz >= viewZ)
				continue;
			vec3 samplePos = GiReconstructPos(suv, sz, aspect);
			vec3 toSample = samplePos - centerPos;
			float dist = length(toSample);
			if (dist < 1e-4)
				continue;
			float sinAngle = dot(toSample / dist, normal);
			// Soft horizon rise instead of a hard cutoff - avoids a
			// coherent grid-shaped banding artifact confirmed via user
			// testing on flat terrain/water.
			const float FARO_GI_HORIZON_SOFTNESS = 0.12;
			float rise = sinAngle - maxSin;
			float riseWeight = smoothstep(0.0, FARO_GI_HORIZON_SOFTNESS, rise);
			if (riseWeight <= 0.0)
				continue;
			float w = clamp(1.0 - abs(sz - viewZ) / (relScale * 3.0), 0.0, 1.0);
			float delta = rise * riseWeight * w;
			bounce += texture(textureSrc, suv).rgb * delta;
			wsum += delta;
			maxSin = max(maxSin, sinAngle);
		}
	}
	vec3 giColor = wsum > 1e-4 ? bounce / wsum : vec3(0.0);
	colorOut0 = vec4(giColor, 1.0);
}
)";

std::string RendererOutputShader::BuildGiShaderSource()
{
	return s_gi_shader_body;
}

// Faro GI denoise: the directional/step kernel's per-pixel random rotation
// (see s_gi_shader_body's own comment) is noisy by design - depth-aware
// (bilateral) box blur over textureSrc=GI's own raw output, so it smooths
// within a surface without bleeding across true depth discontinuities.
static const std::string s_gi_blur_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;
layout(binding = 3) uniform sampler2D textureOriginal;

float GiBlurLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

void outputShader()
{
	vec2 outRes = outputResolution;
	float centerDepth = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	float centerZ = GiBlurLinearize(centerDepth);
	float relThreshold = max(faroGtaoParams.y, 1e-4);
	float relScale = max(centerZ * relThreshold, 1e-5);
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	const int R = 3;
	for (int y = -R; y <= R; y++)
	{
		for (int x = -R; x <= R; x++)
		{
			vec2 suv = passUV + vec2(float(x), float(y)) / outRes;
			float sd = min(max(texture(textureDepth, suv).x, 0.0), 1.0);
			float sz = GiBlurLinearize(sd);
			float w = clamp(1.0 - abs(sz - centerZ) / (relScale * 3.0), 0.0, 1.0);
			sum += texture(textureSrc, suv).rgb * w;
			wsum += w;
		}
	}
	vec3 blurredGi = wsum > 1e-4 ? sum / wsum : texture(textureSrc, passUV).rgb;
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	float distFade = 1.0 - smoothstep(30.0, 120.0, centerZ);
	vec3 original = texture(textureOriginal, passUV).rgb;
	vec3 result = original + blurredGi * intensity * distFade * 0.6;
	colorOut0 = vec4(min(max(result, vec3(0.0)), vec3(1.0)), 1.0);
}
)";

std::string RendererOutputShader::BuildGiBlurShaderSource()
{
	return s_gi_blur_shader_body;
}

// Faro SSR: temporal pre-pass for the reflective-surface gate - see
// s_ssr_flatness_shader's own doc comment in the header for the full
// rationale. faroGtaoParams here means: x=history blend weight (0..1),
// y=far plane (same depth-linearization meaning as everywhere else, kept
// identical to the main SSR shader's own faroGtaoParams.w so the curvature
// scale this produces matches what the main pass used to compute raw at the
// same pixel), z=1.0 once the history target actually holds a previous
// frame's result (0.0 only on the very first use right after
// EnsureSsrFlatnessHistoryTarget (re)creates it - see that method's own
// comment), w=unused.
static const std::string s_ssr_flatness_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;
// Faro SSR: previous frame's own blended flatness, sampled at the same
// screen UV - no reprojection/motion vectors, same accepted simplification
// as Faro TAA's own history blend (see s_taa_resolve_shader_body's doc
// comment on why "same pixel, no reprojection" is good enough here).
layout(binding = 3) uniform sampler2D textureFlatnessHistory;

float SsrFlatnessLinearize(float d)
{
	float farPlane = max(faroGtaoParams.y, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

void outputShader()
{
	vec2 outRes = outputResolution;
	float depthCenter = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	// Same HUD-exclusion + depth-prefilter reasoning as the main SSR
	// shader's own outputShader (see its comments) - kept identical here so
	// the smoothed flatness this produces matches what the main pass would
	// have computed raw at the same pixel.
	if (depthCenter < 0.0005 || depthCenter > 0.99)
	{
		colorOut0 = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec2 ptex = 1.0 / outRes;
	float dsum = depthCenter;
	dsum += min(max(texture(textureDepth, passUV + vec2(ptex.x, 0.0)).x, 0.0), 1.0);
	dsum += min(max(texture(textureDepth, passUV - vec2(ptex.x, 0.0)).x, 0.0), 1.0);
	dsum += min(max(texture(textureDepth, passUV + vec2(0.0, ptex.y)).x, 0.0), 1.0);
	dsum += min(max(texture(textureDepth, passUV - vec2(0.0, ptex.y)).x, 0.0), 1.0);
	float depth = dsum * 0.2;
	float viewZ = SsrFlatnessLinearize(depth);

	// Curvature taps use a WIDER offset than the depthCenter prefilter above
	// (4 texels instead of 1) - real animated water (BOTW's own wave/ripple
	// vertex displacement) genuinely varies the depth buffer at small,
	// ripple-scale distances, which a tight 1-texel Laplacian reads as
	// "not flat", scoring rippling water as LESS reflective than a
	// perfectly static dirt/grass field right next to it (confirmed via
	// user testing: the floor visibly out-reflected the water in front of
	// it). Widening the tap distance low-pass-filters away that small-
	// scale ripple curvature while still catching genuine terrain slopes/
	// steps, which happen over a much larger screen-space distance than a
	// single wave crest.
	const float FARO_SSR_FLATNESS_TAP_SCALE = 4.0;
	vec2 ctex = ptex * FARO_SSR_FLATNESS_TAP_SCALE;
	float dL = SsrFlatnessLinearize(min(max(texture(textureDepth, passUV - vec2(ctex.x, 0.0)).x, 0.0), 1.0));
	float dR = SsrFlatnessLinearize(min(max(texture(textureDepth, passUV + vec2(ctex.x, 0.0)).x, 0.0), 1.0));
	float dD = SsrFlatnessLinearize(min(max(texture(textureDepth, passUV - vec2(0.0, ctex.y)).x, 0.0), 1.0));
	float dU = SsrFlatnessLinearize(min(max(texture(textureDepth, passUV + vec2(0.0, ctex.y)).x, 0.0), 1.0));
	float curvature = (abs(dL + dR - 2.0 * viewZ) + abs(dU + dD - 2.0 * viewZ)) / max(viewZ, 1.0);
	// Upper bound loosened from 0.12 to 0.20 - even with the wider tap
	// radius above, water with pronounced waves/chop can still carry more
	// residual curvature than a perfectly static field, so this gives it
	// more room to still register as "flat enough" before being excluded
	// entirely. The lower bound (0.01, full flatness below this) is
	// unchanged - genuinely calm water/glass still scores 1.0 either way.
	float rawFlatness = 1.0 - smoothstep(0.01, 0.20, curvature);

	// Faro SSR: exponential moving average against last frame's own blended
	// result - smooths out the frame-to-frame "pop" where curvature crosses
	// the smoothstep band on animated surfaces (rippling water) or at
	// grazing angles. Confirmed via user testing as reflections still
	// "apareciendo y desapareciendo" in quick bursts even after the main SSR
	// shader's own spatial-only stabilization (depth prefilter, central-
	// difference normals, soft fades) and the shared GTAO/GI/SSR
	// depth-capture hysteresis fix (see Renderer::CaptureDepthForPostFX's
	// own comment) - those all reduce NOISE within a single frame, but
	// nothing before this persisted anything ACROSS frames, so a value
	// oscillating right at the smoothstep boundary still popped every time
	// it crossed. This is the first real cross-frame memory SSR has.
	//
	// Temporal-variance water detector: the curvature test above (widened
	// tap radius + loosened threshold - see their own comments) still
	// fundamentally can't tell "genuinely bumpy, non-reflective terrain"
	// apart from "animated rippling water" using only ONE frame's depth -
	// both can carry similar instantaneous curvature. The one thing that
	// actually differs between them is TIME: real waves change shape
	// frame to frame, so a water pixel's own curvature value keeps
	// moving; static rock/dirt's curvature stays put no matter how bumpy
	// it looks in any single frame. G/B of this same history target
	// (previously always written as 0.0, unused) now carry a running EMA
	// of curvature (meanCurvature) and a running EMA of how far each new
	// frame's raw curvature strays from that mean (deviation) - a cheap
	// mean-absolute-deviation stand-in for variance, using the exact same
	// history buffer/blend weight the flatness value itself already uses,
	// so this needed zero new Vulkan resources (just channels of the
	// scratch+history pair that already existed and were sitting unused).
	// A pixel with high deviation gets its flatness rescued even if its
	// instantaneous curvature says "not flat" - that instability IS the
	// signal that it's moving water, not bumpy ground.
	vec3 history = texture(textureFlatnessHistory, passUV).xyz;
	float historyFlatness = history.x;
	float historyMeanCurvature = history.y;
	float historyDeviation = history.z;
	float historyWeight = faroGtaoParams.x * faroGtaoParams.z;
	bool historyIsValid = faroGtaoParams.z > 0.5;
	float meanCurvature = mix(curvature, historyMeanCurvature, historyWeight);
	float deviation = historyIsValid ? mix(abs(curvature - historyMeanCurvature), historyDeviation, historyWeight) : 0.0;
	float motionBoost = smoothstep(0.01, 0.05, deviation);
	float rawFlatnessBoosted = max(rawFlatness, motionBoost);
	float blended = mix(rawFlatnessBoosted, historyFlatness, historyWeight);
	colorOut0 = vec4(blended, meanCurvature, deviation, 1.0);
}
)";

std::string RendererOutputShader::BuildSsrFlatnessShaderSource()
{
	return s_ssr_flatness_shader_body;
}

// Faro SSR: see s_ssr_shader's own doc comment in the header for the full
// rationale (reuses GI's depth-derived-normal/assumed-FOV substitutes for
// the camera projection matrix + G-buffer normals real SSR needs).
// faroGtaoParams here means: x=intensity, y=march step size (view-units,
// NOT a relative depth threshold like GTAO/GI use this same slot for -
// SSR genuinely needs an absolute march distance, no way around it without
// real camera units), w=far plane (same shared meaning as everywhere else).
static const std::string s_ssr_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;
// Faro SSR: temporally-smoothed reflective-surface gate, computed by
// s_ssr_flatness_shader as its own tiny pre-pass (see
// VulkanRenderer::RunFaroSsrPass) - see that shader's own comment for why
// this moved out of this shader instead of staying an inline, instantaneous
// computation.
layout(binding = 3) uniform sampler2D textureFlatness;

float SsrLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

vec3 SsrReconstructPos(vec2 uv, float viewZ, float aspect, float tanFov)
{
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * tanFov * aspect, ndc.y * tanFov, 1.0) * viewZ;
}

// Inverse of SsrReconstructPos - projects a pseudo-view-space point back to
// screen UV under the same assumed-FOV pseudo-projection.
vec2 SsrProjectToUV(vec3 P, float aspect, float tanFov)
{
	vec2 ndc = vec2(P.x / (max(P.z, 1e-4) * tanFov * aspect), P.y / (max(P.z, 1e-4) * tanFov));
	return ndc * 0.5 + 0.5;
}

// Neighboring-depth-tap normal reconstruction - replaces an earlier
// dFdx/dFdy version that produced hard per-triangle-facet discontinuities
// in the reflected image ("cuts", confirmed via user testing with SSR
// intensity at maximum to make them clearly visible). Reflection
// direction is very sensitive to normal noise, so this mattered a lot
// here. The curvature-based reflective-surface gate this used to also compute
// (the discrete Laplacian of the depth field - near-zero for a locally
// PLANAR surface, large for bumpy/organic geometry) now lives in
// s_ssr_flatness_shader's own pre-pass instead, so it can be temporally
// smoothed against last frame's result - see that shader's own doc comment.
vec3 SsrReconstructNormal(vec2 uv, vec3 centerPos, float viewZ, float aspect, float tanFov, vec2 texelSize)
{
	float dL = SsrLinearize(min(max(texture(textureDepth, uv - vec2(texelSize.x, 0.0)).x, 0.0), 1.0));
	float dR = SsrLinearize(min(max(texture(textureDepth, uv + vec2(texelSize.x, 0.0)).x, 0.0), 1.0));
	float dD = SsrLinearize(min(max(texture(textureDepth, uv - vec2(0.0, texelSize.y)).x, 0.0), 1.0));
	float dU = SsrLinearize(min(max(texture(textureDepth, uv + vec2(0.0, texelSize.y)).x, 0.0), 1.0));

	vec3 posL = SsrReconstructPos(uv - vec2(texelSize.x, 0.0), dL, aspect, tanFov);
	vec3 posR = SsrReconstructPos(uv + vec2(texelSize.x, 0.0), dR, aspect, tanFov);
	vec3 posD = SsrReconstructPos(uv - vec2(0.0, texelSize.y), dD, aspect, tanFov);
	vec3 posU = SsrReconstructPos(uv + vec2(0.0, texelSize.y), dU, aspect, tanFov);

	// Central differences (ALWAYS both sides, never a per-pixel side pick) -
	// the old neighbor-pick version flipped sides pixel-to-pixel wherever
	// both sides were nearly equidistant (exactly the flat-water case),
	// producing discontinuous reflection directions = visible "cuts" in
	// the water that shifted with every tiny camera move. Central
	// differences smear at most 1px at true silhouettes, and those pixels
	// already fail the curvature gate below, so this is strictly smoother.
	vec3 ddx = posR - posL;
	vec3 ddy = posU - posD;
	return normalize(cross(ddx, ddy));
}

// Interleaved gradient noise hash - used to dither the ray march's
// starting offset per pixel, see outputShader's own comment on why.
float SsrHash(vec2 p)
{
	vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(m.z * fract(dot(p, m.xy)));
}

void outputShader()
{
	vec3 color = texture(textureSrc, passUV).xyz;
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	if (intensity <= 0.0)
	{
		colorOut0 = vec4(color, 1.0);
		return;
	}
	vec2 outRes = outputResolution;
	float aspect = outRes.x / max(outRes.y, 1.0);
	float tanFov = 0.57735027;
	float depthCenter = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	// HUD exclusion: only the literal near-plane/unwritten-depth extreme is
	// excluded now (0.01 was confirmed via user testing to misfire on real,
	// legitimately close geometry - e.g. water right under the camera when
	// tilting down to look at it from above: standard perspective z-buffers
	// compress enormous precision near the camera, so genuinely nearby
	// surfaces can already read well under 0.01 without being anywhere near
	// the true near clip plane. There is no reliable way to distinguish
	// "real close geometry" from "HUD" using depth alone if the HUD doesn't
	// write its own depth - confirmed via research into ReShade's own
	// long-standing HUD-exclusion-via-depth limitation - so this only
	// catches the genuinely-degenerate case now, not anything merely close).
	if (depthCenter < 0.0005 || depthCenter > 0.99)
	{
		colorOut0 = vec4(color, 1.0);
		return;
	}
	// 3x3 depth prefilter for the reflector pixel: with TAA frame-jitter
	// (and animated water) the raw center depth shimmers sub-texel frame
	// to frame; every downstream term (viewZ, normal, march, gates) then
	// flips between hit and miss = reflections blinking while standing
	// still. Averaging the local neighborhood keeps a stable estimate so
	// small input wobble moves strength gradually instead of toggling it.
	float depth = depthCenter;
	{
		vec2 ptex = 1.0 / outRes;
		float dsum = depthCenter;
		dsum += min(max(texture(textureDepth, passUV + vec2(ptex.x, 0.0)).x, 0.0), 1.0);
		dsum += min(max(texture(textureDepth, passUV - vec2(ptex.x, 0.0)).x, 0.0), 1.0);
		dsum += min(max(texture(textureDepth, passUV + vec2(0.0, ptex.y)).x, 0.0), 1.0);
		dsum += min(max(texture(textureDepth, passUV - vec2(0.0, ptex.y)).x, 0.0), 1.0);
		depth = dsum * 0.2; // 5-tap cross (cheaper than 3x3, same stabilizing effect)
	}
	float viewZ = SsrLinearize(depth);
	vec3 centerPos = SsrReconstructPos(passUV, viewZ, aspect, tanFov);
	vec3 normal = SsrReconstructNormal(passUV, centerPos, viewZ, aspect, tanFov, 1.0 / outRes);
	// Reflective-surface gate - temporally-smoothed by s_ssr_flatness_shader's
	// own pre-pass instead of recomputed raw here (see textureFlatness's own
	// declaration comment above and that shader's doc comment for the full
	// rationale). Confirmed via research (every real SSR reference - Unity
	// HDRP, Babylon.js, McGuire/Mara - gates reflections by a roughness/
	// material mask; without one, everything on screen reflects regardless
	// of whether it's actually a mirror-like surface) and via user testing
	// (grass, rock, characters were all visibly reflecting like wet glass).
	// Real water/wet-floor surfaces are locally planar (near-zero
	// curvature); grass/rock/foliage/characters are not.
	float flatness = texture(textureFlatness, passUV).x;
	vec3 viewDir = normalize(centerPos);
	if (dot(normal, viewDir) > 0.0) normal = -normal;
	// Small bias to avoid self-intersection with the reflector surface
	centerPos = centerPos + normal * 0.10 * viewZ;
	vec3 reflectDir = reflect(viewDir, normal);

	// March along the reflected ray in pseudo-view-space, projecting each
	// step back to screen UV and testing against the real depth buffer -
	// a simplified, single-hit substitute for Godot/McGuire-Mara's proper
	// perspective-correct 2D DDA screen-space traversal (see this shader's
	// own header comment on why the full technique isn't available here).
	//
	// Step size scales with the reflecting pixel's OWN distance from camera
	// (viewZ) - confirmed via research (screen-space-reflection step-size
	// discussions - e.g. GameDev.net's own SSR ray-march-step-size thread)
	// that a FIXED view-space step budget fundamentally cannot reach a
	// distant surface within a limited iteration count: reflecting a cliff
	// across a lake needs the ray to cover far more distance than
	// reflecting a rock a meter away, so a constant step size that works up
	// close runs out of steps long before it can reach anything far away -
	// confirmed via user testing as reflections "disappearing" whenever the
	// reflected surface was more than a short distance off. Real SSR
	// (McGuire/Mara, Godot) avoids this entirely by marching in actual
	// screen-space pixels via a real projection matrix, which we don't
	// have; scaling the pseudo-view-space step by the surface's own depth
	// is the standard compromise when only an assumed FOV is available.
	// SSR_STEPS coarse steps find the interval the ray first crosses the
	// scene surface in, then SSR_REFINE_STEPS of binary search converge
	// on the precise crossing point within that interval - confirmed via
	// research (Killing Floor 2's own public SSR writeup, and a Khronos
	// forum thread whose bug was literally computing this refinement but
	// never consuming its result) that skipping this refinement is exactly
	// what produces visible banding: without it, every pixel's hit UV
	// snaps to whichever DISCRETE coarse step first satisfied the
	// thickness test, and since the step size grows with iteration count,
	// that snapped position can jump a lot between neighboring pixels
	// whose rays happen to cross the surface one step earlier or later -
	// confirmed via user testing as reflections (and, since SSR shades
	// every screen pixel including ones the game's own HUD happens to be
	// drawn over, HUD icons too) appearing sliced into discrete bands.
	// Bumped from 24/5 and the growth exponent slowed from 0.15 to 0.08
	// (user testing found the first refinement pass "better, but needs to
	// be refined MUCH more") - per the same research above, MORE/finer
	// coarse steps directly shrinks each step's own bracket width (less
	// for the binary search to have to converge across per pixel), and
	// more refine iterations converge tighter within whatever bracket
	// remains. Both are cheap (a handful of extra texture samples), so
	// there's no reason to stay stingy here given the visible banding.
)" R"(
	const int SSR_STEPS = 64;
	const int SSR_REFINE_STEPS = 12;
	// The floor here (was 1.0) sets how far even a reflector RIGHT NEXT TO
	// THE CAMERA can reach - confirmed via user testing as an abrupt,
	// fake-looking cutoff line across the water: nearby water (small
	// viewZ) hit this floor and could only reach ~140 pseudo-view-space
	// units over all 64 steps, nowhere near enough to reflect a distant
	// rock wall, while water farther from the camera (larger viewZ, so a
	// bigger step size from the OTHER branch of this max()) reached it
	// fine - producing a hard line at whatever screen distance the two
	// branches crossed over, instead of a smooth reflection all the way
	// from the player's feet out to the rocks. Raised to 4.0 so nearby
	// water's own reach (~550 units) comfortably covers the same
	// mid-to-long distances distFade already treats as valid reflection
	// range (250-700, see distFade's own comment) regardless of how close
	// the reflecting water itself is to the camera.
	float stepSize = max(faroGtaoParams.y, 1e-3) * max(viewZ * 0.15, 4.0);
	float thickness = stepSize * 6.0;
	// Dither the march's starting offset per pixel (same idea as Killing
	// Floor 2's Bayer-matrix dither, using this codebase's own GI-style
	// hash instead) - trades the coherent step-aligned banding pattern for
	// much less objectionable per-pixel noise, on top of the refinement
	// above (the two fixes address the same symptom from different
	// angles: refinement removes the big jumps, dithering breaks up
	// whatever small residual stepping pattern is left).
	// Temporal stability: use a stable hash per pixel (no frame jitter)
	// and a fixed march start. The previous jitter was per-frame
	// dependent via gl_FragCoord + TAA jitter, which made the march
	// sample comb shift every frame = reflections blinking while standing
	// still. We keep a tiny sub-pixel offset for banding, but it is
	// constant per pixel.
	vec2 stableCoord = floor(gl_FragCoord.xy);
	float jitter = SsrHash(stableCoord);
	bool hit = false;
	vec2 hitUV = vec2(0.0);
	float prevTravelled = 0.0;
	float hitTravelled = 0.0;
	for (int i = 1; i <= SSR_STEPS; i++)
	{
		// Gently increasing step size (cheap, not perspective-exact) -
		// covers more ground father from the surface without needing many
		// more steps up close where fine detail matters most.
		float travelled = (float(i) + jitter * 0.5) * stepSize * (1.0 + float(i) * 0.08);
		vec3 marchPos = centerPos + reflectDir * travelled;
		if (marchPos.z <= 0.0)
			break; // behind the camera under this pseudo-projection
		vec2 suv = SsrProjectToUV(marchPos, aspect, tanFov);
		if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
			break; // ray left the visible screen
		float sceneDepthRaw = min(max(textureLod(textureDepth, suv, 0.5).x, 0.0), 1.0);
		float sceneZ = SsrLinearize(sceneDepthRaw);
		// Thickness relaxes with travelled distance: far from the
		// reflector each step covers more ground, so a fixed slab misses
		// legitimate crossings (more "lost reflections" on camera moves).
		float localThickness = thickness * (1.0 + float(i) * 0.35);
		if (sceneZ < marchPos.z && (marchPos.z - sceneZ) < localThickness + 0.02)
		{
			hit = true;
			hitTravelled = travelled;
			break;
		}
		prevTravelled = travelled;
	}
	if (!hit)
	{
		colorOut0 = vec4(color, 1.0);
		return;
	}

	// Binary search refinement between the last known miss (prevTravelled,
	// ray still in front of the surface) and the first known hit
	// (hitTravelled, ray already at/behind it) - see this loop's own
	// comment above for why this specific step is what actually fixes
	// banding (not just a quality nicety).
	{
		float lo = prevTravelled;
		float hi = hitTravelled;
		for (int r = 0; r < SSR_REFINE_STEPS; r++)
		{
			float mid = 0.5 * (lo + hi);
			vec3 midPos = centerPos + reflectDir * mid;
			vec2 midUV = SsrProjectToUV(midPos, aspect, tanFov);
			float midDepthRaw = min(max(texture(textureDepth, midUV).x, 0.0), 1.0);
			float midSceneZ = SsrLinearize(midDepthRaw);
			if (midSceneZ < midPos.z)
				hi = mid; // still past the surface - narrow from above
			else
				lo = mid; // still in front of the surface - narrow from below
		}
		hitUV = SsrProjectToUV(centerPos + reflectDir * hi, aspect, tanFov);
	}

	// Hit validation: reconstruct the hit surface's normal (cheap central
	// differences at hitUV) and require it to face the incoming ray.
	// Without this the march "hits" backfaces and background seen THROUGH
	// foreground geometry - the classic SSR leak that paints reflections
	// onto places (and from places) that should never reflect. Genuine
	// hits on front-facing water/ground pass ~1.0; leaks get rejected.
	float hitFacing = 1.0;
	{
		vec2 htex = 1.0 / outRes;
		float hC = SsrLinearize(min(max(textureLod(textureDepth, hitUV, 0.5).x, 0.0), 1.0));
		float hL = SsrLinearize(min(max(textureLod(textureDepth, hitUV - vec2(htex.x, 0.0), 0.5).x, 0.0), 1.0));
		float hR = SsrLinearize(min(max(textureLod(textureDepth, hitUV + vec2(htex.x, 0.0), 0.5).x, 0.0), 1.0));
		float hD = SsrLinearize(min(max(textureLod(textureDepth, hitUV - vec2(0.0, htex.y), 0.5).x, 0.0), 1.0));
		float hU = SsrLinearize(min(max(textureLod(textureDepth, hitUV + vec2(0.0, htex.y), 0.5).x, 0.0), 1.0));
		vec3 hdx = SsrReconstructPos(hitUV + vec2(htex.x, 0.0), hR, aspect, tanFov) - SsrReconstructPos(hitUV - vec2(htex.x, 0.0), hL, aspect, tanFov);
		vec3 hdy = SsrReconstructPos(hitUV + vec2(0.0, htex.y), hU, aspect, tanFov) - SsrReconstructPos(hitUV - vec2(0.0, htex.y), hD, aspect, tanFov);
		vec3 hitN = cross(hdx, hdy);
		hitN *= inversesqrt(max(dot(hitN, hitN), 1e-12)); // safe normalize, never NaN on flat hits
		if (dot(hitN, viewDir) > 0.0)
			hitN = -hitN; // keep the normal on the viewer's side
		hitFacing = smoothstep(0.0, 0.40, dot(hitN, -reflectDir));
	}
	vec3 reflectionColor = texture(textureSrc, hitUV).rgb;
	// Simple bilateral denoising: average 3x3 neighborhood weighted by
	// depth similarity to the HIT point (not the reflector - the old
	// guide compared neighborhood depth against the reflector's own
	// depth, which smeared unrelated colors across cuts instead of
	// cleaning the reflected image).
	{
		vec2 texel = 1.0 / outRes;
		float hitDepthRaw = min(max(texture(textureDepth, hitUV).x, 0.0), 1.0);
		vec3 acc = vec3(0.0);
		float wsum = 0.0;
		for (int y = -2; y <= 2; y++)
		{
			for (int x = -2; x <= 2; x++)
			{
				vec2 uv = hitUV + vec2(float(x), float(y)) * texel;
				vec3 c = texture(textureSrc, uv).rgb;
				float d = texture(textureDepth, uv).x;
				float w = exp(-abs(d - hitDepthRaw) * 10.0);
				acc += c * w;
				wsum += w;
			}
		}
		reflectionColor = acc / max(wsum, 1e-5);
	}
	// Fresnel (Schlick, F0=0.16 - no metallic/material buffer to vary this
	// per-pixel, same "no G-buffer" limitation as GI). Raised from the
	// physically-correct dielectric-water value (0.04) per user testing:
	// at 0.04 the reflection blend craters to ~4% whenever the surface is
	// viewed close to head-on (e.g. tilting the camera down to look at
	// water from above), which reads as "barely there"/opaque regardless
	// of the faro_ssr_intensity slider (that's just a multiplier on top of
	// this same term). 0.16 keeps the grazing-angle falloff shape (still
	// approaches 1.0 at grazing angles) but keeps head-on reflections
	// clearly visible too, closer to how stylized game water is usually
	// authored rather than strict real-world dielectric Fresnel.
	float ndotv = clamp(dot(normal, -viewDir), 0.0, 1.0);
	float fresnel = 0.16 + 0.84 * pow(1.0 - ndotv, 5.0);
	// Edge fade - same margin-based idea as Godot's own SSR, simplified to
	// axis-aligned UV distance from the nearest screen edge.
	vec2 edgeDist = min(hitUV, 1.0 - hitUV);
	float edgeFade = smoothstep(0.0, 0.15, min(edgeDist.x, edgeDist.y));
	// Distance fade - same IDEA as GTAO's/GI's own distFade (suppress
	// spurious results where the depth buffer goes flat/unreliable near
	// the far plane), but NOT the same range: GTAO/GI only ever look at
	// small local neighborhoods around close-to-mid geometry, while SSR's
	// most common subject (water) is a large flat plane that legitimately
	// extends much farther into the frustum. Reusing GTAO/GI's 30-120
	// range here was confirmed via user testing to kill reflections on
	// almost all real water, not just the sky - so this only fades in
	// right before the assumed far plane (1000, see faroGtaoParams.w),
	// where the depth buffer is actually degenerate/flat, not at ordinary
	// mid-to-long reflection distances.
	float distFade = 1.0 - smoothstep(250.0, 700.0, viewZ);
	// Travel confidence fade: hits found after a long march are far less
	// reliable (more chances of leaks/false crossings) than close ones.
	// Fading smoothly instead of a binary hit/miss also removes the hard
	// on/off "cut" line where rays run out of steps or leave the screen -
	// the old code snapped from full reflection to zero between
	// neighboring pixels, which is exactly the visible slicing on water.
	float maxTravel = float(SSR_STEPS) * stepSize * (1.0 + float(SSR_STEPS) * 0.08);
	float travelFade = 1.0 - smoothstep(maxTravel * 0.30, maxTravel, hitTravelled);
	// Hysteresis: once a pixel has a hit, keep it for a few frames of
	// marginal misses to avoid flicker when the depth buffer jitters
	// around the thickness threshold. We emulate it with a soft
	// minimum on the strength: if the hit is marginal, we still keep a
	// small residual instead of snapping to zero.
	float hitConfidence = smoothstep(0.0, 0.25, hitFacing) * smoothstep(0.0, 0.25, flatness);
	// View angle fade: avoid hard pop when looking almost straight down/up
	float viewAngleFade = smoothstep(0.0, 0.3, abs(dot(normal, viewDir)));
	float ssrStrength = clamp(fresnel * edgeFade * distFade * travelFade * hitConfidence * viewAngleFade * intensity, 0.0, 1.0);
	vec3 result = mix(color, reflectionColor, ssrStrength);
	colorOut0 = vec4(min(max(result, vec3(0.0)), vec3(1.0)), 1.0);
}
)";

std::string RendererOutputShader::BuildSsrShaderSource()
{
	return s_ssr_shader_body;
}

std::string RendererOutputShader::BuildSmaaBlendShaderSource(sint32 smaaQuality)
{
	// Matches the public reference's own SMAA_PRESET_LOW/MEDIUM/HIGH/ULTRA
	// blocks exactly (see SMAA.hlsl's "Presets" section) - LOW/MEDIUM disable
	// diagonal and corner detection entirely (cheaper, blockier results),
	// HIGH/ULTRA enable both with a wider search.
	std::string defines;
	switch (smaaQuality)
	{
	case 0: // kSmaaLow
		defines = "#define SMAA_MAX_SEARCH_STEPS 4\n#define SMAA_DISABLE_DIAG_DETECTION\n#define SMAA_DISABLE_CORNER_DETECTION\n";
		break;
	case 1: // kSmaaMedium
		defines = "#define SMAA_MAX_SEARCH_STEPS 8\n#define SMAA_DISABLE_DIAG_DETECTION\n#define SMAA_DISABLE_CORNER_DETECTION\n";
		break;
	case 3: // kSmaaUltra
		defines = "#define SMAA_MAX_SEARCH_STEPS 32\n#define SMAA_MAX_SEARCH_STEPS_DIAG 16\n#define SMAA_CORNER_ROUNDING 25\n";
		break;
	case 2: // kSmaaHigh
	default:
		defines = "#define SMAA_MAX_SEARCH_STEPS 16\n#define SMAA_MAX_SEARCH_STEPS_DIAG 8\n#define SMAA_CORNER_ROUNDING 25\n";
		break;
	}
	return defines + s_smaa_blend_shader_body;
}

std::string RendererOutputShader::PrependFragmentPreamble(const std::string& shaderSrc)
{
	return R"(#version 430
layout(location = 0) smooth in vec2 passUV;
layout(binding = 0) uniform sampler2D textureSrc;
// textureSrc2/textureSrc3 (SMAA's extra inputs) are deliberately NOT declared
// here - only s_smaa_blend_shader_body and s_smaa_neighborhood_shader_source
// actually sample them, and each declares them itself. Every other output
// shader (copy/bicubic/hermite/fsr1/fxaa/smaa-edge-detect) only ever samples
// textureSrc, so it must not declare bindings 2/3 either.
layout(location = 0) out vec4 colorOut0;

#ifdef VULKAN
layout (binding = 1, std140)
#else
layout (binding = 0, std140)
#endif
uniform parameters {
uniform vec2 textureSrcResolution;
uniform vec2 nativeResolution;
uniform vec2 outputResolution;
uniform bool applySRGBEncoding;
uniform float targetGamma;
uniform float displayGamma;
// Faro TAA: only s_taa_resolve_shader_source reads this - see
// OutputUniformVariables's own comment for why it's harmless elsewhere.
uniform bool taaHistoryValid;
// Faro GTAO: only s_gtao_shader_source reads this (x=intensity 0-1,
// y=radius in view units, z=quality, w=depth linearization far plane).
uniform vec4 faroGtaoParams;
};

float sRGBEncode(float linear)
{
	if(linear <= 0.0031308)
		return 12.92f * linear;
	else
		return 1.055f * pow(linear, 1.0f / 2.4f) - 0.055f;

}

vec3 sRGBEncode(vec3 linear)
{
	return vec3(sRGBEncode(linear.r), sRGBEncode(linear.g), sRGBEncode(linear.b));
}

// fwd. declaration
void outputShader();
void main()
{
	outputShader(); // sets colorOut0
	if(applySRGBEncoding)
		colorOut0 = vec4(sRGBEncode(colorOut0.rgb), 1.0f);

	if (displayGamma > 0.0f)
		colorOut0 = pow(colorOut0, vec4(targetGamma / displayGamma) );
	else
		colorOut0 = vec4( sRGBEncode( pow(colorOut0.rgb, vec3(targetGamma)) ), 1.0f);

}

)" + shaderSrc;
}
void RendererOutputShader::InitializeStatic()
{
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_METAL
    case RendererAPI::Metal:
    {
        std::string vertex_source = GetMetalVertexSource(false);
        std::string vertex_source_ud = GetMetalVertexSource(true);

       	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source_mtl);
       	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source_mtl);

       	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source_mtl);
       	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source_mtl);

       	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source_mtl);
       	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source_mtl);
		break;
    }
#endif
#ifdef ENABLE_OPENGL
    case RendererAPI::OpenGL:
    {
    	std::string vertex_source, vertex_source_ud;
    	// vertex shader
		vertex_source = GetOpenGlVertexSource(false);
		vertex_source_ud = GetOpenGlVertexSource(true);

    	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source);
    	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source);

    	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source);
    	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source);

    	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source);
    	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source);
		break;
    }
#endif
#ifdef ENABLE_VULKAN
    case RendererAPI::Vulkan:
    {
    	std::string vertex_source, vertex_source_ud;
    	// vertex shader
		vertex_source = GetVulkanVertexSource(false);
		vertex_source_ud = GetVulkanVertexSource(true);

    	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source);
    	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source);

    	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source);
    	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source);

    	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source);
    	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source);

    	s_fsr1_easu_shader = new RendererOutputShader(vertex_source, s_fsr1_easu_shader_source);
    	s_fsr1_easu_shader_ud = new RendererOutputShader(vertex_source_ud, s_fsr1_easu_shader_source);
    	s_fsr1_rcas_shader = new RendererOutputShader(vertex_source, s_fsr1_rcas_shader_source);

    	s_fxaa_shader = new RendererOutputShader(vertex_source, s_fxaa_shader_source);

    	// All 4 SMAA quality presets are compiled upfront so switching
    	// CemuConfig::smaa_quality at runtime never needs a shader recompile.
    	for (sint32 q = 0; q < kSmaaQualityCount; q++)
    	{
    		s_smaa_edge_shader[q] = new RendererOutputShader(vertex_source, BuildSmaaEdgeShaderSource(q));
    		s_smaa_blend_shader[q] = new RendererOutputShader(vertex_source, BuildSmaaBlendShaderSource(q));
    	}
    	s_smaa_neighborhood_shader = new RendererOutputShader(vertex_source, s_smaa_neighborhood_shader_source);
    	s_taa_resolve_shader = new RendererOutputShader(vertex_source, s_taa_resolve_shader_source);

    	// Faro GTAO: all 3 quality presets compiled upfront so switching
    	// CemuConfig::faro_ssao_quality at runtime never needs a recompile
    	// (same pattern as the SMAA presets above).
    	for (sint32 q = 0; q < kGtaoQualityCount; q++)
    	{
    		s_gtao_shader[q] = new RendererOutputShader(vertex_source, BuildGtaoShaderSource(q));
    	}
    	s_gi_shader = new RendererOutputShader(vertex_source, BuildGiShaderSource());
    	s_gi_blur_shader = new RendererOutputShader(vertex_source, BuildGiBlurShaderSource());
    	s_ssr_flatness_shader = new RendererOutputShader(vertex_source, BuildSsrFlatnessShaderSource());
    	s_ssr_shader = new RendererOutputShader(vertex_source, BuildSsrShaderSource());
		break;
    }
#endif
	}
}

void RendererOutputShader::ShutdownStatic()
{
	delete s_copy_shader;
	delete s_copy_shader_ud;

	delete s_bicubic_shader;
	delete s_bicubic_shader_ud;

	delete s_hermit_shader;
	delete s_hermit_shader_ud;

	delete s_fsr1_easu_shader;
	delete s_fsr1_easu_shader_ud;
	delete s_fsr1_rcas_shader;

	delete s_fxaa_shader;

	for (sint32 q = 0; q < kSmaaQualityCount; q++)
	{
		delete s_smaa_edge_shader[q];
		delete s_smaa_blend_shader[q];
		s_smaa_edge_shader[q] = nullptr;
		s_smaa_blend_shader[q] = nullptr;
	}
	delete s_smaa_neighborhood_shader;
	delete s_taa_resolve_shader;

	for (sint32 q = 0; q < kGtaoQualityCount; q++)
	{
		delete s_gtao_shader[q];
		s_gtao_shader[q] = nullptr;
	}
	delete s_gi_shader;
	s_gi_shader = nullptr;
	delete s_gi_blur_shader;
	s_gi_blur_shader = nullptr;
	delete s_ssr_flatness_shader;
	s_ssr_flatness_shader = nullptr;
	delete s_ssr_shader;
	s_ssr_shader = nullptr;

	s_copy_shader = nullptr;
	s_copy_shader_ud = nullptr;
	s_bicubic_shader = nullptr;
	s_bicubic_shader_ud = nullptr;
	s_hermit_shader = nullptr;
	s_hermit_shader_ud = nullptr;
	s_fsr1_easu_shader = nullptr;
	s_fsr1_easu_shader_ud = nullptr;
	s_fsr1_rcas_shader = nullptr;
	s_fxaa_shader = nullptr;
	s_smaa_neighborhood_shader = nullptr;
	s_taa_resolve_shader = nullptr;
}