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

 // The 12-tap kernel below reaches from -1 to +2 texels around the sample
 // position, so at the image border it addresses texels that do not exist.
 // texelFetch with an out-of-range coordinate has UNDEFINED results in GLSL -
 // in practice drivers differ: some return black, some clamp, some return
 // whatever is adjacent in memory. That is a real "looks fine here, has a
 // dark/garbled one-pixel frame on someone else's GPU" class of bug, and it
 // cannot be fixed with a sampler address mode because texelFetch ignores
 // sampler state entirely. AMD's own reference avoids it by construction: it
 // fetches through textureGather on a CLAMP_TO_EDGE sampler. Clamping the
 // integer coordinate here reproduces that same edge behaviour exactly.
 // Clamped against textureSrcResolution (the view's EFFECTIVE size, see
 // FillUniformBlockBuffer) and not the allocated texture size, so this never
 // reads a padded texture's unused region either.
 vec3 FaroEasuFetch(ivec2 p) {
     return texelFetch(textureSrc, clamp(p, ivec2(0), ivec2(textureSrcResolution) - ivec2(1)), 0).rgb;
 }

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
     vec3 bC = FaroEasuFetch(fpi + ivec2(0, -1));
     vec3 cC = FaroEasuFetch(fpi + ivec2(1, -1));
     vec3 eC = FaroEasuFetch(fpi + ivec2(-1, 0));
     vec3 fC = FaroEasuFetch(fpi + ivec2(0, 0));
     vec3 gC = FaroEasuFetch(fpi + ivec2(1, 0));
     vec3 hC = FaroEasuFetch(fpi + ivec2(2, 0));
     vec3 iC = FaroEasuFetch(fpi + ivec2(-1, 1));
     vec3 jC = FaroEasuFetch(fpi + ivec2(0, 1));
     vec3 kC = FaroEasuFetch(fpi + ivec2(1, 1));
     vec3 lC = FaroEasuFetch(fpi + ivec2(2, 1));
     vec3 nC = FaroEasuFetch(fpi + ivec2(0, 2));
     vec3 oC = FaroEasuFetch(fpi + ivec2(1, 2));

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
 // Sharpness and the optional noise-detection path both come from
 // faroFsrParams (see CemuConfig::fsr_rcas_sharpness / fsr_rcas_denoise)
 // rather than being compiled in: AMD passes sharpness through FsrRcasCon as
 // a constant for exactly this reason, and the right amount depends on the
 // upscale ratio and display, so it has to be adjustable without a shader
 // rebuild.

 // Same out-of-range texelFetch hazard as EASU's own FaroEasuFetch (see its
 // comment for why this cannot be left to a sampler address mode) - the cross
 // pattern below reaches one texel past the image on each border. Clamped
 // against outputResolution, which is the size of the EASU output this pass
 // reads.
 vec3 FaroRcasFetch(ivec2 p) {
     return texelFetch(textureSrc, clamp(p, ivec2(0), ivec2(outputResolution) - ivec2(1)), 0).rgb;
 }

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
     vec3 bC = FaroRcasFetch(sp + ivec2( 0,-1));
     vec3 dC = FaroRcasFetch(sp + ivec2(-1, 0));
     vec3 eC = FaroRcasFetch(sp);
     vec3 fC = FaroRcasFetch(sp + ivec2( 1, 0));
     vec3 hC = FaroRcasFetch(sp + ivec2( 0, 1));

     // Luma times 2.
     float bL = bC.b * 0.5 + (bC.r * 0.5 + bC.g);
     float dL = dC.b * 0.5 + (dC.r * 0.5 + dC.g);
     float eL = eC.b * 0.5 + (eC.r * 0.5 + eC.g);
     float fL = fC.b * 0.5 + (fC.r * 0.5 + fC.g);
     float hL = hC.b * 0.5 + (hC.r * 0.5 + hC.g);

     // Noise detection (verbatim FsrRcasF's own FSR_RCAS_DENOISE block): a
     // normalized highpass of the cross, used below to scale the sharpening
     // lobe down where the local signal looks like grain rather than an
     // edge. Computed unconditionally - it is a handful of ALU ops with no
     // extra texture reads, and branching around it would cost more than it
     // saves.
     float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
     nz = ASatF1(abs(nz) * APrxMedRcpF1(AMax3F1(AMax3F1(bL, dL, eL), fL, hL) - AMin3F1(AMin3F1(bL, dL, eL), fL, hL)));
     nz = -0.5 * nz + 1.0;

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
     // FsrRcasCon's own "stops to linear value" transform.
     float sharpnessScale = exp2(-faroFsrParams.x);
     float lobe = max(-FSR_RCAS_LIMIT, min(AMax3F1(lobeR, lobeG, lobeB), 0.0)) * sharpnessScale;
     // Apply noise removal (FSR_RCAS_DENOISE).
     if (faroFsrParams.y > 0.5)
         lobe *= nz;
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
// Faro motion field (quarter resolution, see RunFaroMotionEstimation). Bound
// every frame; whether the resolve actually reprojects with it is a separate
// step - right now it only feeds the debug view, deliberately, so the field
// can be judged on its own before anything depends on it.
layout(binding = 3) uniform sampler2D textureMotion;

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

	// Debug view 6: the motion field, before anything depends on it. Hue is
	// direction, brightness is speed, grey is at rest.
	//
	// Built and checked BEFORE wiring the field into the reprojection on
	// purpose. The AO investigation burned several rounds inferring the state
	// of an intermediate buffer from the final image, when looking directly
	// at the buffer answered it in seconds. A broken motion field is obvious
	// here - noise, or colour in a still scene - and close to undiagnosable
	// from a resolved frame.
	//
	// What correct looks like: flat grey when nothing moves, and panning the
	// camera washing the screen in one even colour that flips to the opposite
	// hue when the pan reverses.
	if (int(faroDebugParams.x + 0.5) == 6)
	{
		// faroMotionParams.x says whether a real field is bound. Without this
		// check the view painted whatever happened to be bound as a fallback
		// - the scene itself - and reading its red/green channels as a vector
		// produced a convincing-looking colour image that meant nothing. A
		// debug view that invents data is worse than no debug view, so when
		// there is no field this says so plainly: flat black.
		if (faroMotionParams.x < 0.5)
		{
			colorOut0 = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		vec2 motion = texture(textureMotion, uv).rg;
		float speed = clamp(length(motion) * 40.0, 0.0, 1.0);
		float angle = atan(motion.y, motion.x);
		vec3 wheel = clamp(abs(mod(angle / 6.2831853 * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
		colorOut0 = vec4(mix(vec3(0.5), wheel, speed), 1.0);
		return;
	}

vec3 cM = texture(textureSrc, uv).rgb;
	if (!taaHistoryValid) {
		// First frame after boot/resize/reset - no history to blend with yet.
		colorOut0 = vec4(cM, 1.0);
		return;
	}

	// === Reprojection ===
	//
	// Without this the history is read at the pixel's own coordinate, which
	// means the temporal filter can only ever average a pixel with ITSELF one
	// frame ago. That is not antialiasing, it is a fade towards the previous
	// frame, and it forces the choice documented in FARO_TAA_FSR1_DEPTH_
	// investigacion.md: a little history barely smooths anything, a lot of
	// history smears whatever the camera moved over. Which is why TaaHistory
	// has to be pulled down to 60-70 by hand.
	//
	// The motion field says where this pixel USED to be, so the history can be
	// read from there instead. That is what makes a long history safe: the
	// samples being averaged all show the same piece of world.
	//
	// faroMotionParams.x is the dispatch's own "is there a field" flag, set
	// from RunFaroMotionEstimation's return value. When it is unset the
	// offset stays zero and this whole block collapses to the same-coordinate
	// read it replaces - an exact, unchanged fallback rather than a second
	// code path to keep in sync.
vec2 motionOffset = vec2(0.0);
	vec3 hist;
	if (faroMotionParams.x > 0.5)
	{
		// Sampled with a plain bilinear fetch rather than nearest: the field
		// is a quarter of native resolution and its texels are bilinear
		// themselves, so nearest would quantise the offset to whole
		// quarter-res texels and reintroduce the very stepping the smooth
		// upsample exists to avoid.
		motionOffset = texture(textureMotion, uv).rg;
		// The estimate is in the field's own quarter-resolution units, so it
		// has to become a fraction of THIS pass's UV to be an offset in UV.
		// Written as an explicit component divide rather than `/=`: GLSL has
		// no componentwise divide-assign, and `v /= s` silently tries to
		// assign a float back into a vec2.
		motionOffset = vec2(motionOffset.x / max(textureSrcResolution.x, 1.0),
			motionOffset.y / max(textureSrcResolution.y, 1.0));
		// Clamped rather than discarded. The matcher already rejects
		// off-screen vectors, but a bilinear fetch at the very edge of the
		// field can still round one just past it, and sampling outside the
		// history target reads whatever the driver decides is there.
		vec2 historyUV = clamp(uv + motionOffset, vec2(0.0), vec2(1.0));
		// faroMotionParams.y is the master switch, so turning reprojection
		// off drives the offset to exactly zero and this collapses to the
		// same-coordinate read it replaces - the pre-field behaviour, not an
		// approximation of it.
		historyUV = mix(uv, historyUV, faroMotionParams.y);
		hist = clamp(SampleHistoryBicubic(historyUV), 0.0, 1.0);

		// The whole point of reprojection is to arrive somewhere that still
		// holds what this pixel held. If it does not, the field is wrong
		// here - grass, a wrong match, a disocclusion the estimator missed -
		// and reprojecting faithfully would blend two different things. The
		// variance clip below would catch most of that on its own, but only
		// AFTER the fetch, and it rejects on colour alone. Checking the
		// colour at the ORIGINAL coordinate is nearly free and catches the
		// case the clip cannot: history that is merely a plausible colour
		// somewhere else entirely.
		vec3 cHistUnshifted = texture(textureSrc2, uv).rgb;
		float unshiftedDist = length(cHistUnshifted - cM);
		float shiftedDist = length(hist - cM);
		// Only override when staying put was clearly better, so a correct
		// reprojection that lands on a similar-but-not-identical colour (any
		// real motion, any lighting change) is still trusted.
		if (unshiftedDist < shiftedDist * 0.6)
			hist = clamp(SampleHistoryBicubic(uv), 0.0, 1.0);
	}
	else
	{
		hist = clamp(SampleHistoryBicubic(uv), 0.0, 1.0);
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

	// Clamped to [0,1] on the way in. SampleHistoryBicubic has negative lobes,
	// so it overshoots and undershoots the range of its own source even when
	// that source is perfectly in range. While the history buffer was 8-bit
	// UNORM every write saturated and this never mattered; it is now 16-bit
	// float (see EnsureTaaHistoryTarget), which clamps nothing, and the result
// feeds back into the history every frame. An out-of-range value entering
	// this loop can grow instead of decaying - and the Karis inverse below
	// divides by (1 - luma), so a luma approaching 1 multiplies by up to 1e4
	// and the pixel blows out to white.
	//
	// (hist itself was sampled above, in the reprojection block.)

	// Variance box in YCoCg, at a FIXED width. It used to be widened or
	// narrowed by a luma-difference "ghost" term; that term is gone, and
	// removing it is what let sub-pixel jitter be switched back on - see
	// below and LatteMRT::GetCurrentTaaJitter.
	//
	// The floor on the box matters: on a flat surface the neighbourhood
	// variance is almost zero, so without it the box would collapse and
	// reject history over mere quantization noise, stalling accumulation
	// exactly where it is cheapest to accumulate.
	vec3 vbox = max(sqrt(max(m2 - m1 * m1, vec3(0.0))) * 1.25, vec3(0.015));
	vec3 yHist = RGB2YCoCg(hist);
	vec3 yClamped = ClipAABB(m1 - vbox, m1 + vbox, yHist);
	vec3 histClamped = YCoCg2RGB(yClamped);

	// Blend fraction of the CURRENT frame: a FIXED weight, from
	// CemuConfig::taa_history (faroFsrParams.z). The clip above is the entire
	// rejection mechanism; the blend weight must not reject a second time.
	//
	// This used to be mix(slider, 0.95, clamp(clipDistance, 0, 1)), i.e. the
	// weight was pushed towards "almost entirely the current frame" in
	// proportion to how far history had been clipped. That was wrong, and it
	// is what made the whole image tremble with TAA active while FXAA (which
	// applies no jitter at all) looked clean.
	//
	// Why it was wrong: the comment it replaced claimed a jittered sample of
	// the same surface "still lies INSIDE the local colour box, so it clips
	// by nothing". That is false exactly where it matters. The box is built
	// from the CURRENT frame's 3x3 neighbourhood, and on a high-contrast edge
	// a sub-pixel shift moves the colour a long way - so history clipped far,
	// clipDist saturated, the weight went to ~0.95, and the output was
	// essentially the raw jittered frame. Accumulation stalled precisely on
	// the edges that needed it, and the jitter was left on screen as wobble.
	// Double-rejecting cancelled the feature out: jitter supplies sub-pixel
	// samples, and this threw them away.
	//
	// Karis 2014 clips history into the box and then blends with a constant
	// weight - the clip bounds the error, which is its job. With no
	// reprojection, fast camera motion means history is stale, but clipping
	// keeps it inside the local colour box, so the result degrades to a
	// slightly softened current frame rather than a smear.
	//
	// Going as low as 0.05 is only meaningful because the history buffer is
	// 16-bit float (see EnsureTaaHistoryTarget) - at 8 bits a 0.05
	// contribution falls below one quantization step for any difference under
	// ~8% and the accumulation stalls outright.
	float blendCur = clamp(faroFsrParams.z, 0.05, 1.0);

	// Karis 2014 ("High Quality Temporal Supersampling") HDR weighting: blend
	// in a tonemapped space, c / (1 + luma(c)), instead of linearly. Without
	// it a single very bright sample dominates the average and reads as a
	// flickering firefly on specular highlights and foliage; the transform
	// compresses the bright end so one hot sample can no longer outvote the
	// rest, and the blend is then expanded back.
	//
	// NOTE the direction: this reweights the SAMPLES. Scaling the blend
	// factor by a luma term instead (a tempting shortcut) does the opposite
	// of what is wanted here - it would make bright pixels retain MORE
	// history and so ghost harder, which is exactly the artifact Karis's
	// weighting exists to remove.
	vec3 tCur = cM / (1.0 + FaroLuma(cM));
	vec3 tHist = histClamped / (1.0 + FaroLuma(histClamped));
	vec3 tMix = mix(tHist, tCur, blendCur);
	// Inverse transform. The denominator is clamped away from zero: luma of
	// the tonemapped value approaches 1 for very bright input, and an
	// unclamped reciprocal there produces infinities that propagate as white
	// specks.
	vec3 resolved = tMix / max(1.0 - FaroLuma(tMix), 1e-4);

	// Optional unsharp mask against the SPATIAL cross - deliberately not
	// against the history, since cM - hist is a temporal difference and
	// amplifying it would boost exactly the pixels that disagree between
	// frames, manufacturing shimmer rather than detail.
	//
	// Strength is now a flat fraction of the slider instead of scaling with
	// how much history was used, and that inversion matters. The old form
	// sharpened HARDEST when history dominated, on the assumption that
	// history means blur. With jitter that assumption is backwards: heavy
	// accumulation is now genuine supersampling, which comes out sharper AND
	// less aliased, so sharpening it hardest would re-introduce the very edge
	// contrast the pass just resolved away. Left on the slider because
	// whether an upscaled image wants a touch of acuity back is a taste call,
	// but it is no longer load-bearing.
	vec3 spatialBlur = (cN + cS + cE + cW) * 0.25;
	vec3 sharpened = resolved + (faroFsrParams.w * 0.5) * (resolved - spatialBlur);

	// Anti-ringing guard on the unsharp mask, so it cannot overshoot into
	// halos around high-contrast edges (same reason EASU clips against the
	// min/max of its own nearest 4 taps).
	//
	// The box is the local neighbourhood WIDENED to include `resolved`
	// itself, and that widening is the important part: clamping purely to
	// the current frame's own min/max would discard exactly the temporally
	// accumulated values that lie outside this frame's neighbourhood - which
	// is the entire output of the temporal filter. Widening makes this a
	// pure "the sharpen may not push the pixel further out than it already
	// was" limiter, leaving the resolve itself untouched.
	vec3 mnBox = min(min(min(cM, cN), min(cS, cE)), cW);
	vec3 mxBox = max(max(max(cM, cN), max(cS, cE)), cW);
	mnBox = min(mnBox, resolved);
	mxBox = max(mxBox, resolved);
	colorOut0 = vec4(clamp(sharpened, mnBox, mxBox), 1.0);
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

	// Faro FSR1: RCAS sharpness/denoise - see OutputUniformVariables for why
	// these are filled here instead of per dispatch. Sharpness is stored as
	// hundredths of a stop (see CemuConfig::fsr_rcas_sharpness) and converted
	// to the stops value AMD's own FsrRcasCon expects.
	vars.faroFsrParams[0] = (float)std::clamp(GetConfig().fsr_rcas_sharpness.GetValue(),
		CemuConfig::kFsrRcasSharpnessMin, CemuConfig::kFsrRcasSharpnessMax) / 100.0f;
	vars.faroFsrParams[1] = GetConfig().fsr_rcas_denoise ? 1.0f : 0.0f;
	// Faro TAA: z = minimum weight of the current frame in the resolve (the
	// history length, see CemuConfig::taa_history - 100 maps to 0.05 for the
	// longest history, 0 to 1.0 which disables history entirely), w = unsharp
	// strength (CemuConfig::taa_sharpness). Carried in the spare slots of this
	// same vec4 rather than a new uniform, so the std140 block layout is
	// unchanged.
	const float taaHistory = (float)std::clamp(GetConfig().taa_history.GetValue(), 0, 100) / 100.0f;
	vars.faroFsrParams[2] = 1.0f - taaHistory * 0.95f;
	vars.faroFsrParams[3] = (float)std::clamp(GetConfig().taa_sharpness.GetValue(), 0, 100) / 100.0f;

	// Faro debug view - see OutputUniformVariables::faroDebugParams.
	vars.faroDebugParams[0] = (float)GetConfig().faro_debug_view.GetValue();
	vars.faroDebugParams[1] = 0.0f;
	vars.faroDebugParams[2] = 0.0f;
	vars.faroDebugParams[3] = 0.0f;
	// Faro motion estimation: overridden per pass by the dispatch (see
	// RunFaroMotionEstimation); zero here so every other pass ignores it.
	vars.faroMotionParams[0] = 0.0f;
	vars.faroMotionParams[1] = 0.0f;
	vars.faroMotionParams[2] = 0.0f;
	vars.faroMotionParams[3] = 0.0f;

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
RendererOutputShader* RendererOutputShader::s_gtao_denoise_shader;
RendererOutputShader* RendererOutputShader::s_motion_fill_shader;
RendererOutputShader* RendererOutputShader::s_motion_downsample_shader;
RendererOutputShader* RendererOutputShader::s_motion_estimate_shader;
RendererOutputShader* RendererOutputShader::s_gi_shader;
RendererOutputShader* RendererOutputShader::s_gi_blur_shader;

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
	// Writes the AO FACTOR (what colour gets multiplied by), not shaded
	// colour. Splitting those two is what makes a denoise pass possible at
	// all: a bilateral filter has to average occlusion estimates against each
	// other, and it cannot once they are folded into the scene colour. See
	// s_gtao_denoise_shader_body, which blurs this and then applies it - the
	// same split MXAO uses (its AO pass writes CommonTex0, and
	// PS_SpatialFilter2 is what finally blends into the image).
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	if (intensity <= 0.0)
	{
		colorOut0 = vec4(1.0);
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
	// The march radius MUST shrink with distance. It represents a fixed
	// WORLD-SPACE radius projected onto the screen, and that projection is
	// what makes ambient occlusion a local, contact-shadow effect at every
	// distance instead of only nearby.
	//
	// This was a constant in pixels (32/40/56), and that is what made the
	// effect unstable specifically far away and never close up. Near, 32px
	// spans little world distance, every tap lands on the same surface and
	// the estimate is well-conditioned. Far away the same 32px spans an
	// enormous world distance, so taps land on unrelated geometry at wildly
	// different depths, their weights (see `w` below) collapse towards zero,
	// wsum ends up hovering near its floor, and whichever one or two taps
	// happen to be depth-adjacent decide the whole pixel. Any small change in
	// distant geometry - an LOD pop, animated vegetation, dithered
	// transparency - then flips it. Reported precisely as "always at
	// distance, never near".
	//
	// MXAO scales by depth everywhere for this reason: its AO radius is
	// `0.25 * R / (samples * (depth + 2))` and even its normal-smoothing
	// radius is `0.018 / position.z`.
	//
	// (1 - depth) is the scale factor to use, and it needs no far plane at
	// all: for a standard perspective projection the raw depth is
	// d = f/(f-n) * (1 - n/z), so (1 - d) is proportional to 1/z, which is
	// exactly how a fixed world-space length projects to screen space. That
	// matters here because every previous attempt to scale by distance went
	// through GtaoLinearize's invented far plane and inherited its
	// miscalibration (see the distance-fade comment below for what that cost).
	//
	// Normalized at d = 0.99 so faroGtaoParams.z keeps roughly its old
	// meaning in the mid-field, and capped so the near field cannot explode
	// into a hundreds-of-pixels march.
	float marchPx = clamp(max(faroGtaoParams.z, 1.0) * ((1.0 - depth) / 0.01), 2.0, 96.0);
	// MXAO's own early-out, same threshold in spirit: once the projected
	// radius is down to a couple of pixels there is no geometry left to
	// resolve, and sampling anyway is what manufactures the distant noise.
	// Reporting "no occlusion" is both stable and correct there.
	if (max(faroGtaoParams.z, 1.0) * ((1.0 - depth) / 0.01) < 2.0)
	{
		colorOut0 = vec4(1.0);
		return;
	}
	// Ordered 4x4 dither, taken from MXAO's own jitter
	// (qUINT_mxao.fx: dot(floor(vpos.xy % 4 + 0.1), float2(0.0625, 0.25)) + 0.0625).
	//
	// This replaced an interleaved-gradient hash of gl_FragCoord, which gave
	// every pixel its own rotation. That is the standard trick when a denoise
	// pass follows, but there was none here, so the raw high-frequency noise
	// went straight to the screen. Worse, the pattern is locked to SCREEN
	// space while the scene slides underneath it, so a given surface point
	// drew a different kernel rotation every frame and therefore a different
	// noisy estimate every frame - the AO crawled and sparkled, which is what
	// the flicker actually was.
	//
	// A 4x4 ordered tile gives 16 rotations arranged so neighbouring pixels
	// share them in a regular block. That is what makes the bilateral passes
	// below able to average coherently instead of smearing unrelated
	// estimates together. On its own this does not remove the flicker - it is
	// what lets the filter remove it.
	float ditherJitter = dot(floor(mod(gl_FragCoord.xy, 4.0) + 0.1), vec2(0.0625, 0.25)) + 0.0625;
	float rot = ditherJitter * 3.14159265;
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
	// Normalizing by wsum alone is what made this pass flicker. occ/wsum is a
	// weighted mean of per-sample occlusion so it is always inside [0,1], but
	// it is not STABLE: every sample's weight falls off with depth difference,
	// so wherever neighbours differ a lot in depth - silhouette edges, grass,
	// foliage, exactly the geometry this effect is meant to show - the whole
	// weight mass collapses towards zero and a single noisy sample ends up
	// deciding the entire AO value for that pixel. The camera is never
	// perfectly still and that vegetation animates, so those pixels snap
	// between full occlusion and none from one frame to the next. Read as the
	// image flashing, concentrated on edges, and more visible behind a sharper
	// spatial pre-pass (SMAA) than a blurrier one (FXAA).
	//
	// Flooring the divisor at a fraction of the total weight the kernel COULD
	// have gathered fixes it without a temporal filter: where evidence is
	// plentiful (wsum well above the floor) this is unchanged, and where it is
	// scarce the result is pulled smoothly towards "no occlusion" instead of
	// being decided by noise. Being conservative in exactly those pixels is
	// also the right answer - a handful of valid samples is not enough
	// evidence to darken a pixel.
	float totalWeight = float(FARO_GTAO_DIRS * FARO_GTAO_STEPS);
	float ao = 1.0 - min(max(occ / max(wsum, 0.15 * totalWeight), 0.0), 1.0);
	// Distance fade. Still here for the original reason - raw depth precision
	// collapses far from the camera, so at long range the "occlusion" this
	// technique detects is depth quantization rather than real creases, which
	// showed up as distant terrain darkening with nothing occluding it.
	//
	// But it is now driven by the RAW depth value, not by viewZ, and that
	// change is the point.
	//
	// viewZ comes out of GtaoLinearize, which divides by an invented far plane
	// (faroGtaoParams.w = 1000, admittedly made up - there is no way to read
	// the game's real projection from here). Fading on absolute viewZ between
	// 30 and 120 therefore put the entire transition inside raw depth
	// 0.9676..0.9924 - a 2.5% wide window at the very far end of the range:
	//
	//     d=0.90 -> viewZ 9.9    d=0.98 -> viewZ 50 (fade 0.88)
	//     d=0.96 -> viewZ 24.4   d=0.99 -> viewZ 91 (fade 0.17)
	//
	// Perspective depth is heavily front-loaded, so with a typical near/far
	// the visible mid-field already sits above d=0.99 and was being faded out
	// almost entirely. Worse, WHERE that window lands in world terms depends
	// completely on the game's own near/far planes - so anything that changes
	// the projection (a draw-distance graphic pack, or the game adjusting it
	// per area or per frame) slid the whole curve and the effect's reach grew
	// and shrank on its own while standing still. Reported exactly that way,
	// and no amount of fixing depth selection, descriptor hazards or denoise
	// could touch it.
	//
	// Keying on raw depth instead is immune to all of that: precision
	// collapses as a function of the raw value, which is where the problem
	// actually lives, and the raw value needs no far plane to interpret.
	// Note the AO STRENGTH was always scale-invariant - the occlusion test
	// reduces to (1 - sz/viewZ)/relThreshold, a pure ratio of depths - so
	// this fade was the only part of the pass that the invented far plane
	// could distort.
	float distFade = 1.0 - smoothstep(0.995, 0.9995, depth);
	// Intensity and distance fade are folded in here rather than left to the
	// denoise pass, so what gets blurred is the final multiplier and the
	// filter never needs to know about either.
	float aoFactor = mix(1.0, ao, intensity * distFade);

	// Debug views. Written into the AO target, which the denoise pass then
	// passes through untouched (see its own debug branch), so what reaches the
	// screen is the raw intermediate.
	//
	// Mode 1 is the one that matters most and the question this whole
	// instrument exists to answer: is the DEPTH DATA steady? Everything
	// measurable from the CPU said the pass runs every frame and the depth
	// selection never changes, but neither of those says anything about
	// whether the contents of that buffer are stable frame to frame. If mode 1
	// flickers, the cause is upstream of this shader entirely and no amount of
	// work in here will fix it.
	int debugMode = int(faroDebugParams.x + 0.5);
	if (debugMode == 1)
	{
		// Raw depth, remapped so the far field is actually visible: almost the
		// whole [0,1] range is spent above 0.99, so a linear ramp would show a
		// flat white image and hide exactly the region under investigation.
		float d = pow(clamp(depth, 0.0, 1.0), 256.0);
		colorOut0 = vec4(d, d, d, 1.0);
		return;
	}
	if (debugMode == 2)
	{
		// Raw AO before the bilateral pass - the noise as the AO pass produces
		// it. Expected to look grainy; the question is whether it is STEADY.
		colorOut0 = vec4(ao, ao, ao, 1.0);
		return;
	}
	if (debugMode == 3)
	{
		// Confidence: how much of the kernel's weight actually survived. Dark
		// means the estimate rests on almost no valid taps, which is the
		// regime where it is numerically fragile.
		float conf = clamp(wsum / max(totalWeight, 1.0), 0.0, 1.0);
		colorOut0 = vec4(conf, conf, conf, 1.0);
		return;
	}
	if (debugMode == 4)
	{
		// Effective march radius in pixels, normalized against the cap, to
		// show how the depth-scaled radius actually falls off with distance.
		float r = clamp(marchPx / 96.0, 0.0, 1.0);
		colorOut0 = vec4(r, r, r, 1.0);
		return;
	}

	colorOut0 = vec4(aoFactor, aoFactor, aoFactor, 1.0);
}
)";

// Faro GTAO denoise + apply: bilateral blur of the raw AO factor, then
// multiplied into the scene colour.
//
// This pass is the whole point. Any sampling-based AO produces high-frequency
// noise by construction - each pixel takes a different rotation of the same
// small kernel, so each lands on a slightly different estimate. ReShade's MXAO
// accepts exactly the same noise and then removes it with two bilateral passes
// (PS_SpatialFilter1/2). This implementation had no filter at all, so the raw
// estimate went straight to the screen; and because the rotation pattern is
// locked to SCREEN space while the scene slides underneath it, a given surface
// point drew a different estimate every frame. That is what read as flicker.
//
// One pass here instead of MXAO's two: its first filter doubles as the
// upsample from a sub-resolution AO buffer, which does not apply while this
// runs at full resolution.
//
// Bilateral rather than a plain blur: taps are weighted by how close their
// depth is to the centre's, so occlusion cannot bleed across a silhouette. A
// plain box blur would halo every object edge.
static const std::string s_gtao_denoise_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;
layout(binding = 3) uniform sampler2D textureSceneColor;

float GtaoLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

void outputShader()
{
	vec3 sceneColor = texture(textureSceneColor, passUV).xyz;
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	if (intensity <= 0.0)
	{
		colorOut0 = vec4(sceneColor, 1.0);
		return;
	}

	// Tap pattern from MXAO's own blur_filter: two rings of four at half-pixel
	// offsets, chosen so taps spread across the 4x4 dither tile the AO pass
	// jitters on instead of repeatedly landing on pixels that share a
	// rotation.
	vec2 offsets[8] = vec2[8](
		vec2( 1.5, 0.5), vec2(-1.5,-0.5), vec2(-0.5, 1.5), vec2( 0.5,-1.5),
		vec2( 1.5, 2.5), vec2(-1.5,-2.5), vec2(-2.5, 1.5), vec2( 2.5,-1.5)
	);

	vec2 texel = 1.0 / max(outputResolution, vec2(1.0));
	float centerZ = GtaoLinearize(min(max(textureLod(textureDepth, passUV, 0.0).x, 0.0), 1.0));

	float aoSum = texture(textureSrc, passUV).x;
	float weightSum = 1.0;
	// Relative tolerance, for the same reason the AO pass uses one: this
	// linearized depth has no calibrated unit, so a fraction of the centre's
	// own depth is the only scale-invariant threshold available.
	float depthTolerance = max(centerZ * 0.05, 1e-4);

	for (int i = 0; i < 8; i++)
	{
		vec2 tapUV = passUV + offsets[i] * texel * 2.0;
		float tapZ = GtaoLinearize(min(max(textureLod(textureDepth, tapUV, 0.0).x, 0.0), 1.0));
		float w = clamp(1.0 - abs(tapZ - centerZ) / depthTolerance, 0.0, 1.0);
		aoSum += texture(textureSrc, tapUV).x * w;
		weightSum += w;
	}

	float ao = clamp(aoSum / max(weightSum, 1e-4), 0.0, 1.0);

	int debugMode = int(faroDebugParams.x + 0.5);
	if (debugMode >= 1 && debugMode <= 4)
	{
		// Pass the AO pass's debug output straight through, so what reaches
		// the screen is that raw intermediate and not this filter's view of
		// it.
		float raw = texture(textureSrc, passUV).x;
		colorOut0 = vec4(raw, raw, raw, 1.0);
		return;
	}
	if (debugMode == 5)
	{
		// The AO after this filter. Compared against mode 2 this shows
		// exactly how much the bilateral pass is actually removing.
		colorOut0 = vec4(ao, ao, ao, 1.0);
		return;
	}

	colorOut0 = vec4(sceneColor * ao, 1.0);
}
)";

// Faro motion estimation, pass 1 of 3: build the matching pyramid's top level.
//
// Packs luma in R and linearized depth in G. Depth is carried alongside luma
// on purpose: large regions of a game frame are flat in colour (sky, walls,
// shaded ground) where luma alone gives a block matcher nothing to lock onto,
// and depth usually still varies across those same regions. Matching on both
// is what keeps the search from drifting there.
//
// Written at a quarter of native resolution. Motion fields are smooth almost
// everywhere - this is estimating where the camera went, not per-pixel
// detail - so a quarter-res field upsamples cleanly and costs 1/16 of the
// work. The resolve's own variance clipping stays as the safety net for the
// fine detail this cannot resolve.
static const std::string s_motion_fill_shader_body = R"(
layout(binding = 2) uniform sampler2D textureDepth;

float FaroMotionLuma(vec3 c)
{
	return dot(c, vec3(0.299, 0.587, 0.114));
}

float FaroMotionLinearizeDepth(float d)
{
	// Same transform the AO chain uses, for the same reason: the far plane is
	// not knowable here, so this is only ever compared against itself.
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

// Raw depth into the bounded, comparable range the block cost works in. The
// linearized value is unbounded and front-loaded, so feeding it raw would let
// a single distant texel dominate the cost.
float FaroMotionCompressDepth(float rawDepth)
{
	rawDepth = min(max(rawDepth, 0.0), 1.0);
	return 1.0 - 1.0 / (1.0 + FaroMotionLinearizeDepth(rawDepth) * 0.1);
}

void outputShader()
{
	// Box average of the 2x2 that this texel covers in the source, so the
	// pyramid starts from a properly filtered image rather than point samples
	// that alias and would make the match jitter frame to frame.
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec3 c0 = texture(textureSrc, passUV + vec2(-0.5, -0.5) * texel).rgb;
	vec3 c1 = texture(textureSrc, passUV + vec2( 0.5, -0.5) * texel).rgb;
	vec3 c2 = texture(textureSrc, passUV + vec2(-0.5,  0.5) * texel).rgb;
	vec3 c3 = texture(textureSrc, passUV + vec2( 0.5,  0.5) * texel).rgb;
float luma = FaroMotionLuma((c0 + c1 + c2 + c3) * 0.25);

	// Depth gets the SAME 2x2 treatment as the luma above, and it has to.
	// Depth carries 0.5 of the block cost (FaroBlockCost), so a bad depth
	// sample is not a minor blemish - it is half the decision.
	//
	// Point-sampling it produced a field that was speckled even with the
	// camera completely still, which is the tell that it was never motion at
	// all: in grass and foliage, depth varies enormously WITHIN a single
	// pyramid texel (every blade sits at its own distance), so which one a
	// single tap happens to land on is arbitrary and re-rolled per texel.
	// Grass is also where depth legitimately differs from its neighbours, so
	// averaging is the honest reading of "how far away is this texel" rather
	// than a fudge to hide the noise.
	//
	// The same 2x2 footprint is reused rather than a fresh offset set so the
	// depth average describes exactly the block the luma average describes -
	// two channels of one texel, not two independent samples of the area.
	vec2 d0 = textureLod(textureDepth, passUV + vec2(-0.5, -0.5) * texel, 0.0).x;
	vec2 d1 = textureLod(textureDepth, passUV + vec2( 0.5, -0.5) * texel, 0.0).x;
	vec2 d2 = textureLod(textureDepth, passUV + vec2(-0.5,  0.5) * texel, 0.0).x;
	vec2 d3 = textureLod(textureDepth, passUV + vec2( 0.5,  0.5) * texel, 0.0).x;
	// Compressed per-tap and then averaged, NOT averaged raw then compressed.
	// The compressed curve is strongly nonlinear, so compressing the mean is
	// not the same value and would quietly bias every texel toward the near
	// plane.
	float depth = (FaroMotionCompressDepth(d0.x) + FaroMotionCompressDepth(d1.x) +
		FaroMotionCompressDepth(d2.x) + FaroMotionCompressDepth(d3.x)) * 0.25;

	colorOut0 = vec4(luma, depth, 0.0, 1.0);
}
)";

// Faro motion estimation, pass 2 of 3: one pyramid level down.
//
// Plain 2x2 box. A wider kernel would blur away the very features the matcher
// locks onto, and the levels exist to widen the search range, not to denoise.
static const std::string s_motion_downsample_shader_body = R"(
void outputShader()
{
	// Each pyramid level is bound through its OWN single-mip view, so the
	// descriptor covers exactly the subresource being read and an explicit
	// LOD of 0 always means "this level". A fragment shader has no correct
	// derivative when rendering into a differently sized target, so relying
	// on an implicit LOD here would be wrong.
	vec2 texel = 1.0 / max(outputResolution, vec2(1.0));
	vec2 s0 = textureLod(textureSrc, passUV + vec2(-0.25, -0.25) * texel, 0.0).rg;
	vec2 s1 = textureLod(textureSrc, passUV + vec2( 0.25, -0.25) * texel, 0.0).rg;
	vec2 s2 = textureLod(textureSrc, passUV + vec2(-0.25,  0.25) * texel, 0.0).rg;
	vec2 s3 = textureLod(textureSrc, passUV + vec2( 0.25,  0.25) * texel, 0.0).rg;
	colorOut0 = vec4((s0 + s1 + s2 + s3) * 0.25, 0.0, 1.0);
}
)";

// Faro motion estimation, pass 3 of 3: coarse-to-fine block matching.
//
// Written from scratch. The obvious references (JakobPCoder's
// ReshadeMotionEstimation, and TFAA which consumes it) are CC BY-NC 4.0,
// which cannot be combined with Cemu's MPL-2.0 - so none of their code is
// used here. The technique itself is not theirs to license: pyramidal block
// matching comes from 1980s video coding, and is the standard way to get a
// dense motion field without the engine's own motion vectors.
//
// Shape: at each level, take the coarser level's vector as the starting
// point, then refine it by testing a ring of candidates around that start and
// keeping whichever lowers the block cost, halving the ring radius each
// iteration. The coarsest level starts from rest and, because a texel there
// covers 16 native pixels, a short search still reaches a long way in native
// terms - which is the entire point of doing this on a pyramid instead of
// searching a huge radius at full resolution.
static const std::string s_motion_estimate_shader_body = R"(
layout(binding = 2) uniform sampler2D texturePrevGray;
layout(binding = 3) uniform sampler2D textureCoarseMotion;

// Sum of absolute differences over a 3x3 block. Luma carries most of the
// signal; depth is weighted lower but breaks ties where colour is flat.
float FaroBlockCost(vec2 baseUV, vec2 offset, vec2 texel)
{
	float cost = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			vec2 o = vec2(float(x), float(y)) * texel;
			vec2 a = textureLod(textureSrc, baseUV + o, 0.0).rg;
			vec2 b = textureLod(texturePrevGray, baseUV + offset + o, 0.0).rg;
			cost += abs(a.r - b.r) + abs(a.g - b.g) * 0.5;
		}
	}
return cost;
}

// Average depth of a 3x3 block on the PREVIOUS frame, for the disocclusion
// test below. Separate from FaroBlockCost because that one folds depth into
// the cost with a small weight; here depth is the subject of the question
// rather than a term in a score.
float FaroBlockDepthPrev(vec2 baseUV, vec2 offset, vec2 texel)
{
	float sum = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			sum += textureLod(texturePrevGray, baseUV + offset + vec2(float(x), float(y)) * texel, 0.0).g;
		}
	}
	return sum / 9.0;
}

// Same block on THIS frame, at the pixel doing the matching.
float FaroBlockDepthCur(vec2 baseUV, vec2 texel)
{
	float sum = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			sum += textureLod(textureSrc, baseUV + vec2(float(x), float(y)) * texel, 0.0).g;
		}
	}
	return sum / 9.0;
}

void outputShader()
{
	// No previous frame to match against yet - right after startup or a
	// resize. Reporting rest is correct rather than a fallback: there is no
	// motion to measure, and a garbage field here would be reprojected
	// against for a frame.
	if (faroMotionParams.x < 0.5)
	{
		colorOut0 = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	float searchRadius = max(faroMotionParams.y, 1.0);
	int iterations = int(faroMotionParams.z + 0.5);
	bool hasCoarse = faroMotionParams.w > 0.5;

	// outputResolution is this level's own size, so a texel here is one texel
	// at the level being matched.
	vec2 texel = 1.0 / max(outputResolution, vec2(1.0));

	// Start from the coarser level's answer. Sampled with a plain bilinear
	// fetch, which smooths the inherited field slightly - desirable, since a
	// coarse vector is a guess and a hard edge in it would be propagated
	// downward as a hard edge in the final field.
	// The coarser level's vector is both the starting point for the search
	// AND the answer to fall back on when this level cannot do better.
	//
	// That fallback is the important part. Rejecting a match used to mean
	// writing zero, which produced a SPARSE field: during a camera pan the
	// sky and smooth ground failed the texture test and were left at rest
	// while only detailed areas carried a vector. Sparse is worse than
	// useless for reprojection, because "zero" is not "unknown" to the
	// resolve - it would confidently fetch history from the wrong place
	// across most of the screen.
	//
	// Inheriting instead is what a pyramid is for. A texel at the coarsest
	// level spans 48 native pixels, so its block almost always contains
	// something to lock onto even when a fine-level block is featureless,
	// and broad camera motion is exactly the kind of signal it captures well.
	// So a level that cannot improve on its parent simply keeps the parent's
	// answer, and the field stays dense.
	vec2 inherited = hasCoarse ? texture(textureCoarseMotion, passUV).rg : vec2(0.0);
	vec2 motion = inherited;

	// How distinctive is this block at all? A block matcher can only locate
	// something that stands out; over a flat wall or an evenly lit patch of
	// ground every candidate scores about the same and the "best" one is
	// decided by noise. Measuring the local spread first, and deferring to
	// the parent when there is nothing to lock onto, is cheaper and more
	// honest than searching anyway and trusting the winner.
	vec2 fMin = vec2(1e9);
	vec2 fMax = vec2(-1e9);
	for (int fy = -1; fy <= 1; fy++)
	{
		for (int fx = -1; fx <= 1; fx++)
		{
			vec2 s = textureLod(textureSrc, passUV + vec2(float(fx), float(fy)) * texel, 0.0).rg;
			fMin = min(fMin, s);
			fMax = max(fMax, s);
		}
	}
	float feature = max(fMax.r - fMin.r, (fMax.g - fMin.g) * 0.5);
	if (feature < 0.02)
	{
		// Nothing to match on here - hand the parent's answer straight through
		// rather than claiming this pixel is at rest.
		colorOut0 = vec4(inherited, 0.0, 1.0);
		return;
	}

	// Cost of not moving at all, kept as the benchmark every candidate has to
	// beat by a clear margin further down.
	float restCost = FaroBlockCost(passUV, vec2(0.0), texel);
	float bestCost = FaroBlockCost(passUV, motion, texel);

	// Eight candidates on a ring, radius halving each pass. Costs far fewer
	// taps than scanning a full window and converges on the same place for
	// the smooth fields this is after.
	const vec2 ring[8] = vec2[8](
		vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
		vec2( 0.707,  0.707), vec2(-0.707,  0.707), vec2( 0.707, -0.707), vec2(-0.707, -0.707)
	);

	float radius = searchRadius;
	for (int it = 0; it < iterations; it++)
	{
		vec2 bestStep = vec2(0.0);
		for (int s = 0; s < 8; s++)
		{
			vec2 candidate = motion + ring[s] * radius * texel;
			float cost = FaroBlockCost(passUV, candidate, texel);
			if (cost < bestCost)
			{
				bestCost = cost;
				bestStep = ring[s] * radius * texel;
			}
		}
		motion += bestStep;
		radius *= 0.5;
	}

	// Reject a vector that points off-screen: there is no previous-frame data
	// out there, so whatever it matched against is the edge clamp repeating,
	// which is a false match by construction.
	vec2 target = passUV + motion;
	if (target.x < 0.0 || target.x > 1.0 || target.y < 0.0 || target.y > 1.0)
		motion = vec2(0.0);

	// A match has to beat standing still by a clear margin to be believed.
	//
	// Grass, foliage and tiled ground are highly repetitive, so some distant
	// block almost always scores marginally better than the correct one by
	// chance - which showed up as isolated coloured blocks scattered over an
	// otherwise still field. Those are the dangerous kind of error: a wrong
	// vector sends the resolve to fetch history from the wrong place and
	// smears, whereas a MISSED vector just falls back to the variance clip
	// that already handles unmatched motion today. Given that asymmetry,
	// being conservative here is the right trade, so a candidate that is only
	// marginally better than rest is discarded.
if (bestCost > restCost * 0.8)
		motion = inherited;

	// Disocclusion test. The block cost weighs depth at only 0.5 relative to
	// luma, which is right for ranking candidates but far too weak to notice
	// that a match landed on a DIFFERENT SURFACE.
	//
	// Silhouettes are exactly where the block matcher fails worst, and it
	// fails in the most confident-looking way possible: sliding along a
	// foliage edge, the block on the near side of the edge keeps landing on
	// foliage, so the luma cost barely rises and the search happily returns a
	// long vector that runs parallel to the boundary. Every pixel along that
	// edge independently reports the same wrong direction, which is why it
	// shows up as a solid band of one hue tracing the trees rather than as
	// scattered noise.
	//
	// Both sides are the same pyramid format and the same frame's scene
	// depth, so they are directly comparable: ask whether the previous frame's
	// block at the matched position sits at the same depth as this frame's
	// block here. If it does not, nothing at that offset can be the same
	// surface, no matter how well its luma scored.
	//
	// Relative tolerance, because the compressed depth saturates towards 1 at
	// range - an absolute one would reject legitimate motion in the distance
	// (where the whole band is within a rounding error of itself) while
	// accepting a large relative error up close. The floor keeps very near
	// geometry, where the compressed value is tiny, from having any motion at
	// all declared a disocclusion.
	//
	// Falls back to the parent's vector rather than to rest, for the same
	// reason the rest-cost gate does: a level that cannot vouch for its own
	// answer should not claim its pixel is stationary. Writing zero here is
	// what produced a sparse field before - see the inherited-vector comment
	// above.
	float depthCur = FaroBlockDepthCur(passUV, texel);
	float depthPrev = FaroBlockDepthPrev(passUV, motion, texel);
	float depthTolerance = max(depthCur * 0.35, 0.02);
	if (abs(depthPrev - depthCur) > depthTolerance)
		motion = inherited;

	colorOut0 = vec4(motion, 0.0, 1.0);
}
)";

std::string RendererOutputShader::BuildMotionFillShaderSource()
{
	return s_motion_fill_shader_body;
}

std::string RendererOutputShader::BuildMotionDownsampleShaderSource()
{
	return s_motion_downsample_shader_body;
}

std::string RendererOutputShader::BuildMotionEstimateShaderSource()
{
	return s_motion_estimate_shader_body;
}

std::string RendererOutputShader::BuildGtaoDenoiseShaderSource()
{
	// No per-quality variants: the filter's tap pattern is fixed and does not
	// depend on how many samples the AO pass took.
	return s_gtao_denoise_shader_body;
}

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
// Faro FSR1: only s_fsr1_rcas_shader_source reads this (x=RCAS sharpness in
// AMD's "stops" scale, y=RCAS denoise on/off, z/w reserved).
uniform vec4 faroFsrParams;
// Faro debug view: x = mode. See OutputUniformVariables::faroDebugParams.
uniform vec4 faroDebugParams;
// Faro motion estimation - see OutputUniformVariables::faroMotionParams.
uniform vec4 faroMotionParams;
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
    	s_gtao_denoise_shader = new RendererOutputShader(vertex_source, BuildGtaoDenoiseShaderSource());
    	s_motion_fill_shader = new RendererOutputShader(vertex_source, BuildMotionFillShaderSource());
    	s_motion_downsample_shader = new RendererOutputShader(vertex_source, BuildMotionDownsampleShaderSource());
    	s_motion_estimate_shader = new RendererOutputShader(vertex_source, BuildMotionEstimateShaderSource());
    	s_gi_shader = new RendererOutputShader(vertex_source, BuildGiShaderSource());
    	s_gi_blur_shader = new RendererOutputShader(vertex_source, BuildGiBlurShaderSource());
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
	delete s_gtao_denoise_shader;
	s_gtao_denoise_shader = nullptr;
	delete s_motion_fill_shader;
	s_motion_fill_shader = nullptr;
	delete s_motion_downsample_shader;
	s_motion_downsample_shader = nullptr;
	delete s_motion_estimate_shader;
	s_motion_estimate_shader = nullptr;
	delete s_gi_shader;
	s_gi_shader = nullptr;
	delete s_gi_blur_shader;
	s_gi_blur_shader = nullptr;

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