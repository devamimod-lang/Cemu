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

// Anubis: Lanczos2 downscale filter with a RATIO-AWARE footprint.
//
// Why the existing filters are wrong for downscaling. Bilinear reads 2x2 source
// texels, and the bicubic/Hermite filters above read a fixed 4x4 neighbourhood
// (4 bilinear taps) - in both cases the footprint is a constant number of
// SOURCE texels. When the render resolution is higher than the window, one
// output pixel covers ratio^2 source texels, so above a ratio of about 2 most
// of the rendered pixels are never read at all. Rendering at 300% and then
// throwing away two thirds of it is not supersampling, it is point sampling
// with extra steps, and the giveaway is aliasing that supersampling was
// supposed to remove. This is the same defect the code already documents for
// FSR1's EASU, which is why EASU is not offered as a downscale option.
//
// The fix is a kernel whose width follows the ratio, so every source texel
// inside the output pixel's footprint contributes.
//
// Lanczos2 rather than a box: a 2x2 box is the cheapest possible resolve, but
// it does not cut high frequencies fast enough (it still aliases) and it
// attenuates low frequencies, which reads as mush. Lanczos2 - sinc windowed by
// sinc, support 2 - has a far sharper frequency response for the same support
// and is the standard choice for a quality resolve. It can ring slightly on
// very high contrast edges; that is the known trade and the reason this is an
// added option rather than a replacement for the existing filters.
//
// Cost is bounded by sampling with BILINEAR taps spaced two source texels
// apart, so each fetch already averages its own neighbourhood and the tap grid
// stays small: 3x3 fetches at ratio 1, 5x5 at ratio 2, 7x7 at ratio 3, capped
// there. The maths is textbook signal processing, implemented from the
// definition - no third-party code.
// Anubis: the filter used when upscaling a game texture on the GPU.
//
// Catmull-Rom, which is the only reason doing this is worth anything: the
// hardware already magnifies with a 2x2 bilinear tent at sample time, so
// reproducing bilinear here would cost a pass and change nothing. Catmull-Rom
// is a wider, mildly sharpening reconstruction over 4x4 taps.
//
// Alpha is filtered alongside colour. Cut-out textures - foliage, smoke - carry
// their shape in alpha, and filtering only RGB would leave the silhouette at
// the original resolution while the colour got sharper.
const std::string RendererOutputShader::s_texture_upscale_shader_source =
R"(
float AnubisTexCatmullRom(float x)
{
	x = abs(x);
	if (x < 1.0)
		return 1.5 * x * x * x - 2.5 * x * x + 1.0;
	if (x < 2.0)
		return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
	return 0.0;
}

void outputShader()
{
	vec2 srcRes = max(textureSrcResolution, vec2(1.0));
	vec2 st = passUV * srcRes - 0.5;
	vec2 f = fract(st);
	vec2 base = floor(st);

	float wx[4];
	float wy[4];
	for (int k = 0; k < 4; k++)
	{
		wx[k] = AnubisTexCatmullRom(float(k - 1) - f.x);
		wy[k] = AnubisTexCatmullRom(float(k - 1) - f.y);
	}

	vec4 acc = vec4(0.0);
	float wsum = 0.0;
	for (int ky = 0; ky < 4; ky++)
	{
		for (int kx = 0; kx < 4; kx++)
		{
			float w = wx[kx] * wy[ky];
			// Clamped to the edge so the border does not pull in wrapped or
			// undefined texels, which would show as a seam on every tile.
			vec2 tapUV = (base + vec2(float(kx - 1), float(ky - 1)) + 0.5) / srcRes;
			tapUV = clamp(tapUV, vec2(0.0), vec2(1.0));
			acc += texture(textureSrc, tapUV) * w;
			wsum += w;
		}
	}

	// Catmull-Rom weights are signed, so the result overshoots on a hard edge.
	// Clamping is what stops that wrapping round into the opposite value.
	vec4 result = wsum != 0.0 ? acc / wsum : texture(textureSrc, passUV);
	colorOut0 = clamp(result, vec4(0.0), vec4(1.0));
}
)";

const std::string RendererOutputShader::s_lanczos_shader_source =
R"(
float AnubisLanczos2(float x)
{
	x = abs(x);
	if (x < 1e-5)
		return 1.0;
	if (x >= 2.0)
		return 0.0;
	float px = 3.14159265359 * x;
	// sinc(x) * sinc(x/2), the Lanczos a=2 window.
	return (sin(px) / px) * (sin(px * 0.5) / (px * 0.5));
}

void outputShader()
{
	vec2 srcRes = max(textureSrcResolution, vec2(1.0));
	vec2 dstRes = max(outputResolution, vec2(1.0));
	// Per-axis, because a non-uniform aspect correction can downscale one axis
	// and not the other.
	vec2 ratio = max(srcRes / dstRes, vec2(1.0));

	// A FIXED number of taps with an ADAPTIVE step, not the other way round.
	//
	// The first version fixed the step at 2 source texels and let the tap
	// count follow the ratio. That is wrong at low ratios: at 1800p -> 1080p
	// (ratio 1.67) the taps land at 0, 1.2 and 2.4 output pixels, and 2.4 is
	// outside Lanczos2's support of 2.0, so it weighs zero and drops out. Only
	// three taps per axis survive, which samples the KERNEL too coarsely to
	// represent it - a different artifact from the one being fixed.
	//
	// Deriving the step from the support instead keeps the kernel properly
	// sampled at every ratio, keeps the cost constant (7x7 fetches), and still
	// scales the footprint with the ratio, which was the whole point. The
	// 3.5 divisor places the outermost tap at 1.71 output pixels - inside the
	// support, so no tap is wasted on a zero weight.
	const int TAPS_PER_SIDE = 3;
	// Named tapStep, not step: step() is a GLSL built-in and shadowing it here
	// would compile but read as a bug waiting to happen.
	vec2 tapStep = (2.0 * ratio) / 3.5;

	vec2 centerTexel = passUV * srcRes;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	for (int y = -TAPS_PER_SIDE; y <= TAPS_PER_SIDE; y++)
	{
		// Offsets are in source texels; the kernel is evaluated in OUTPUT
		// pixels. Dividing by the ratio to cross between the two is what makes
		// the footprint follow the scale factor.
		float offY = float(y) * tapStep.y;
		float wy = AnubisLanczos2(offY / ratio.y);
		for (int x = -TAPS_PER_SIDE; x <= TAPS_PER_SIDE; x++)
		{
			float offX = float(x) * tapStep.x;
			float wx = AnubisLanczos2(offX / ratio.x);
			vec2 tapUV = (centerTexel + vec2(offX, offY)) / srcRes;
			float w = wx * wy;
			sum += texture(textureSrc, tapUV).rgb * w;
			wsum += w;
		}
	}
	// Lanczos weights are signed, so the sum can be negative on a high
	// contrast edge (this is the ringing). Clamping here keeps the result in
	// range, which also matters because FSR1's RCAS returns NaN for negative
	// input if this ever feeds it.
	vec3 result = wsum > 1e-5 ? sum / wsum : texture(textureSrc, passUV).rgb;
	colorOut0 = vec4(clamp(result, vec3(0.0), vec3(1.0)), 1.0);
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

     // Debug view: how much of RCAS's maximum sharpening lobe is actually in
     // play, as a fraction. Built because the sharpness slider was reported as
     // making no visible difference between its extremes, and the plumbing
     // checked out - the uniform reaches this shader and the maths matches
     // AMD's ffx_fsr1.h. That leaves "there is nothing here to sharpen" as the
     // hypothesis, and this is the only way to see it rather than argue it.
     //
     // Black means the 5-tap cross found no local contrast, so scaling the
     // lobe by the slider scales zero and the control genuinely cannot do
     // anything. Bright means RCAS has signal and the slider must have an
     // effect, which would point the search somewhere else entirely.
     //
     // Read it with SSAO off: the GTAO pass runs after RCAS and folds
     // occlusion into whatever colour this pass produced, including this view.
     if (int(faroDebugParams.x + 0.5) == 6)
     {
         float lobeFraction = clamp(-lobe / FSR_RCAS_LIMIT, 0.0, 1.0);
         colorOut0 = vec4(vec3(lobeFraction), 1.0);
         return;
     }
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

	// Faro FSR1: RCAS sharpness/denoise - see OutputUniformVariables for why
	// these are filled here instead of per dispatch. Sharpness is stored as
	// hundredths of a stop (see CemuConfig::fsr_rcas_sharpness) and converted
	// to the stops value AMD's own FsrRcasCon expects.
	vars.faroFsrParams[0] = (float)std::clamp(GetConfig().fsr_rcas_sharpness.GetValue(),
		CemuConfig::kFsrRcasSharpnessMin, CemuConfig::kFsrRcasSharpnessMax) / 100.0f;
	vars.faroFsrParams[1] = GetConfig().fsr_rcas_denoise ? 1.0f : 0.0f;
	// z/w are unused: the restored resolve (see s_taa_resolve_shader_source)
	// takes no parameters. Left zero rather than removed from the struct so the
	// std140 block layout is unchanged.
	vars.faroFsrParams[2] = 0.0f;
	vars.faroFsrParams[3] = 0.0f;

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
RendererOutputShader* RendererOutputShader::s_texture_upscale_shader;
RendererOutputShader* RendererOutputShader::s_a4k_restore_pass[4];
RendererOutputShader* RendererOutputShader::s_fsrcnn_pass[31];
RendererOutputShader* RendererOutputShader::s_anubis_water_ssr_shader;
RendererOutputShader* RendererOutputShader::s_lanczos_shader;
RendererOutputShader* RendererOutputShader::s_lanczos_shader_ud;

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
	// float, not vec2: .x of a depth fetch is a scalar. Declaring these vec2
	// made this shader fail to compile outright ("cannot convert from float to
	// 2-component vector of float"), which is why the motion field never
	// existed - the whole pass was silently absent, and the only trace was one
	// GLSL error at startup.
	float d0 = textureLod(textureDepth, passUV + vec2(-0.5, -0.5) * texel, 0.0).x;
	float d1 = textureLod(textureDepth, passUV + vec2( 0.5, -0.5) * texel, 0.0).x;
	float d2 = textureLod(textureDepth, passUV + vec2(-0.5,  0.5) * texel, 0.0).x;
	float d3 = textureLod(textureDepth, passUV + vec2( 0.5,  0.5) * texel, 0.0).x;
	// Compressed per-tap and then averaged, NOT averaged raw then compressed.
	// The compressed curve is strongly nonlinear, so compressing the mean is
	// not the same value and would quietly bias every texel toward the near
	// plane.
	float depth = (FaroMotionCompressDepth(d0) + FaroMotionCompressDepth(d1) +
		FaroMotionCompressDepth(d2) + FaroMotionCompressDepth(d3)) * 0.25;

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
// Point-sampled depth, alongside the bilinear texture() reads elsewhere.
//
// The depth sampler is VK_FILTER_LINEAR, so a plain texture() fetch across a
// SILHOUETTE blends the character's depth with the background's and returns a
// value that exists nowhere in the scene. The gather then treats that invented
// in-between depth as a real nearby surface and collects bounce light off it -
// which is what produced patches around the character.
//
// Technique from Tomasz Stachowiak's depth-buffer raymarching
// (dual Apache-2.0 / MIT): sample the depth buffer BOTH point-sampled and
// bilinear-filtered, because each one's artifacts cancel the other's. Point
// sampling alone reintroduces the quantization stepping that bilinear exists to
// smooth out, so neither is used on its own - see the gather loop for how the
// two are combined.
float GiPointDepth(vec2 uv, vec2 res)
{
	ivec2 p = ivec2(uv * res);
	return min(max(texelFetch(textureDepth, clamp(p, ivec2(0), ivec2(res) - ivec2(1)), 0).x, 0.0), 1.0);
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
	// Gather radius, with an optional multiplier in faroBitmaskAoParams.z.
	//
	// Note the asymmetry with the SSAO pass, which scales its radius by depth
	// while this one is a flat pixel count. That flat count is why occlusion
	// reads at character scale but not at building scale: 48 pixels covers the
	// folds of a nearby character and only a few metres of a distant wall, so
	// the large recesses, alcoves and gaps between pillars that give a
	// structure its shading are never sampled.
	//
	// The multiplier defaults to 1.0 so the GI - which this radius also drives,
	// and which is reported as working - is untouched unless the setting is
	// raised. A knob rather than a new hard-coded number, because the right
	// radius depends on the scene and guessing it has already cost this project
	// several rounds.
	float aoRadiusMul = max(faroBitmaskAoParams.z, 0.25);
	float marchPx = max(faroGtaoParams.z, 1.0) * aoRadiusMul;
	// Ordered 4x4 dither, not white noise.
	//
	// This used to be GiHash(gl_FragCoord.xy), a per-pixel hash. That gives
	// every pixel an unrelated set of gather directions, so neighbouring
	// pixels disagree wildly and the blur below turns that high-frequency
	// disagreement into low-frequency BLOTCHES - which is exactly how it was
	// reported, as patches in the character's shadows.
	//
	// An ordered dither fixes it by construction: each 4x4 block of pixels
	// covers 16 evenly spaced rotations, so a denoise averaging over that
	// neighbourhood reconstructs the full angular integral instead of whatever
	// biased subset white noise happened to pick. This is the same scheme the
	// SSAO pass in this file already uses, and SSAO is the one that does not
	// blotch.
	float ditherJitter = dot(floor(mod(gl_FragCoord.xy, 4.0) + 0.1), vec2(0.0625, 0.25)) + 0.0625;
	float rot = ditherJitter * 3.14159265;
	vec3 bounce = vec3(0.0);
	float wsum = 0.0;
	// Ambient occlusion straight out of the visibility bitmask, which the paper
	// defines as AO += 1 - countbits(b_i)/Nb once a direction is done.
	//
	// Nearly free: the bit field is already being built for the indirect light,
	// so this is one popcount per direction. And it is a genuinely different
	// estimator from the horizon-based SSAO - the paper notes that family
	// cannot let light pass behind thin surfaces, which is exactly the case
	// where the two will disagree.
	float aoSum = 0.0;
	// Sum of the per-sample depth-similarity weights, independent of whether
	// the horizon rose. Used only to scale the divisor floor - see below.
	float wPotential = 0.0;
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
		// Visibility bitmask, replacing the single running horizon angle.
		//
		// Therrien, Levesque & Gilet, "Screen space indirect lighting with
		// visibility bitmask" (The Visual Computer 39, 2023; arXiv:2301.11376).
		// Implemented from the paper, not from any shader source.
		//
		// The problem it solves is the one a horizon angle cannot: a horizon
		// records only how high the highest occluder reached, so once it is
		// high, nothing behind it ever contributes again. A thin railing or a
		// tree trunk therefore killed all the light behind it rather than just
		// its own silhouette. The paper states the limitation directly - this
		// family of methods "cannot properly simulate light passing behind
		// thin surfaces".
		//
		// Instead, 32 bits record WHICH sectors of this hemisphere slice are
		// occluded. An occluder claims only its own sectors and light passes
		// through the rest. 32 is the paper's own choice: the count at which
		// artifacts become almost invisible, and it fits exactly in one uint.
		uint occluded = 0u;
		for (int s = 1; s <= FARO_GI_STEPS; s++)
		{
			// Jittered, non-uniformly spaced steps - both from Stachowiak's
			// parameter set (jitter, linear_march_exponent).
			//
			// Uniform un-jittered steps put every pixel's samples at the same
			// radii, so the gather produces concentric rings. Offsetting the
			// first step by the same ordered dither that rotates the kernel
			// spreads those radii across the 4x4 block, and the exponent packs
			// more steps near the centre, where contact-range bounce light
			// actually lives.
			float t = (float(s) - 1.0 + ditherJitter) / float(FARO_GI_STEPS);
			t = pow(clamp(t, 0.0, 1.0), 1.5);
			vec2 suv = passUV + dirPx * (t * marchPx) / outRes;
			float sdLinear = min(max(texture(textureDepth, suv).x, 0.0), 1.0);
			float sdPoint = GiPointDepth(suv, outRes);
			// Both readings have to agree this sample sits in FRONT of the
			// centre before it is allowed to contribute. On a silhouette the
			// bilinear read is a blend of character and background, so it
			// disagrees with the point read and the sample is dropped instead
			// of being treated as a surface that is not there. Inside a
			// surface the two agree and nothing changes, which is how the
			// bilinear smoothing is kept where it helps.
			float szLinear = GiLinearize(sdLinear);
			float szPoint = GiLinearize(sdPoint);
			if (szLinear >= viewZ || szPoint >= viewZ)
				continue;
			// The point reading is the one that exists in the scene, so the
			// geometry is reconstructed from it.
			float sz = szPoint;
			vec3 samplePos = GiReconstructPos(suv, sz, aspect);
			vec3 toSample = samplePos - centerPos;
			float dist = length(toSample);
			if (dist < 1e-4)
				continue;
			float sinAngle = dot(toSample / dist, normal);

			// The sample's BACK face. This is the piece that lets light pass
			// behind a thin object: the paper treats every sample as local
			// geometry of thickness t, occluding between a front and a back
			// angle rather than everything above one horizon.
			//
			// Taken as the same pixel pushed further from the camera instead of
			// the paper's s_b = s_f - (p/||p||)t. Same meaning, and it cannot
			// get the sign of the view direction wrong - which would invert the
			// whole effect silently.
			//
			// Thickness scales with depth (an option the paper notes) because a
			// fixed world-space value tuned for a nearby wall is enormous
			// relative to geometry far across a field.
			float thickness = max(viewZ * 0.02, 1e-4);
			vec3 samplePosBack = GiReconstructPos(suv, sz + thickness, aspect);
			vec3 toBack = samplePosBack - centerPos;
			float distBack = max(length(toBack), 1e-4);
			float sinBack = dot(toBack / distBack, normal);

			// Angles to sector indices. theta spans [-pi/2, pi/2] across the
			// slice, so (theta + pi/2)/pi normalises to [0,1] before scaling to
			// the 32 sectors.
			//
			// NOTE on the paper's printed equation for the bit WIDTH: it reads
			// b = ceil((theta_max - theta_min + pi/2)/pi * Nb), which gives 16
			// bits for a zero-width span and cannot be meant literally. The
			// semantically correct width is used here; the discrepancy is
			// recorded rather than quietly "corrected".
			const float FARO_GI_PI = 3.14159265;
			const int FARO_GI_SECTORS = 32;
			float thetaFront = asin(clamp(sinAngle, -1.0, 1.0));
			float thetaBack = asin(clamp(sinBack, -1.0, 1.0));
			float thetaMin = min(thetaFront, thetaBack);
			float thetaMax = max(thetaFront, thetaBack);

			int sectorStart = int(floor((thetaMin + FARO_GI_PI * 0.5) / FARO_GI_PI * float(FARO_GI_SECTORS)));
			int sectorWidth = int(ceil((thetaMax - thetaMin) / FARO_GI_PI * float(FARO_GI_SECTORS)));
			sectorStart = clamp(sectorStart, 0, FARO_GI_SECTORS - 1);
			// At least one sector. A sample thinner than a sector still blocks
			// something, and rounding it to zero would let it contribute light
			// while occluding nothing at all.
			sectorWidth = clamp(sectorWidth, 1, FARO_GI_SECTORS - sectorStart);

			// (2^width - 1) << start, with the full-width case special-cased
			// because 1u << 32 is undefined and the width can legitimately
			// reach 32.
			uint sampleBits = (sectorWidth >= FARO_GI_SECTORS) ? 0xFFFFFFFFu
				: (((1u << uint(sectorWidth)) - 1u) << uint(sectorStart));

			// Only sectors this sample is the FIRST to occlude carry light -
			// the paper's countbits(b_j & ~b_i). This replaces the old
			// rise/riseWeight test: both stop stacked occluders being counted
			// twice, but this does it per sector instead of discarding the
			// whole sample, which is exactly why light can now get past a thin
			// occluder.
			uint newBits = sampleBits & ~occluded;
			occluded |= sampleBits;

			float w = clamp(1.0 - abs(sz - viewZ) / (relScale * 3.0), 0.0, 1.0);
			if (newBits == 0u)
			{
				// Still counts toward the divisor floor: the sample was valid
				// and simply added no new visibility.
				wPotential += w;
				continue;
			}
			float visFrac = float(bitCount(newBits)) / float(FARO_GI_SECTORS);

			// Receiver cosine (n_p . l_j). The emitter cosine (n_j . -l_j) in
			// the paper's formula is omitted because it needs the SAMPLE's own
			// normal and this pass has only depth. The horizon version omitted
			// it too, so this is not a regression - just not the full formula.
			float ndotl = max(sinAngle, 0.0);
			float delta = visFrac * ndotl * w;
			bounce += texture(textureSrc, suv).rgb * delta;
			wsum += delta;
			// Accumulated separately to give the divisor floor below something
			// at the RIGHT SCALE to compare against. wsum sums
			// rise*riseWeight*w, and rise is a small difference of sines, so
			// wsum can never approach DIRS*STEPS the way the SSAO pass's own
			// weight sum can. Flooring against that count crushed the result
			// by several times over - the blotches went away and so did most
			// of the effect.
		}
		// The direction is finished, so its bit field is complete: the share of
		// the 32 sectors still unoccluded is this direction's visibility. This
		// is the paper's AO += 1 - countbits(b_i)/Nb, accumulated here and
		// averaged over the directions at the end.
		aoSum += 1.0 - float(bitCount(occluded)) / 32.0;
	}
	// Divisor floor, the second half of the blotch fix.
	//
	// bounce/wsum alone is a weighted mean, so it stays in range - but in a
	// shadowed area almost every sample fails the gates above (behind the
	// surface, or below the horizon), leaving wsum tiny. Dividing by a tiny
	// weight lets the one or two samples that did get through decide the whole
	// pixel, and that is why the patches showed up in shadow specifically.
	//
	// With the floor, a pixel whose evidence is scarce is pulled smoothly
	// towards "no indirect light" instead of towards whatever single sample
	// survived. Where samples are plentiful this changes nothing. The SSAO
	// pass carries the same floor for the same reason - see its own comment
	// about what normalizing by wsum alone did.
	//
	// The floor is a fraction of wPotential, NOT of DIRS*STEPS. The first
	// attempt used the sample count, copying SSAO - but SSAO's weight sum is
	// built from w alone and really can approach that count, while this one is
	// built from rise*riseWeight*w and cannot. Flooring against the count
	// divided every pixel by a constant far larger than its own weight sum,
	// which fixed the blotches and simultaneously crushed the effect to almost
	// nothing. Comparing like with like keeps both properties.
	vec3 giColor = bounce / max(wsum, 0.15 * wPotential + 1e-4);

	// AO travels in alpha, which was previously a constant 1.0.
	//
	// The same pass, the same bit field, no extra target and no extra march -
	// the occlusion was already computed as a side effect of deciding which
	// sectors could carry light. The blur pass filters it with the same
	// bilateral weights as the colour and applies it separately from the GI, so
	// the two have independent strengths.
	float ao = clamp(aoSum / float(FARO_GI_DIRS), 0.0, 1.0);
	colorOut0 = vec4(giColor, ao);
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
	// Bitmask AO, arriving in the GI target's alpha channel.
	float aoSum = 0.0;
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
			vec4 s = texture(textureSrc, suv);
			sum += s.rgb * w;
			// Alpha carries the bitmask AO. Filtered with the SAME bilateral
			// weights as the colour, because it came from the same marches and
			// is noisy in the same places - running it through a second,
			// differently-weighted filter would make the occlusion and the
			// light disagree about where edges are.
			aoSum += s.a * w;
			wsum += w;
		}
	}
	vec4 center = texture(textureSrc, passUV);
	vec3 blurredGi = wsum > 1e-4 ? sum / wsum : center.rgb;
	float blurredAo = wsum > 1e-4 ? aoSum / wsum : center.a;
	float intensity = clamp(faroGtaoParams.x, 0.0, 1.0);
	float distFade = 1.0 - smoothstep(30.0, 120.0, centerZ);
	vec3 original = texture(textureOriginal, passUV).rgb;

	// AO darkens the original; GI adds to it. Two separate strengths, which is
	// the entire point: this AO and the SSAO can both be enabled, and neither
	// one's slider moves the other. An intensity of zero leaves the frame
	// exactly as it was.
	float aoIntensity = clamp(faroBitmaskAoParams.x, 0.0, 1.0);

	// The AO gets its OWN distance fade, not the GI's.
	//
	// Sharing the GI's fade is what limited this to whatever was closest: it
	// reaches zero at 120 linearized units, and buildings and ruins usually sit
	// further out than a character does. The reported symptom - clearly visible
	// on Link, barely there on structures - is that fade, not the estimator.
	//
	// Separate because the two effects legitimately want different reach:
	// bounce light is a local phenomenon and gets noisy when stretched, while
	// occlusion stays meaningful across a whole building. faroBitmaskAoParams.y
	// carries the far distance, and the fade begins at half of it.
	float aoFar = max(faroBitmaskAoParams.y, 1.0);
	float aoDistFade = 1.0 - smoothstep(aoFar * 0.5, aoFar, centerZ);
	float aoFactor = mix(1.0, blurredAo, aoIntensity * aoDistFade);
	vec3 result = original * aoFactor + blurredGi * intensity * distFade * 0.6;
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
// Anubis bitmask AO: x = intensity. Independent of the GI intensity in
// faroGtaoParams.x, so this AO and the SSAO can both be on without either
// dictating the other.
uniform vec4 faroBitmaskAoParams;
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
    	s_lanczos_shader = new RendererOutputShader(vertex_source, s_lanczos_shader_source);
    	s_lanczos_shader_ud = new RendererOutputShader(vertex_source_ud, s_lanczos_shader_source);
    	s_texture_upscale_shader = new RendererOutputShader(vertex_source, s_texture_upscale_shader_source);
	// Anime4K Restore CNN. Four passes, run back to back on a texture before it
	// is upscaled: the network restores detail at the ORIGINAL resolution, which
	// is both what it was trained for and far cheaper than running it on the
	// enlarged image.
	s_a4k_restore_pass[0] = new RendererOutputShader(vertex_source, s_a4k_restore_pass0_source);
	s_a4k_restore_pass[1] = new RendererOutputShader(vertex_source, s_a4k_restore_pass1_source);
	s_a4k_restore_pass[2] = new RendererOutputShader(vertex_source, s_a4k_restore_pass2_source);
	s_a4k_restore_pass[3] = new RendererOutputShader(vertex_source, s_a4k_restore_pass3_source);
	// FSRCNN-small x2, instantiated in EXECUTION order: the schedule in
	// AnubisRunFsrcnnChain indexes this array positionally, so reordering
	// these lines would silently rewire the network.
	s_fsrcnn_pass[0] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_0_source);
	s_fsrcnn_pass[1] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_1_source);
	s_fsrcnn_pass[2] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_2_source);
	s_fsrcnn_pass[3] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_3_source);
	s_fsrcnn_pass[4] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_4_source);
	s_fsrcnn_pass[5] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_5_source);
	s_fsrcnn_pass[6] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_6_source);
	s_fsrcnn_pass[7] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_7_source);
	s_fsrcnn_pass[8] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a0_source);
	s_fsrcnn_pass[9] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a1_source);
	s_fsrcnn_pass[10] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a2_source);
	s_fsrcnn_pass[11] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a3_source);
	s_fsrcnn_pass[12] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a0_source);
	s_fsrcnn_pass[13] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a1_source);
	s_fsrcnn_pass[14] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a2_source);
	s_fsrcnn_pass[15] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a3_source);
	s_fsrcnn_pass[16] = new RendererOutputShader(vertex_source, s_fsrcnn_f3_t0_source);
	s_fsrcnn_pass[17] = new RendererOutputShader(vertex_source, s_fsrcnn_f3_t1_source);
	s_fsrcnn_pass[18] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_0_source);
	s_fsrcnn_pass[19] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_1_source);
	s_fsrcnn_pass[20] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_2_source);
	s_fsrcnn_pass[21] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_3_source);
	s_fsrcnn_pass[22] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_4_source);
	s_fsrcnn_pass[23] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_5_source);
	s_fsrcnn_pass[24] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_6_source);
	s_fsrcnn_pass[25] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_7_source);
	s_fsrcnn_pass[26] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a0_source);
	s_fsrcnn_pass[27] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a1_source);
	s_fsrcnn_pass[28] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a2_source);
	s_fsrcnn_pass[29] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a3_source);
	s_fsrcnn_pass[30] = new RendererOutputShader(vertex_source, s_fsrcnn_recon_source);
	s_anubis_water_ssr_shader = new RendererOutputShader(vertex_source, s_anubis_water_ssr_source);
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
    	s_lanczos_shader = new RendererOutputShader(vertex_source, s_lanczos_shader_source);
    	s_lanczos_shader_ud = new RendererOutputShader(vertex_source_ud, s_lanczos_shader_source);
    	s_texture_upscale_shader = new RendererOutputShader(vertex_source, s_texture_upscale_shader_source);

    	// Anime4K restore, FSRCNN and the water reflection march. These were
    	// created in the OpenGL branch only, which meant that on Vulkan - the
    	// backend actually in use - every one of them was a null pointer and
    	// silently did nothing. The neural texture reconstruction was enabled and
    	// tested in that state and its lack of effect was wrongly attributed to
    	// the thrash guard.
    	//
    	// The cause was a scripted insertion anchored on text that appears in
    	// both branches, landing in whichever came first. Worth noting because
    	// the same anchor put s_texture_upscale_shader in BOTH by luck, which is
    	// precisely why texture upscaling worked and these did not.
    	s_a4k_restore_pass[0] = new RendererOutputShader(vertex_source, s_a4k_restore_pass0_source);
    	s_a4k_restore_pass[1] = new RendererOutputShader(vertex_source, s_a4k_restore_pass1_source);
    	s_a4k_restore_pass[2] = new RendererOutputShader(vertex_source, s_a4k_restore_pass2_source);
    	s_a4k_restore_pass[3] = new RendererOutputShader(vertex_source, s_a4k_restore_pass3_source);
    	s_fsrcnn_pass[0] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_0_source);
    	s_fsrcnn_pass[1] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_1_source);
    	s_fsrcnn_pass[2] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_2_source);
    	s_fsrcnn_pass[3] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_3_source);
    	s_fsrcnn_pass[4] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_4_source);
    	s_fsrcnn_pass[5] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_5_source);
    	s_fsrcnn_pass[6] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_6_source);
    	s_fsrcnn_pass[7] = new RendererOutputShader(vertex_source, s_fsrcnn_f1_7_source);
    	s_fsrcnn_pass[8] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a0_source);
    	s_fsrcnn_pass[9] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a1_source);
    	s_fsrcnn_pass[10] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a2_source);
    	s_fsrcnn_pass[11] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t0_a3_source);
    	s_fsrcnn_pass[12] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a0_source);
    	s_fsrcnn_pass[13] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a1_source);
    	s_fsrcnn_pass[14] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a2_source);
    	s_fsrcnn_pass[15] = new RendererOutputShader(vertex_source, s_fsrcnn_f2_t1_a3_source);
    	s_fsrcnn_pass[16] = new RendererOutputShader(vertex_source, s_fsrcnn_f3_t0_source);
    	s_fsrcnn_pass[17] = new RendererOutputShader(vertex_source, s_fsrcnn_f3_t1_source);
    	s_fsrcnn_pass[18] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_0_source);
    	s_fsrcnn_pass[19] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_1_source);
    	s_fsrcnn_pass[20] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_2_source);
    	s_fsrcnn_pass[21] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_3_source);
    	s_fsrcnn_pass[22] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_4_source);
    	s_fsrcnn_pass[23] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_5_source);
    	s_fsrcnn_pass[24] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_6_source);
    	s_fsrcnn_pass[25] = new RendererOutputShader(vertex_source, s_fsrcnn_f4_7_source);
    	s_fsrcnn_pass[26] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a0_source);
    	s_fsrcnn_pass[27] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a1_source);
    	s_fsrcnn_pass[28] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a2_source);
    	s_fsrcnn_pass[29] = new RendererOutputShader(vertex_source, s_fsrcnn_f5_a3_source);
    	s_fsrcnn_pass[30] = new RendererOutputShader(vertex_source, s_fsrcnn_recon_source);
    	s_anubis_water_ssr_shader = new RendererOutputShader(vertex_source, s_anubis_water_ssr_source);

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
	delete s_lanczos_shader;
	delete s_lanczos_shader_ud;
	delete s_texture_upscale_shader;
	for (int i = 0; i < 4; i++)
		delete s_a4k_restore_pass[i];
	for (int i = 0; i < 31; i++)
		delete s_fsrcnn_pass[i];
	delete s_anubis_water_ssr_shader;

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
	s_lanczos_shader = nullptr;
	s_lanczos_shader_ud = nullptr;
	s_texture_upscale_shader = nullptr;
	for (int i = 0; i < 4; i++)
		s_a4k_restore_pass[i] = nullptr;
	for (int i = 0; i < 31; i++)
		s_fsrcnn_pass[i] = nullptr;
	s_anubis_water_ssr_shader = nullptr;
	s_fsr1_easu_shader = nullptr;
	s_fsr1_easu_shader_ud = nullptr;
	s_fsr1_rcas_shader = nullptr;
	s_fxaa_shader = nullptr;
	s_smaa_neighborhood_shader = nullptr;
	s_taa_resolve_shader = nullptr;
}// GENERATED by gen_a4k.py from Anime4K_Restore_CNN_S.glsl - do not hand-edit.
// Regenerate rather than patch, so the weights always match upstream.
//
// Upstream licence, reproduced as the MIT terms require:
// MIT License
// Copyright (c) 2019-2021 bloc97
// All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Pass 0: Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x3
// reads MAIN -> writes conv2d_tf
const std::string RendererOutputShader::s_a4k_restore_pass0_source =
R"(
#define go_0(x_off, y_off) (texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel))
void outputShader()
{
	vec2 anubisTexel = 1.0 / max(textureSrcResolution, vec2(1.0));
    vec4 result = mat4(-0.19288683, -0.21397883, 0.111997396, -0.04791413, -0.26682988, -0.06144587, -0.03601853, -0.16693151, 0.038494494, -0.16651472, 0.147657, -0.083003886, 0.0, 0.0, 0.0, 0.0) * go_0(-1.0, -1.0);
    result += mat4(-0.14286195, 0.08746566, -0.40107322, 0.12390977, -0.33392772, -0.18703035, -0.21326795, 0.04780781, -0.15155545, -0.0010025925, -0.1554875, -0.10676251, 0.0, 0.0, 0.0, 0.0) * go_0(-1.0, 0.0);
    result += mat4(0.28095165, 0.022872915, -0.21342312, -0.29982176, 0.025937587, -0.055012174, -0.33779636, 0.0015666655, 0.076416336, 0.06656033, -0.1557806, 0.1078894, 0.0, 0.0, 0.0, 0.0) * go_0(-1.0, 1.0);
    result += mat4(-0.31584853, 0.07527119, 0.30713862, -0.34014285, -0.50103146, -0.07217874, 0.512807, -0.09597398, -0.32097813, -0.051580857, -0.022466356, 0.01148551, 0.0, 0.0, 0.0, 0.0) * go_0(0.0, -1.0);
    result += mat4(-0.026032459, -0.04193211, 0.37703893, -0.031916667, -0.27421117, 1.0906446, -0.049654085, -0.19814016, 0.07819544, 0.06003738, 0.1405805, -0.0064135445, 0.0, 0.0, 0.0, 0.0) * go_0(0.0, 0.0);
    result += mat4(0.041450135, 0.11319654, -0.23237701, 0.08443178, 0.53344345, 0.30857387, -0.057264958, -0.1575803, 0.2325609, -0.027797326, -0.04544767, -0.18720597, 0.0, 0.0, 0.0, 0.0) * go_0(0.0, 1.0);
    result += mat4(0.2531829, -0.074966915, -0.27800754, -0.3146097, 0.20126024, -0.5380133, -0.15082566, -0.19021043, 0.29951036, 0.17123336, -0.01681872, -0.12574998, 0.0, 0.0, 0.0, 0.0) * go_0(1.0, -1.0);
    result += mat4(0.25203633, 0.19882993, 0.14906439, 0.13593598, 0.40712556, 0.084902965, 0.42969635, 0.2961132, -0.057267334, -0.030388135, 8.8084314e-05, 0.0210724, 0.0, 0.0, 0.0, 0.0) * go_0(1.0, 0.0);
    result += mat4(-0.13459359, -0.12199573, 0.12591946, 0.24736497, 0.2033463, -0.09388599, -0.094370656, 0.1071285, -0.18479438, -0.066625565, 0.08279283, 0.20130983, 0.0, 0.0, 0.0, 0.0) * go_0(1.0, 1.0);
    result += vec4(-0.011108127, -0.07481861, 0.07640154, 0.4964964);
    colorOut0 = result;
}
)";

// Pass 1: Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x8
// reads conv2d_tf -> writes conv2d_1_tf
const std::string RendererOutputShader::s_a4k_restore_pass1_source =
R"(
#define go_0(x_off, y_off) (max((texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
#define go_1(x_off, y_off) (max(-(texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
void outputShader()
{
	vec2 anubisTexel = 1.0 / max(textureSrcResolution, vec2(1.0));
    vec4 result = mat4(-0.056432575, 0.0028165397, -0.026325442, -0.14802271, 0.16885762, -0.062179096, -0.2332292, 0.17513658, -0.08011296, 0.02947316, 0.014771492, -0.17946689, 0.026012989, -0.09823925, 0.036625937, -0.06924322) * go_0(-1.0, -1.0);
    result += mat4(-0.13571467, 0.09831142, 0.12911566, 0.06305893, -0.07188695, -0.20161287, 0.3858435, -0.21069056, -0.12294444, -0.1404628, -0.022659872, 0.23008968, 0.10969853, 0.17640765, 0.39796907, 0.20413099) * go_0(-1.0, 0.0);
    result += mat4(-0.0061665224, 0.055102807, -0.0059629944, -0.021429887, 0.061626043, 0.16898955, -0.21215646, 0.16510476, 0.2238265, 0.19429931, 0.09874656, 0.06828208, -0.122404456, -0.00026717107, -0.28203064, -0.29979932) * go_0(-1.0, 1.0);
    result += mat4(-0.22735378, 0.14538136, 0.11549746, 0.194148, -0.09841722, -0.0661309, 0.348576, -0.017375294, -0.044078812, 0.1298332, 0.04793373, -0.30687734, 0.08353025, 0.083519086, 0.10766399, 0.31796935) * go_0(0.0, -1.0);
    result += mat4(0.048365135, -0.17566709, -0.33212858, -0.052667376, -0.26443407, -0.010216014, 0.1573303, 0.05725314, 0.08140953, -0.09664591, 0.076109104, -0.026773714, 0.07732627, 0.10188082, -0.28266954, -0.16230233) * go_0(0.0, 0.0);
    result += mat4(0.29931107, 0.117944, -0.10414009, 0.12795551, 0.12576093, 0.17082554, -0.15803693, 0.13430743, -0.025801308, -0.10797019, 0.0721032, 0.2825884, -0.11025257, 0.12798019, 0.081827976, -0.050441865) * go_0(0.0, 1.0);
    result += mat4(-0.11827391, 0.08306765, -0.3430314, 0.07898041, -0.023839617, -0.019507334, 0.23176382, -0.40992323, 0.09411734, 0.38415068, -0.25845516, -0.29984522, 0.1470966, -0.0684779, -0.07071314, -0.026773235) * go_0(1.0, -1.0);
    result += mat4(0.19091596, 0.082110435, -0.5266589, -0.1744098, -0.015838385, -0.046316292, 0.023171103, -0.03731331, 0.2642396, 0.31824252, -0.041754793, -0.09525519, -0.14696182, 0.052168854, 0.039857205, -0.027555354) * go_0(1.0, 0.0);
    result += mat4(0.15207373, 0.09845733, 0.0142631065, 0.096375965, 0.06089903, 0.17902578, -0.42391995, 0.22475442, 0.016356342, -0.06277531, -0.12173141, -0.18635495, -0.0013459618, 0.15725887, 0.019310836, 0.20293565) * go_0(1.0, 1.0);
    result += mat4(-0.18395247, 0.30672902, 0.09034339, 0.1821889, -0.0419004, -0.2169228, -0.14052129, 0.11006559, 0.1709272, 0.51062274, 0.13758625, -0.2242552, -0.030382963, 0.3357568, -0.26491287, 0.02501938) * go_1(-1.0, -1.0);
    result += mat4(0.040511727, 0.12523083, -0.27318433, 0.08388512, 0.25354835, 0.3404216, -0.2632471, -0.17784123, 0.2732347, 0.4468553, 0.084667034, -0.1856242, 0.034099877, -0.00954992, -0.32751867, -0.062207516) * go_1(-1.0, 0.0);
    result += mat4(0.17564747, 0.11645554, -0.16362113, 0.105654195, -0.2762563, -0.1413764, 0.23264363, -0.14000498, 0.095402054, 0.0715738, -0.19346157, -0.028285999, 0.009799127, 0.04059529, 0.19688335, 0.1282381) * go_1(-1.0, 1.0);
    result += mat4(0.23575781, -0.11446148, -0.20504695, 0.035568226, 0.36890212, -0.85968876, -0.18545328, 0.33796397, -0.30916876, -0.10445518, -0.3046253, 0.33271998, -0.06263589, -0.2160114, -0.16383372, -0.31173357) * go_1(0.0, -1.0);
    result += mat4(0.20469664, 0.4039374, -0.070057206, 0.030353077, 0.39843914, -0.15490077, -0.24476516, 0.38238233, -0.21809858, 0.23496576, -0.051794037, 0.033664484, -0.14411364, -0.2515329, 0.124655396, -0.05818785) * go_1(0.0, 0.0);
    result += mat4(-0.09065731, -0.16787091, 0.013269188, 0.23687351, -0.41504318, -0.048163068, 0.31760025, -0.33648986, 0.29752317, 0.2926866, 0.14408836, -0.33382463, -0.15873958, -0.121961035, 0.11797893, 0.09000567) * go_1(0.0, 1.0);
    result += mat4(0.13356976, 0.013763947, 0.012169505, -0.109594524, 0.03417223, 0.7031121, 0.65146804, 0.5250268, -0.50132495, -0.419648, 0.2940041, 0.83051753, -0.17595838, 0.1633008, -0.018587278, 0.079596795) * go_1(1.0, -1.0);
    result += mat4(0.07570128, -0.1581438, 0.03904949, 0.14890033, -0.054611947, 0.17469402, -0.44252598, 0.036181703, -0.4981031, -0.37507218, -0.18466389, 0.2645845, 0.25189674, -0.025896115, 0.034307647, -0.020462232) * go_1(1.0, 0.0);
    result += mat4(-0.11645865, 0.02296537, 0.040909223, 0.015069485, 0.062284566, -0.22526766, 0.09241534, -0.32623053, 0.18208642, 0.3954284, 0.2884468, -0.25137675, -0.037232924, -0.10185309, -0.17956531, 0.018966453) * go_1(1.0, 1.0);
    result += vec4(-0.16371979, -0.024620198, -0.035754893, 0.04176776);
    colorOut0 = result;
}
)";

// Pass 2: Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x8
// reads conv2d_1_tf -> writes conv2d_2_tf
const std::string RendererOutputShader::s_a4k_restore_pass2_source =
R"(
#define go_0(x_off, y_off) (max((texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
#define go_1(x_off, y_off) (max(-(texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
void outputShader()
{
	vec2 anubisTexel = 1.0 / max(textureSrcResolution, vec2(1.0));
    vec4 result = mat4(0.01921286, -0.26684764, -0.12663573, 0.31641877, -0.25313398, 0.12264074, 0.58750325, -0.14084283, 0.5837018, -0.042300556, -0.20435576, -0.009954825, 0.060783498, 0.05540401, 0.2205112, -0.06578902) * go_0(-1.0, -1.0);
    result += mat4(-0.21930243, -0.03774968, 0.22615197, 0.18338196, 0.011201461, -0.271034, 0.00573116, -0.12248194, 0.47990513, 0.2982416, -0.1087603, -0.050099242, -0.07620939, -0.07148229, 0.03691984, -0.16796488) * go_0(-1.0, 0.0);
    result += mat4(-0.14962853, -0.053769328, 0.02387081, 0.22002189, 0.052237745, -0.26160842, -0.08603077, 0.012542448, 0.08119985, 0.075785555, -0.33437458, -0.43373227, -0.13206963, -0.08759176, -0.03288923, -0.09799959) * go_0(-1.0, 1.0);
    result += mat4(-0.1305593, -0.5974288, 0.06058367, 0.08406488, 0.013692483, 0.06646377, 0.16469325, 0.08990975, 0.42217395, -0.11289523, -0.06165009, 0.48556912, -0.15702641, -0.19922857, -0.0035429662, -0.0022089656) * go_0(0.0, -1.0);
    result += mat4(-0.1964807, 0.038099788, 0.21587034, 0.039734077, -0.07063389, 0.11604167, -0.24558097, -0.08900199, -0.7684516, -0.1037487, -0.09380674, 0.33144563, -0.16653742, 0.0028585843, -0.33774406, -0.0528696) * go_0(0.0, 0.0);
    result += mat4(-0.27298656, -0.05665099, 0.09661685, 0.19780266, 0.1025106, -0.22055034, -0.21218458, -0.040628925, 0.0095010325, 0.13118382, -0.42582452, -0.22197723, 0.21006055, -0.06189587, -0.15285942, -0.09526762) * go_0(0.0, 1.0);
    result += mat4(-0.14494462, -0.046788953, 0.065877035, 0.09911713, 0.35096622, 0.16682479, 0.028363144, 0.36037162, 0.29413632, 0.28212717, -0.025364442, -0.3406269, 0.047262143, -0.11892685, -0.008032766, 0.29743317) * go_0(1.0, -1.0);
    result += mat4(-0.15191558, -0.36980554, 0.14555687, 0.0043930537, -0.012661432, 0.15737776, -0.115250416, 0.10324491, 0.24491951, -0.15575431, -0.27802598, 0.21959937, 0.18063772, 0.4455559, -0.09693302, 0.33382267) * go_0(1.0, 0.0);
    result += mat4(0.2717801, 0.13452889, 0.14105384, 0.16324317, -0.40111846, 0.1154301, -0.0076733204, -0.09697362, 0.44306824, -0.02831414, -0.2153124, -0.12075326, 0.060776163, 0.30347148, -0.0036976219, -0.12070682) * go_0(1.0, 1.0);
    result += mat4(-0.39780128, -0.29875937, -0.12952097, 0.080333896, 0.07520163, 0.021689568, -0.23121156, -0.038140096, -0.1593877, 0.017156163, -0.06038025, 0.009244022, -0.13917233, 0.30957314, 0.243109, -0.104947075) * go_1(-1.0, -1.0);
    result += mat4(-0.07965157, 0.06776501, -0.13288979, 0.005851189, -0.08768168, -0.03689969, 0.12034646, 0.22441491, 0.14453568, -0.17648841, -0.3378289, -0.018329712, 0.11722939, -0.34161824, 0.08424494, -0.01400687) * go_1(-1.0, 0.0);
    result += mat4(0.08153887, 0.07222914, -0.14663404, -0.038526025, -0.07385973, 0.18440577, 0.35890242, 0.17084727, 0.26345527, 0.15280858, -0.007446105, -0.024403179, -0.30336383, -0.22978698, 0.11612946, -0.23614909) * go_1(-1.0, 1.0);
    result += mat4(-0.07447396, 0.09023449, -0.13798, -0.086943336, -0.30787337, 0.15087669, 0.14418626, -0.03371195, 0.048989657, -0.13075387, -0.13458036, -0.059836224, 0.06495196, 0.269715, 0.3674355, 0.38956037) * go_1(0.0, -1.0);
    result += mat4(0.34981915, -0.048779126, 0.31717536, 0.38080826, -0.20149232, -0.82969636, -0.10167862, 0.6382858, 0.25976858, 0.4370118, -0.04724865, -0.10014156, 0.19380626, -0.080370255, 0.09578106, -0.035166856) * go_1(0.0, 0.0);
    result += mat4(-0.026443917, 0.4132611, 0.01822534, 0.12742202, -0.26652107, -0.2996705, 0.30905882, 0.07989903, 0.38249823, 0.21486135, 0.025314959, -0.14717339, -0.13344015, -0.32088286, -0.2833883, -0.30973712) * go_1(0.0, 1.0);
    result += mat4(0.021517841, 0.006556378, 0.2025686, -0.12044382, -0.38583103, -0.0027515136, -0.06556736, -0.097090125, 0.04676486, -0.11954886, -0.051612873, 0.07831412, -0.18823163, -0.16542958, 0.04245155, 0.6437998) * go_1(1.0, -1.0);
    result += mat4(-0.39475346, -0.2936861, 0.26768062, -0.28151843, 0.21935691, 0.2101108, -0.15455097, 0.19548604, 0.09188909, -0.020147726, 0.103328265, -0.12574542, -0.34167948, 0.07523185, -0.17669058, 0.62446547) * go_1(1.0, 0.0);
    result += mat4(-0.37661025, -0.29630858, 0.05451026, 0.1611643, 0.14079669, -0.2170294, -0.038716137, 0.13514164, -0.21235192, -0.07860726, -0.005749412, 0.025625167, -0.13297133, 0.33012658, -0.27434957, -0.18416783) * go_1(1.0, 1.0);
    result += vec4(-0.0036821906, -0.050239526, -0.01355402, 0.00048220603);
    colorOut0 = result;
}
)";

// Pass 3: Anime4K-v4.0-Restore-CNN-(S)-Conv-3x3x3x8
// reads conv2d_2_tf -> writes MAIN
const std::string RendererOutputShader::s_a4k_restore_pass3_source =
R"(
#define go_0(x_off, y_off) (max((texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
#define go_1(x_off, y_off) (max(-(texture(textureSrc, passUV + vec2(x_off, y_off) * anubisTexel)), 0.0))
void outputShader()
{
	vec2 anubisTexel = 1.0 / max(textureSrcResolution, vec2(1.0));
    vec4 result = mat4(0.15873, 0.17989138, 0.14648493, 0.0, -0.017379675, -0.017363746, -0.019855022, 0.0, 0.009670625, 0.0070157526, 0.0075994316, 0.0, 0.025388412, 0.027231036, 0.024052646, 0.0) * go_0(-1.0, -1.0);
    result += mat4(0.048195973, 0.041760173, 0.037366055, 0.0, -0.115950756, -0.12887983, -0.12535639, 0.0, 0.032125086, 0.03397254, 0.032950625, 0.0, 0.01223746, 0.020822672, 0.0161561, 0.0) * go_0(-1.0, 0.0);
    result += mat4(0.0890567, 0.094453335, 0.09014035, 0.0, 0.016081346, 0.017434116, 0.020783134, 0.0, -0.011775135, -0.010094134, -0.018522855, 0.0, 0.072103254, 0.07940666, 0.065876864, 0.0) * go_0(-1.0, 1.0);
    result += mat4(-0.04841196, -0.06963968, -0.056574684, 0.0, 0.10912542, 0.11813441, 0.10643838, 0.0, -0.013013885, -0.01562045, -0.013802797, 0.0, 0.037505716, 0.04352026, 0.04645123, 0.0) * go_0(0.0, -1.0);
    result += mat4(-0.3472869, -0.36243078, -0.33530185, 0.0, 0.23654196, 0.2305048, 0.22150646, 0.0, -0.045226905, -0.041799217, -0.042511635, 0.0, -0.10267792, -0.1123385, -0.10845448, 0.0) * go_0(0.0, 0.0);
    result += mat4(0.011987401, 0.012285043, 0.007813165, 0.0, -0.15911353, -0.17523928, -0.1535267, 0.0, 0.15675929, 0.16531634, 0.15948962, 0.0, -0.09240023, -0.09513292, -0.084187366, 0.0) * go_0(0.0, 1.0);
    result += mat4(0.069052905, 0.07278333, 0.0756627, 0.0, -0.012180326, -0.018794727, -0.031050753, 0.0, -0.044663202, -0.04362803, -0.038904265, 0.0, -0.008540197, -0.011201734, -0.01556625, 0.0) * go_0(1.0, -1.0);
    result += mat4(-0.08261173, -0.09042543, -0.07589266, 0.0, 0.043515377, 0.045066774, 0.04037769, 0.0, -0.06262993, -0.07469342, -0.058593787, 0.0, 0.026696987, 0.028740842, 0.037405368, 0.0) * go_0(1.0, 0.0);
    result += mat4(0.07975598, 0.09597654, 0.08997132, 0.0, -0.07844719, -0.07880916, -0.06835411, 0.0, 0.05668995, 0.050163813, 0.053357534, 0.0, -0.020040333, -0.019867316, -0.01907621, 0.0) * go_0(1.0, 1.0);
    result += mat4(-0.017078733, -0.017393313, -0.008266595, 0.0, -0.0033478448, -0.0027439648, -0.0042334674, 0.0, -0.06354017, -0.062058125, -0.04652064, 0.0, -0.010787706, -0.0062706997, -0.007573461, 0.0) * go_1(-1.0, -1.0);
    result += mat4(-0.019895451, -0.016341688, -0.008712399, 0.0, 0.026231976, 0.023955572, 0.0216376, 0.0, -0.061950512, -0.05481285, -0.05261985, 0.0, -0.018804235, -0.016235247, -0.0131616965, 0.0) * go_1(-1.0, 0.0);
    result += mat4(-0.055628926, -0.063315354, -0.057192408, 0.0, -0.0256364, -0.028660972, -0.02937357, 0.0, -0.017604912, -0.020851422, -0.016070362, 0.0, -0.0870202, -0.0832279, -0.07525406, 0.0) * go_1(-1.0, 1.0);
    result += mat4(0.062738225, 0.07106593, 0.061644047, 0.0, -0.06068257, -0.06983662, -0.066070385, 0.0, 0.024919355, 0.03227179, 0.028569462, 0.0, -0.07866227, -0.098967604, -0.092128105, 0.0) * go_1(0.0, -1.0);
    result += mat4(0.040397774, 0.047241107, 0.03962998, 0.0, -0.09112752, -0.10057507, -0.09301817, 0.0, 0.10833967, 0.101835825, 0.10027467, 0.0, 0.27189335, 0.27433604, 0.26781923, 0.0) * go_1(0.0, 0.0);
    result += mat4(-0.044211388, -0.042373534, -0.03658007, 0.0, 0.113148406, 0.12423258, 0.107804194, 0.0, -0.17081551, -0.18562958, -0.17475435, 0.0, 0.09636739, 0.10763415, 0.093332425, 0.0) * go_1(0.0, 1.0);
    result += mat4(-0.03798545, -0.047811143, -0.050768293, 0.0, 0.018775463, 0.026812987, 0.03452908, 0.0, 0.0055677597, 0.0039081173, -0.0017878668, 0.0, -0.10728597, -0.12618187, -0.109045394, 0.0) * go_1(1.0, -1.0);
    result += mat4(0.06359783, 0.064184755, 0.04934199, 0.0, -0.009819327, -0.006616115, -0.007431496, 0.0, 0.025055679, 0.024787048, 0.017360551, 0.0, -0.047140837, -0.061695747, -0.06440822, 0.0) * go_1(1.0, 0.0);
    result += mat4(0.060199022, 0.06482763, 0.059514645, 0.0, 0.026998974, 0.028776823, 0.024897143, 0.0, 0.17968474, 0.19337215, 0.16760105, 0.0, 0.0075838566, 0.010503482, 0.011993149, 0.0) * go_1(1.0, 1.0);
    result += vec4(-0.0052927984, -0.0060193934, -0.0048643993, 0.0);
    colorOut0 = result + texture(textureSrc2, passUV);
}
)";

// GENERATED by tools/fsrcnn/gen_shaders.py - do not hand-edit, regenerate.
// Weights from Saafke/FSRCNN_Tensorflow (Apache-2.0); licence text in
// tools/fsrcnn/LICENSE-FSRCNN_Tensorflow.
//
// Architecture as read from the checkpoint:
//   f1 [5, 5, 1, 32]
//   f2 [1, 1, 32, 5]
//   f3 [3, 3, 5, 5]
//   f4 [1, 1, 5, 32]
//   f5 [1, 1, 32, 4]

const std::string RendererOutputShader::s_fsrcnn_f1_0_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.022794006, -1.4706217, 0.0041848561, 0.074732363) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.12280572, -0.92381197, 0.13452764, -0.23829909) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.031733382, -1.0725499, -0.11350829, -0.048500098) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.10011739, -1.1264448, 0.14412121, 0.10277271) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.097387232, -1.0826552, -0.1938121, 0.088477045) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.12667076, -1.4304265, 0.023500657, 0.12397495) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.094444364, -0.40369228, 0.092744969, -0.86321396) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.42898929, -1.1559296, 0.17101587, 0.58980584) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.0032095, -0.39298686, 0.25376344, 0.024766332) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.22767323, -1.4540257, 0.15757385, -0.017646803) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.042996313, -2.2875428, -0.042716935, 0.27488065) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.28749394, -0.85378855, 0.18330353, -1.5501994) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(2.3242404, -0.53655922, -1.0341759, 1.8059992) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.6727294, -0.74129385, -0.35100812, -0.16234134) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.20076603, -1.9879045, 0.029283464, 0.010029658) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.010453184, 1.5119364, 0.13576093, -0.61085194) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.15642235, 0.70087063, -0.098248057, 0.20776697) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.42329502, 0.53071362, 0.27876946, 0.39143786) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.1251616, 0.78402066, -0.16308637, -0.17268379) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.17242174, 1.5443643, -0.030725611, 0.049793262) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.10198572, 0.21745406, -0.18009047, 0.027487021) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.00074446335, -0.20468247, 0.1544659, -0.34221771) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.053583216, -0.079266727, -0.097391143, 0.28865054) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.011735072, -0.36616457, -3.7595468, -0.064154692) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.051140275, -0.071496047, -1.0512791, 0.033759017) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.31034875, 0.19551241, 0.42027169, -0.32508406);
	result = max(result, vec4(0.0)) + vec4(-0.15493678, -0.062378414, 0.068163097, -0.41406596) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_1_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.3730562, -1.7142709, -0.22140071, -0.01457455) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.51948071, -1.1151713, -1.2215954, -0.065074265) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.31458989, 0.64597052, 0.88415498, 0.10630032) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.49595571, -0.69385242, -0.52096188, 0.19267949) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.033770844, 0.077861831, 0.18555628, -0.033058044) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.67830968, -1.5000319, -0.05379976, -0.070181347) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.2527449, -0.39980009, 1.4735271, 0.24861367) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.3848532, 0.52670246, 0.38808078, -0.45262131) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.25195822, -0.45809937, -0.5463087, -0.050737672) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.12019987, 0.078284696, 0.260389, -0.26346025) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.16608134, -0.69905418, 0.11729676, -1.72869) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.92558354, -0.099513434, -1.3309935, 1.7491709) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.4342123, 0.82762271, -1.1419486, -0.49400514) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.26825076, -0.37656045, -0.50025052, -0.35046473) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.087194882, 0.17881882, 0.24373382, 0.17992805) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.0308515, -1.154731, 0.23570837, 0.33395112) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.1568794, -0.81770724, -0.97266984, 0.13577224) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.13005048, 0.80787516, 0.3422001, -0.36418521) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.15106407, -0.56892651, 0.18722951, -0.27552056) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.02696109, 0.088588089, 0.17216633, 0.072691448) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.29549491, -0.99700671, -0.26859343, 0.30342704) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.49424359, -1.2687522, -0.3093811, 0.022780877) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.0024940844, 1.0935105, 0.37903059, -0.25330305) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.025757881, -0.85493767, -0.050696988, 0.30827534) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.079037555, 0.073464714, -0.010144114, -0.14948031) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.24767587, -0.047166329, 0.14542165, -0.0034391859);
	result = max(result, vec4(0.0)) + vec4(-0.25107113, -0.057137694, -0.093491994, 0.10785955) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_2_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(0.024383202, -0.25175151, -0.29882884, 0.025396597) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.011660974, 0.13073298, 0.30500799, 0.0078440504) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.039787296, -0.34626245, -0.10318056, -0.075893968) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.0095761176, 0.56710905, 0.31050202, -0.036217961) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.018975964, -0.39685467, -0.29226992, 0.021098673) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.023586737, 0.019846383, 0.11491992, -0.010821464) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.15454383, -0.38902566, -0.060584538, 0.12795001) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.13869248, -1.2117934, -0.6717698, 0.29001692) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.031003257, 0.89970595, 0.39692271, 0.098443054) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.029851094, -0.70815778, 0.08258152, 0.035174917) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.066609822, 0.40061483, -0.37087813, -0.092095509) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.24020687, -0.56476611, -0.20891599, 0.34001935) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(2.3263323, 1.1361523, -1.3736596, -2.9220157) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.36281142, -0.73146439, 1.2181281, 0.51748824) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.0685139, -0.71820384, -0.71416479, -0.1098651) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.013787122, -0.48444381, 0.19276085, -0.013876699) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.24632555, -0.21834908, -0.09610191, 0.40187287) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.36945954, 0.9701128, -1.0292959, 0.51871008) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.1180362, -0.62369734, 1.0010952, 0.14056885) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.0044192043, 0.37433213, -0.47365776, 0.011224234) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.0078788316, 0.10655122, -0.11505906, -0.007323571) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.034972329, 0.11131515, -0.099092819, -0.037816964) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.085790373, 0.14518432, -0.22726181, -0.12693296) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.037776437, -1.0759419, 0.56694239, 0.044998892) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.013493289, 0.35555038, -0.43691933, 0.031009521) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.76759821, -0.16809762, 0.046317738, 0.36516935);
	result = max(result, vec4(0.0)) + vec4(1.0501654, 0.008737809, -0.089184567, 1.0839397) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_3_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.68470466, -0.10487677, 0.146044, -1.8455613) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.61293155, -0.33679512, -0.099053934, -1.6638122) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.066388458, 0.35430047, -0.047495589, -0.92633784) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.0504615, 0.077643536, 0.081816763, -0.93387276) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.55719733, -0.021037329, -0.071137324, -1.2393332) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.66371953, 0.58786768, 0.36146581, -1.1363541) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.67343891, -0.62198883, -0.42336279, -0.33134508) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.24338606, 0.027327828, -0.52165079, -0.82077557) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.04926965, 0.064713344, 0.48309734, -1.1119916) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.91509104, 0.04425985, 0.007751869, -0.9181965) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.96762675, 0.37329996, 0.13501878, 1.2120252) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.78279757, -1.0982481, 1.7895359, 0.56353951) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.0310518, -1.1308043, -1.6682711, 1.4399072) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.5615116, 0.3159858, -0.23391831, 1.4159164) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.72986674, 0.47694814, -0.04384267, 0.79951006) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.80387878, 0.047386378, -0.38350481, -0.78029919) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.5322237, 2.046555, 0.86784077, -0.2545808) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.41831392, -0.78668272, -1.2936724, -0.49012572) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.3468471, -0.41479948, 0.3097429, -0.18674976) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.93193746, -0.33565065, 0.17655139, -0.4851841) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.18879564, -0.45152366, -0.11894002, 0.19363877) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.082803339, 0.2074537, 0.27924487, 0.032709975) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.16173568, 0.27202618, 0.037341915, -0.073235489) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.44298813, 0.12682354, -0.054980922, 0.12064347) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.35749176, 0.009434781, -0.043828975, 0.12114768) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.062443715, -0.10179173, -0.17305566, -0.25212568);
	result = max(result, vec4(0.0)) + vec4(-0.073048778, -0.089616962, -0.096701093, 0.050709967) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_4_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.031671274, 0.11426731, 0.11486482, 0.012380741) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.41976508, -0.1083798, -0.69398642, 0.096451566) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.40574431, -0.27046433, -0.19786407, -0.0067429026) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.30935299, 0.10372353, 0.39477807, 0.10173561) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.38082948, 0.16127869, 0.43230903, -0.091400586) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.059447803, -0.11876493, -0.77448797, -0.59821266) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.20327972, -0.38933158, 1.0336273, -0.10606756) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.1690489, -0.25376502, 1.1844928, 0.58470976) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.5981131, 0.67123771, -0.27528185, 0.50690228) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.48598072, -0.076877169, -0.6722154, -0.061063152) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.12760706, -0.28757548, 0.6379962, 0.45188597) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.48260149, -0.28587875, 0.16195577, -0.77164173) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-2.0986505, 0.64003181, -2.4908018, -2.7687008) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.86045426, 1.3370334, 0.052812673, 0.56180847) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.23143485, -0.58155, 0.094257936, 0.30424601) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.067818463, 0.2388079, 0.16446497, 0.48367783) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.22218707, 0.79633081, 0.15632354, 1.2284572) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.34202966, 1.0252025, -0.091939799, 1.2537698) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.1653963, -2.5291362, -0.16244218, -1.5979185) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.741732, -0.16759297, 0.22060654, -0.48737356) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.067594387, 0.15882285, -0.14714953, -0.45506003) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.14760451, -0.2483267, -0.56267595, -0.47790536) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.38726848, -0.73021376, 0.53200525, 0.1900197) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.61348271, 0.1417042, 0.43531293, 0.87904811) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.30964696, 0.64312351, -0.013997852, 0.21812399) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.18427324, -0.46234491, -0.077239059, -0.13376911);
	result = max(result, vec4(0.0)) + vec4(-0.17759678, -0.19645433, -0.089079469, -0.0570159) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_5_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.0069018388, 0.22144489, 0.0070385193, -0.3462052) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.13120337, 0.06230687, 0.46995476, -0.26031828) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.17146131, -0.23516199, -0.2374417, -1.0250599) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.34735501, -0.14902179, 0.11481021, -0.43244299) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.45853236, -0.027574666, -0.11215472, 0.046110705) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.13819349, 0.13199849, -0.88333076, -0.40746403) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.29311499, -1.6188126, 1.4898987, -0.55680758) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.77497524, -0.30514729, -0.84940809, -0.72418517) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.30927768, 0.96378285, -0.095757835, -0.79967391) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.59693152, 0.55211502, -0.03066477, -1.187624) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.16664992, -0.79732287, -0.2637108, -1.31759) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.24912094, 0.49730495, -0.26977819, -1.2142276) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.93532, 2.1100128, 0.85934097, 1.0451859) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.260031, -0.53288358, -0.52047276, 0.57283467) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.30753073, -0.58589745, -0.038084324, -0.028900774) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.033540823, 0.33650109, 0.56323242, 0.95409477) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.42095137, 0.63042176, -2.0445061, -0.02079349) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.154608, -0.58446729, 1.297291, -0.45404992) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.74927038, -0.42921001, 0.040243231, -0.55716056) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.16451441, -0.19660416, 0.10036064, -0.14783998) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.049862869, 0.081158906, 0.52925593, 0.16213167) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.09285716, 0.17518067, -0.27924427, 0.093037598) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.10125007, -0.43729687, -0.45786679, 0.25267184) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.49807486, -0.075661197, 0.252092, 0.17592669) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.11692987, 0.1394608, 0.026585039, 0.16091964) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.30245212, -0.47149986, -0.34920421, -0.076620862);
	result = max(result, vec4(0.0)) + vec4(-0.1446559, -0.19287434, -0.17125766, -0.11920999) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_6_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(-0.46707973, 0.026166037, 0.17753668, 0.20843607) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.084325515, 0.10852505, -0.12594575, 0.76190847) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.66609001, -0.10017142, 0.30352351, 0.42332479) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.7320385, -0.043417022, -0.034667674, -0.54279983) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.29068351, -0.0179967, 0.036199626, -0.4140166) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.6461466, 0.15114734, 0.14089832, -0.33413485) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.2549993, -0.1507584, -0.493007, -0.96756274) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.59842741, -0.0564138, -0.20501804, 0.37583971) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.19946598, -0.1820371, -0.70872235, 1.843612) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.080232926, 0.0090430686, -0.093467362, 0.52254444) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.14612365, 0.10799748, -0.63391477, -0.063421577) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-2.2132747, 0.23642045, -1.2567594, -0.089781649) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-2.2757573, 2.5220721, 1.2951201, -1.4394277) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.0617286, 0.17558105, 0.5379644, -1.6551422) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.076660573, 0.11813463, 0.44415906, 0.17006496) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.25281736, -0.40731493, 0.070152164, 0.38560787) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.38590303, -0.83165914, 0.82009351, 0.3899014) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.75062191, -2.005301, -0.86428535, 0.23799984) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.78184158, 0.65548193, -0.68945724, 0.16616826) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.17282338, -0.10618308, -1.1402905, -0.34434777) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.35377249, 0.21006307, -0.093908481, -0.18700527) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.084803693, 0.36542434, -0.44578856, -0.063686602) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.080356047, -0.49147308, -0.71393085, -0.065537967) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.067193247, -0.095561266, 0.30103421, 0.20717287) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.059400026, -0.066478945, 0.20958096, 0.094296806) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.01344755, -0.34417793, -0.10884163, -0.31737068);
	result = max(result, vec4(0.0)) + vec4(-0.26026434, -0.11722318, -0.014477089, -0.037535459) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f1_7_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	result += vec4(0.033774275, -0.39042172, 0.053931031, -1.1753532) * dot(texture(textureSrc, passUV + vec2(-2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.042561393, 0.19229823, -0.24584983, -3.4779577) * dot(texture(textureSrc, passUV + vec2(-1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.15595794, 0.25703603, 0.14215881, -0.85789973) * dot(texture(textureSrc, passUV + vec2(0.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.15151696, 0.013087627, 0.087256841, 0.65468389) * dot(texture(textureSrc, passUV + vec2(1.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.11784055, 0.11330092, -0.056613199, -0.097044319) * dot(texture(textureSrc, passUV + vec2(2.0, -2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.08890114, 0.35936761, -0.098621063, -1.0095004) * dot(texture(textureSrc, passUV + vec2(-2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.14591248, 1.7755477, 0.0057509467, -3.2530656) * dot(texture(textureSrc, passUV + vec2(-1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.16432144, -0.1107511, 0.39615303, -0.27190936) * dot(texture(textureSrc, passUV + vec2(0.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.33384955, -1.0503985, -0.36575174, 0.707591) * dot(texture(textureSrc, passUV + vec2(1.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.045062803, -0.3901681, 0.023331838, 0.014363267) * dot(texture(textureSrc, passUV + vec2(2.0, -1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.51794964, 0.46345097, 0.2296039, -1.631368) * dot(texture(textureSrc, passUV + vec2(-2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.17718738, -1.3415577, 0.081218064, -3.2817912) * dot(texture(textureSrc, passUV + vec2(-1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.68809539, -2.8025808, 1.4873338, -0.95250559) * dot(texture(textureSrc, passUV + vec2(0.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.23715222, 1.1423528, 0.79454148, 0.86535531) * dot(texture(textureSrc, passUV + vec2(1.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.2811279, 0.3681255, -0.1328246, 0.044792823) * dot(texture(textureSrc, passUV + vec2(2.0, 0.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.11608107, -0.49738848, -0.22946359, -0.89411515) * dot(texture(textureSrc, passUV + vec2(-2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.41096056, -0.40257579, 0.15133394, -3.3254552) * dot(texture(textureSrc, passUV + vec2(-1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-2.0740035, 1.5574629, 0.45108396, -0.36719081) * dot(texture(textureSrc, passUV + vec2(0.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-1.7743167, 0.11880831, -0.69195879, 0.79934454) * dot(texture(textureSrc, passUV + vec2(1.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.49763229, -0.10516743, 0.24497044, -0.084261514) * dot(texture(textureSrc, passUV + vec2(2.0, 1.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.2115024, -0.0079923728, -0.0090589197, -0.44170326) * dot(texture(textureSrc, passUV + vec2(-2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.21216935, -0.041039534, -0.048240211, -3.4365187) * dot(texture(textureSrc, passUV + vec2(-1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.3045453, 0.013713411, -0.060202993, -1.1124905) * dot(texture(textureSrc, passUV + vec2(0.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(1.3673539, 0.031536799, 0.181095, 0.8577432) * dot(texture(textureSrc, passUV + vec2(1.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(0.38357645, -0.053446699, -0.17916112, -0.16926505) * dot(texture(textureSrc, passUV + vec2(2.0, 2.0) * texel).rgb, vec3(0.299, 0.587, 0.114));
	result += vec4(-0.19924848, -0.13327453, -1.761605, 0.4014661);
	result = max(result, vec4(0.0)) + vec4(0.13841274, -0.12808895, -0.035106458, 0.0057000895) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t0_a0_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.02547021, 0.05141193, 0.58514804, 0.61803138) * a.x;
	result += vec4(-0.0020396565, -0.63316715, 0.14795224, -0.29568267) * a.y;
	result += vec4(-0.044875171, 0.18928503, -0.054713707, -0.29105398) * a.z;
	result += vec4(0.058660388, -0.12976925, -0.3784582, -0.13936651) * a.w;
	result += vec4(-0.079826847, 0.097374491, 0.51473176, -0.43049037) * b.x;
	result += vec4(0.032409083, -0.051505778, -0.076570854, -2.0100138) * b.y;
	result += vec4(-0.043029323, -0.10413659, 0.11350417, -1.0611563) * b.z;
	result += vec4(-0.070073679, 0.12156291, 0.41594011, 0.052943692) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t0_a1_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.39745581, -0.23391725, 0.10426322, -0.019147487) * a.x;
	result += vec4(0.0023488824, 0.49878487, 0.31140998, 0.56918621) * a.y;
	result += vec4(-0.0018969685, -0.10223619, -0.19620784, -2.1261852) * a.z;
	result += vec4(-0.41702849, -0.36307195, 0.18786964, -0.069304787) * a.w;
	result += vec4(-0.069085807, 1.1843188, 0.0072332798, -0.28503346) * b.x;
	result += vec4(-0.072451465, 0.18896616, 0.37748298, 0.29588717) * b.y;
	result += vec4(-0.042130295, 0.20641196, 0.40313959, 0.25067392) * b.z;
	result += vec4(-0.0154334, 0.32984984, 0.11746228, 0.064955249) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t0_a2_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.035015758, -0.16746363, -0.46845737, -1.7665856) * a.x;
	result += vec4(-0.0010845503, -0.23995128, 0.31378788, 0.66713351) * a.y;
	result += vec4(0.022970473, -0.19333835, 0.15111819, 0.41852292) * a.z;
	result += vec4(0.0027597498, 0.40428704, -0.28258321, -0.80380523) * a.w;
	result += vec4(0.025485408, -0.22421813, -0.25255448, -0.94885546) * b.x;
	result += vec4(0.021337891, 0.22247431, -0.28926733, 0.011436102) * b.y;
	result += vec4(0.082369961, -0.23322977, -0.43732882, -0.48158333) * b.z;
	result += vec4(0.033907313, 0.72012192, -0.20210195, 0.30208823) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t0_a3_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.024726573, -0.12065145, -0.19079925, -1.8358934) * a.x;
	result += vec4(0.011259326, -0.34820247, 0.26026413, -0.17509995) * a.y;
	result += vec4(0.12694442, -0.49193951, -0.24196425, -0.19079912) * a.z;
	result += vec4(0.0050384374, -0.51376462, 0.027308425, -0.020046435) * a.w;
	result += vec4(0.028713088, -0.18945991, 0.099264555, -0.23736572) * b.x;
	result += vec4(-0.039605964, -0.19354449, 0.45598865, 0.30763549) * b.y;
	result += vec4(0.044059582, 0.15688531, 0.020122444, -0.40077075) * b.z;
	result += vec4(-0.0022077251, -0.023216061, 0.02425869, 1.1771177) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t1_a0_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.075458549, 0, 0, 0) * a.x;
	result += vec4(-0.074618533, 0, 0, 0) * a.y;
	result += vec4(0.22017521, 0, 0, 0) * a.z;
	result += vec4(-0.018772678, 0, 0, 0) * a.w;
	result += vec4(0.099274807, 0, 0, 0) * b.x;
	result += vec4(-0.031071497, 0, 0, 0) * b.y;
	result += vec4(0.099491335, 0, 0, 0) * b.z;
	result += vec4(0.064432457, 0, 0, 0) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t1_a1_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.36637944, 0, 0, 0) * a.x;
	result += vec4(-0.069119386, 0, 0, 0) * a.y;
	result += vec4(0.069921024, 0, 0, 0) * a.z;
	result += vec4(0.4114584, 0, 0, 0) * a.w;
	result += vec4(0.29540986, 0, 0, 0) * b.x;
	result += vec4(0.071586147, 0, 0, 0) * b.y;
	result += vec4(-0.036638536, 0, 0, 0) * b.z;
	result += vec4(0.070983991, 0, 0, 0) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t1_a2_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.061262403, 0, 0, 0) * a.x;
	result += vec4(-0.16342865, 0, 0, 0) * a.y;
	result += vec4(-0.14089815, 0, 0, 0) * a.z;
	result += vec4(0.10056151, 0, 0, 0) * a.w;
	result += vec4(-0.019790541, 0, 0, 0) * b.x;
	result += vec4(0.094929963, 0, 0, 0) * b.y;
	result += vec4(-0.11411001, 0, 0, 0) * b.z;
	result += vec4(0.027790431, 0, 0, 0) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f2_t1_a3_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.033799279, 0, 0, 0) * a.x;
	result += vec4(-0.081756487, 0, 0, 0) * a.y;
	result += vec4(-0.33489999, 0, 0, 0) * a.z;
	result += vec4(-0.064518996, 0, 0, 0) * a.w;
	result += vec4(-0.18141383, 0, 0, 0) * b.x;
	result += vec4(-0.034118444, 0, 0, 0) * b.y;
	result += vec4(-0.33289403, 0, 0, 0) * b.z;
	result += vec4(0.016198924, 0, 0, 0) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f3_t0_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	{
		vec2 o = vec2(-1.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.074030183, 0.21721165, 0.40232828, -0.15265515) * s0.x;
		result += vec4(-0.0055811936, 0.023027223, -0.14991289, 0.10397474) * s0.y;
		result += vec4(-0.058059867, 0.062593497, 0.63385516, -0.09453655) * s0.z;
		result += vec4(-0.053781386, 0.33375174, -0.36992753, -0.073875472) * s0.w;
		result += vec4(0.11180343, -0.10975985, -0.68781656, 0.28798127) * s1.x;
	}
	{
		vec2 o = vec2(0.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.09171807, 0.70027733, -0.2241199, -0.85752136) * s0.x;
		result += vec4(-0.025013356, 0.011592175, 0.027104847, 0.18625422) * s0.y;
		result += vec4(0.070198134, 0.12071715, -0.80366141, -0.28776202) * s0.z;
		result += vec4(0.17919146, -0.20235005, -0.11487748, -0.08762072) * s0.w;
		result += vec4(-0.084093407, -0.26401561, 0.46952876, 0.82247949) * s1.x;
	}
	{
		vec2 o = vec2(1.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.068691321, 0.14760779, -0.57507008, -0.34364614) * s0.x;
		result += vec4(0.037107777, -0.1214787, 0.13978888, -0.039686412) * s0.y;
		result += vec4(-0.046213418, -0.08597403, 0.23593213, 0.13435976) * s0.z;
		result += vec4(-0.046008397, 0.022236301, 1.6937524, 1.3712872) * s0.w;
		result += vec4(-0.084400237, -0.1551405, 0.22958155, 0.19102171) * s1.x;
	}
	{
		vec2 o = vec2(-1.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.16381757, 0.59027028, -0.073809795, -0.20119408) * s0.x;
		result += vec4(-0.068548486, 0.37890974, 0.13926172, -0.29525629) * s0.y;
		result += vec4(-0.094770469, 0.36677632, -0.67325884, 0.16391318) * s0.z;
		result += vec4(0.012774732, -0.42231444, 0.10014005, 0.021915175) * s0.w;
		result += vec4(0.10536957, -0.26121229, 0.43194395, -0.23330264) * s1.x;
	}
	{
		vec2 o = vec2(0.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(1.4449277, -2.457521, 1.0371351, -0.35464421) * s0.x;
		result += vec4(0.070663072, 0.016398268, 0.029510243, -0.84648716) * s0.y;
		result += vec4(0.45893618, -0.097437337, 0.3375634, 0.18926151) * s0.z;
		result += vec4(0.12089816, 0.041328203, 0.065273836, -0.0022761913) * s0.w;
		result += vec4(0.24430405, -1.5853745, 0.019120771, -0.35277385) * s1.x;
	}
	{
		vec2 o = vec2(1.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.38893703, 0.75603622, -0.33323172, -0.17536063) * s0.x;
		result += vec4(-0.025797855, 0.1804565, -0.32628047, -0.2082375) * s0.y;
		result += vec4(-0.14330639, 0.12600787, -0.20160903, -0.18426411) * s0.z;
		result += vec4(-0.1331467, -1.6225531, -1.1656568, -0.29661939) * s0.w;
		result += vec4(-0.11005057, 0.2115756, -0.35457808, -0.12331338) * s1.x;
	}
	{
		vec2 o = vec2(-1.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.067049831, 0.24442568, -0.46523881, 0.2658754) * s0.x;
		result += vec4(-0.025194941, 0.14865881, -0.43839124, 0.1956965) * s0.y;
		result += vec4(-0.0070469151, 0.052057132, 0.037502151, -0.14652224) * s0.z;
		result += vec4(-0.0062258551, 0.22290154, 0.11934284, 0.013212473) * s0.w;
		result += vec4(0.00052374788, -0.0054970789, 0.15270618, 0.036855992) * s1.x;
	}
	{
		vec2 o = vec2(0.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.1559982, 0.88900352, -0.40981683, 0.71741861) * s0.x;
		result += vec4(0.063117512, 0.13883904, 0.30128327, 0.6026091) * s0.y;
		result += vec4(-0.0075870208, 0.078131959, 0.13295843, -0.017685259) * s0.z;
		result += vec4(0.22812375, 0.033258453, -0.15970607, 0.027120765) * s0.w;
		result += vec4(0.036996048, -0.046656728, -0.18481576, -0.34767962) * s1.x;
	}
	{
		vec2 o = vec2(1.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.090638608, 0.23413835, 0.47734553, 0.32164559) * s0.x;
		result += vec4(0.037559915, 0.070866108, 0.1625233, 0.12856616) * s0.y;
		result += vec4(0.018048918, -0.085190564, -0.025604576, 0.11806787) * s0.z;
		result += vec4(-0.22228593, 0.098262563, -1.3501673, -0.61448932) * s0.w;
		result += vec4(-0.053897828, -0.079928614, 0.050262429, -0.031501427) * s1.x;
	}
	result += vec4(-0.30132511, -0.63716692, -0.12685409, -0.48961827);
	result = max(result, vec4(0.0)) + vec4(0.96554464, 0.13446368, 0.35844347, 0.59644157) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f3_t1_source =
R"(
void outputShader()
{
	vec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));
	vec4 result = vec4(0.0);
	{
		vec2 o = vec2(-1.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.1346495, 0, 0, 0) * s0.x;
		result += vec4(0.0038526962, 0, 0, 0) * s0.y;
		result += vec4(0.16287632, 0, 0, 0) * s0.z;
		result += vec4(0.18115495, 0, 0, 0) * s0.w;
		result += vec4(-0.34283739, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(0.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.13726538, 0, 0, 0) * s0.x;
		result += vec4(0.052627213, 0, 0, 0) * s0.y;
		result += vec4(-0.13241161, 0, 0, 0) * s0.z;
		result += vec4(-0.49382108, 0, 0, 0) * s0.w;
		result += vec4(0.12111714, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(1.0, -1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.1410155, 0, 0, 0) * s0.x;
		result += vec4(-0.074786671, 0, 0, 0) * s0.y;
		result += vec4(0.075570405, 0, 0, 0) * s0.z;
		result += vec4(0.11570959, 0, 0, 0) * s0.w;
		result += vec4(0.10093066, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(-1.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.72416222, 0, 0, 0) * s0.x;
		result += vec4(0.20393535, 0, 0, 0) * s0.y;
		result += vec4(0.30843619, 0, 0, 0) * s0.z;
		result += vec4(-0.015166553, 0, 0, 0) * s0.w;
		result += vec4(-0.34358984, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(0.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.64677262, 0, 0, 0) * s0.x;
		result += vec4(-0.11777845, 0, 0, 0) * s0.y;
		result += vec4(-0.65524155, 0, 0, 0) * s0.z;
		result += vec4(-0.40582794, 0, 0, 0) * s0.w;
		result += vec4(0.19747077, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(1.0, 0.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.61283296, 0, 0, 0) * s0.x;
		result += vec4(0.060047016, 0, 0, 0) * s0.y;
		result += vec4(0.34004188, 0, 0, 0) * s0.z;
		result += vec4(0.15439352, 0, 0, 0) * s0.w;
		result += vec4(0.28358591, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(-1.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(0.2360743, 0, 0, 0) * s0.x;
		result += vec4(0.12161585, 0, 0, 0) * s0.y;
		result += vec4(0.0020132463, 0, 0, 0) * s0.z;
		result += vec4(0.10145565, 0, 0, 0) * s0.w;
		result += vec4(-0.030855535, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(0.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.056750163, 0, 0, 0) * s0.x;
		result += vec4(-0.083877049, 0, 0, 0) * s0.y;
		result += vec4(0.035437562, 0, 0, 0) * s0.z;
		result += vec4(-0.57887846, 0, 0, 0) * s0.w;
		result += vec4(-0.062931322, 0, 0, 0) * s1.x;
	}
	{
		vec2 o = vec2(1.0, 1.0) * texel;
		vec4 s0 = texture(textureSrc, passUV + o) + vec4(-0.073815852, 0.14344446, -0.053699207, -0.14246403);
		vec4 s1 = texture(textureSrc2, passUV + o) + vec4(-0.33677015, 0, 0, 0);
		s0 = max(s0, vec4(0.0)) + vec4(1.0272365, 1.0345706, 0.86868167, 0.036550444) * min(s0, vec4(0.0));
		s1 = max(s1, vec4(0.0)) + vec4(0.96666902, 0, 0, 0) * min(s1, vec4(0.0));
		result += vec4(-0.2169469, 0, 0, 0) * s0.x;
		result += vec4(-0.059854534, 0, 0, 0) * s0.y;
		result += vec4(-0.067484349, 0, 0, 0) * s0.z;
		result += vec4(0.76586473, 0, 0, 0) * s0.w;
		result += vec4(0.09621267, 0, 0, 0) * s1.x;
	}
	result += vec4(-0.018053759, 0, 0, 0);
	result = max(result, vec4(0.0)) + vec4(0.96111727, 0, 0, 0) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_0_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.55846095, 0.056318365, 0.0060229111, 0.58870786) * s0.x;
	result += vec4(-0.36904094, -0.25209814, 0.016976601, -0.0075290706) * s0.y;
	result += vec4(0.02174703, 0.07684999, -0.80750632, -0.0033418345) * s0.z;
	result += vec4(0.29669017, -0.56558579, -0.1912657, -0.011668893) * s0.w;
	result += vec4(0.019422468, -0.44971183, -0.08564128, 0.23175898) * s1.x;
	result += vec4(-0.93768924, -0.26853067, -0.17351179, -0.11520121);
	result = max(result, vec4(0.0)) + vec4(-0.013790623, 0.3891837, -0.016245246, 1.0327178) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_1_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.21653819, 0.096763641, -0.11092465, 0.11248652) * s0.x;
	result += vec4(0.049817655, -2.1254673, -0.56239599, -0.28258899) * s0.y;
	result += vec4(0.14318006, -0.23875749, 0.19877425, -0.021408347) * s0.z;
	result += vec4(-0.24275666, 0.22843914, 0.075270034, -0.019022806) * s0.w;
	result += vec4(-0.40273315, -0.19926898, -0.0094462633, -0.48932791) * s1.x;
	result += vec4(-0.47094366, -0.03022182, -0.1343108, -0.11498569);
	result = max(result, vec4(0.0)) + vec4(-0.016797973, -0.0047365492, -0.024171477, 0.05173951) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_2_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.27014562, -0.0029441169, -0.058103379, 0.027613167) * s0.x;
	result += vec4(-0.2976903, -0.30945665, -0.24773309, -1.7172403) * s0.y;
	result += vec4(-0.084448613, 0.10609533, -0.68095922, 0.0097079528) * s0.z;
	result += vec4(-0.4620229, -0.14716236, -0.044487085, -0.23020008) * s0.w;
	result += vec4(0.3432698, -0.19154002, 0.014315045, -0.19791238) * s1.x;
	result += vec4(-0.33336037, -0.11496565, -0.12883362, -0.19483134);
	result = max(result, vec4(0.0)) + vec4(0.38824064, 0.010005776, -0.035840839, -0.071174815) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_3_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.20083305, -0.16192426, -0.12854566, 0.46104884) * s0.x;
	result += vec4(-0.14428146, 0.15460829, 0.22400527, -0.58681053) * s0.y;
	result += vec4(-0.59472919, -0.0033201403, 0.077415548, -0.027122561) * s0.z;
	result += vec4(0.54499263, 0.17575388, -0.36256894, -0.26738262) * s0.w;
	result += vec4(-0.59392947, 0.12268492, -0.40150648, -0.015863163) * s1.x;
	result += vec4(-0.524593, -0.077492774, -0.31529516, -1.1478072);
	result = max(result, vec4(0.0)) + vec4(-0.026938228, -0.066324897, -0.067181207, -0.012188942) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_4_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.17375043, -0.13794601, -0.094588965, -0.030321635) * s0.x;
	result += vec4(-0.43644941, 0.15342882, -0.32987139, 0.014852737) * s0.y;
	result += vec4(0.066104606, 0.019984273, -0.0057213278, -0.40435734) * s0.z;
	result += vec4(0.14766128, -0.1669184, 0.56431198, 0.19889989) * s0.w;
	result += vec4(-0.29402989, 0.1135475, 0.37690169, 0.15069501) * s1.x;
	result += vec4(-0.13148713, -0.1620325, 0.067596771, -0.014471671);
	result = max(result, vec4(0.0)) + vec4(0.0087661128, -0.10107288, 0.23624907, 0.044106778) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_5_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.53780437, -0.20402904, -0.1123618, -0.10830806) * s0.x;
	result += vec4(0.12466204, -0.11975279, -0.048811004, -0.039409418) * s0.y;
	result += vec4(0.24550694, 0.0049916175, 0.010628072, -0.040606581) * s0.z;
	result += vec4(0.60516548, 0.1020459, -0.14562346, 0.30092528) * s0.w;
	result += vec4(0.12942764, 0.35863885, -0.00050446147, 0.19699256) * s1.x;
	result += vec4(-0.5540967, -0.17300975, -0.11561847, -0.2708326);
	result = max(result, vec4(0.0)) + vec4(-0.028385377, 0.084774412, -0.001488631, -0.036383428) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_6_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.27169555, -0.030490372, -0.41388217, -0.025568463) * s0.x;
	result += vec4(-0.31002417, -1.434852, 0.098256364, 0.23355801) * s0.y;
	result += vec4(0.095024504, -0.00062610151, -0.053902652, -0.16961841) * s0.z;
	result += vec4(-0.28634626, 0.25663754, -0.29711285, 0.22897889) * s0.w;
	result += vec4(0.35940531, 0.14790553, 0.1594847, -0.34951463) * s1.x;
	result += vec4(-1.1450875, -0.056282852, -0.45734432, -0.10638455);
	result = max(result, vec4(0.0)) + vec4(-0.010386526, -0.091442242, -0.0044160751, -0.052724361) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f4_7_source =
R"(
void outputShader()
{
	vec4 s0 = texture(textureSrc, passUV);
	vec4 s1 = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.12361681, 0.21581008, -0.11719742, 0.44388533) * s0.x;
	result += vec4(-2.0801806, -0.44012147, -0.27332205, -0.62986714) * s0.y;
	result += vec4(-0.14870742, -0.2602419, -0.036626801, -0.094277062) * s0.z;
	result += vec4(-0.16134699, 0.53796345, -0.4718121, 0.31126237) * s0.w;
	result += vec4(0.11567913, -0.39806896, -0.038453951, 0.38530314) * s1.x;
	result += vec4(-0.23154329, 0.14850156, -0.28785071, -1.0813047);
	result = max(result, vec4(0.0)) + vec4(0.011041798, 0.23992229, 0.031401396, -0.0096122343) * min(result, vec4(0.0));
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f5_a0_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.064146727, 0.074119106, 0.061237421, -0.19758719) * a.x;
	result += vec4(0.0039588027, 0.19394137, -0.25712556, -0.0048614121) * a.y;
	result += vec4(-0.11060183, 0.068857178, 0.083427392, -0.027740868) * a.z;
	result += vec4(0.37737736, 0.37523568, 0.38014778, 0.38153368) * a.w;
	result += vec4(-0.0029747232, -0.13979761, 0.20333596, -0.069275305) * b.x;
	result += vec4(-0.0054682344, 0.11340438, 0.031177569, -0.16190441) * b.y;
	result += vec4(0.047460627, -0.027635552, -0.075230539, 0.050612524) * b.z;
	result += vec4(-0.091292597, 0.12816393, -0.12013659, 0.081630938) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f5_a1_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.23531467, -0.011862959, -0.031540904, -0.25722703) * a.x;
	result += vec4(0.054276239, 0.0026467156, -0.092832915, 0.092419468) * a.y;
	result += vec4(0.11754816, -0.12204386, -0.050537661, 0.052348509) * a.z;
	result += vec4(0.037960369, -0.20029411, 0.067345172, 0.017787021) * a.w;
	result += vec4(0.12155377, 0.015930155, -0.024636654, -0.093363352) * b.x;
	result += vec4(-0.054727253, 0.14623673, 0.001772346, -0.082300857) * b.y;
	result += vec4(-0.086421467, 0.00019704351, 0.12878491, -0.031221978) * b.z;
	result += vec4(0.065038636, -0.2135953, 0.10320193, 0.050108049) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f5_a2_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(0.083482668, 0.026639469, 0.011024003, 0.0053207735) * a.x;
	result += vec4(0.055462744, -0.090403676, -0.041550063, 0.083125748) * a.y;
	result += vec4(0.021336351, -0.24438146, 0.16168371, 0.046303958) * a.z;
	result += vec4(-0.12531532, 0.0768613, 0.13133603, -0.063151546) * a.w;
	result += vec4(0.01034816, 0.11441813, -0.059776019, -0.032851875) * b.x;
	result += vec4(0.075653896, -0.085023642, 0.13883112, -0.10807357) * b.y;
	result += vec4(0.026882323, 0.013104453, 0.093966372, 0.18659484) * b.z;
	result += vec4(0.093493171, 0.15318683, -0.20762233, -0.04782756) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_f5_a3_source =
R"(
void outputShader()
{
	vec4 a = texture(textureSrc, passUV);
	vec4 b = texture(textureSrc2, passUV);
	vec4 result = vec4(0.0);
	result += vec4(-0.19816333, 0.034158487, 0.04133166, 0.079003654) * a.x;
	result += vec4(0.052083705, 0.072410271, -0.18786724, 0.01319572) * a.y;
	result += vec4(-0.041885242, -0.043405235, -0.042702999, 0.11793804) * a.z;
	result += vec4(0.12791517, -0.044031046, -0.098553173, 0.010005831) * a.w;
	result += vec4(-0.2335279, 0.095795758, 0.08389134, 0.0023950781) * b.x;
	result += vec4(-0.21341382, -0.059342917, 0.033845421, 0.19505849) * b.y;
	result += vec4(0.098858088, 0.13617709, -0.090989679, -0.11406642) * b.z;
	result += vec4(0.047141403, 0.06043192, -0.18846764, 0.076397114) * b.w;
	colorOut0 = result;
}
)";

const std::string RendererOutputShader::s_fsrcnn_recon_source =
R"(
void outputShader()
{
	vec4 src = texture(textureSrc2, passUV);
	float lumaOld = dot(src.rgb, vec3(0.299, 0.587, 0.114));

	// Which of the 2x2 sub-pixels this output pixel is. textureSrcResolution
	// describes the LOW-resolution input, so scaling passUV by it and taking
	// the fractional half gives the position inside the source texel.
	vec2 lowRes = max(textureSrcResolution, vec2(1.0));
	vec2 sub = floor(fract(passUV * lowRes) * 2.0);
	vec4 block = texture(textureSrc, passUV);
	float lumaNew = (sub.y < 0.5)
		? ((sub.x < 0.5) ? block.x : block.y)
		: ((sub.x < 0.5) ? block.z : block.w);

	// Scale the colour by the luma ratio. Guarded because a black source texel
	// has no hue to preserve and the ratio would be a division by zero.
	vec3 outRgb = (lumaOld > 1e-3) ? src.rgb * (lumaNew / lumaOld) : vec3(lumaNew);
	colorOut0 = vec4(clamp(outRgb, vec3(0.0), vec3(1.0)), src.a);
}
)";
// Anubis water reflections: a ReShade-style screen-space reflection that runs
// in the OUTPUT CHAIN, with the game's own buffer used only as a material mask.
//
// WHERE this runs is the whole design, and getting it wrong cost a day. The
// first version marched at the moment the game finished drawing its reflection
// buffer, mid-frame. Everything failed there, and never for the reason it
// looked like:
//
//   - the scene depth read 1.0 to five decimal places on 100% of pixels,
//     because the game draws that buffer BEFORE the scene;
//   - substituting an end-of-frame depth copy made two capture sites fight
//     over one allocation and left it empty;
//   - the game's own reflection depth is valid there, but marching it put 88%
//     of rays off-screen on the first step.
//
// ReShade's SSR works on this game, and ReShade runs in post-processing - at
// the end of the frame, where colour and depth are complete. This renderer has
// that exact place, and it is already proven: GTAO and GI read full scene
// colour and depth here every frame. So the march lives here now, on data that
// is correct by construction rather than data that has to be rescued.
//
// WHAT the output chain cannot know is which surfaces are supposed to reflect.
// qUINT documents that limitation of its own SSR plainly: it "cannot
// distinguish between different materials and will add reflections on
// everything". That is the one thing the emulator can supply and ReShade
// cannot - the game picks pixel shader 340382e6fbbb3951 for its water
// reflection and draws that buffer as water geometry, so a copy of it marks
// where water is. AnubisRunWaterReflection takes that copy; this samples it.
//
// Inputs:
//   textureSrc       - the complete scene colour, and also what is reflected
//   textureDepth     - the complete scene depth
//   textureWaterMask - the game's reflection buffer, as a material mask
const std::string RendererOutputShader::s_anubis_water_ssr_source =
R"(
layout(binding = 2) uniform sampler2D textureDepth;
layout(binding = 3) uniform sampler2D textureWaterMask;

float WaterLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

vec3 WaterReconstruct(vec2 uv, float viewZ, float aspect)
{
	float tanFov = 0.57735027;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * tanFov * aspect, ndc.y * tanFov, 1.0) * viewZ;
}

void outputShader()
{
	vec3 sceneColor = texture(textureSrc, passUV).rgb;

	// THE MATERIAL MASK.
	//
	// The game draws its reflection buffer as water GEOMETRY, so that buffer is
	// written where water is and left at its clear value everywhere else. Its
	// ALPHA is not the signal - a CPU readback measured alpha as 0/255 on every
	// one of 245760 pixels, which is what killed the first attempt at this -
	// so the test is on colour presence instead.
	//
	// Gated BEFORE the march, so a non-water pixel costs nothing and can never
	// be altered.
	vec3 maskRgb = texture(textureWaterMask, passUV).rgb;
	float maskStrength = max(max(maskRgb.r, maskRgb.g), maskRgb.b);

	int waterDebug = int(faroDebugParams.x + 0.5);
	// The mask buffer itself. View 10 shows it raw; view 7 shows the same
	// sample after the capture has overwritten it with a known orange, which
	// is what distinguishes an empty source from a broken sampling path.
	//
	// Both are amplified 64x on the right half of the screen, because content
	// that is present but very dark is indistinguishable from zero at 8 bits -
	// a mistake already made once here, when a depth sitting in the last
	// percent of its range was reported as a constant.
	if (waterDebug == 10 || waterDebug == 7)
	{
		vec3 shown = (passUV.x > 0.5) ? clamp(maskRgb * 64.0, 0.0, 1.0) : maskRgb;
		colorOut0 = vec4(shown, 1.0);
		return;
	}

	if (maskStrength <= 0.004)
	{
		// Under the probe, a rejected pixel reports HOW rejected it was rather
		// than vanishing into the scene.
		//
		// The previous version returned the scene colour here even in debug,
		// so a mask that rejected everything was indistinguishable from a pass
		// that never ran - and that is exactly what happened: the pass logged
		// RUNNING 600/600 frames while the screen stayed perfectly normal.
		// Blue scaled 64x makes the difference between "exactly zero" and
		// "small but present" visible, which decides whether the threshold is
		// wrong or the buffer is empty.
		if (waterDebug == 9)
		{
			colorOut0 = vec4(0.0, 0.0, clamp(maskStrength * 64.0, 0.0, 1.0), 1.0);
			return;
		}
		colorOut0 = vec4(sceneColor, 1.0);
		return;
	}

	vec2 depthRes = max(faroBitmaskAoParams.xy, vec2(1.0));
	float aspect = depthRes.x / depthRes.y;
	float centreDepth = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	float centreZ = WaterLinearize(centreDepth);
	float farPlane = max(faroGtaoParams.w, 1.0);
	// Sky: nothing in front to reflect from. Expressed against the far plane
	// rather than as a raw-depth threshold, because a projected depth puts
	// almost the whole scene inside the last percent of 0..1 and a literal
	// 0.9999 cutoff rejected 100% of pixels when it was written that way.
	if (centreZ >= farPlane * 0.99)
	{
		colorOut0 = vec4(sceneColor, 1.0);
		return;
	}

	vec3 origin = WaterReconstruct(passUV, centreZ, aspect);
	vec3 viewDir = normalize(origin);

	// Normal from depth, two-texel CENTRAL differences.
	//
	// A one-texel forward difference produced a degenerate cross product
	// wherever neighbouring pixels held the same quantised depth, and
	// normalize() of a zero vector is undefined - which sent rays in arbitrary
	// directions. Central differences also avoid the per-pixel side-flipping
	// the GI pass documents for flat surfaces, which is exactly the water case.
	vec2 texel = 1.0 / depthRes;
	vec2 off = texel * 2.0;
	float zL = WaterLinearize(min(max(texture(textureDepth, passUV - vec2(off.x, 0.0)).x, 0.0), 1.0));
	float zR = WaterLinearize(min(max(texture(textureDepth, passUV + vec2(off.x, 0.0)).x, 0.0), 1.0));
	float zU = WaterLinearize(min(max(texture(textureDepth, passUV - vec2(0.0, off.y)).x, 0.0), 1.0));
	float zD = WaterLinearize(min(max(texture(textureDepth, passUV + vec2(0.0, off.y)).x, 0.0), 1.0));
	vec3 pL = WaterReconstruct(passUV - vec2(off.x, 0.0), zL, aspect);
	vec3 pR = WaterReconstruct(passUV + vec2(off.x, 0.0), zR, aspect);
	vec3 pU = WaterReconstruct(passUV - vec2(0.0, off.y), zU, aspect);
	vec3 pD = WaterReconstruct(passUV + vec2(0.0, off.y), zD, aspect);
	vec3 nRaw = cross(pR - pL, pD - pU);
	float nLen = length(nRaw);
	if (nLen < 1e-6)
	{
		colorOut0 = vec4(sceneColor, 1.0);
		return;
	}
	vec3 normal = nRaw / nLen;
	if (dot(normal, viewDir) > 0.0)
		normal = -normal;
	vec3 reflectDir = reflect(viewDir, normal);

	// The march, as the working ReShade implementations do it.
	//
	//  - the step GROWS geometrically (NGLighting: steplength *= RayInc, 1.012
	//    to 2.0 by preset; qUINT: ray.step *= 1.6). Uniform steps force a
	//    choice between reach and accuracy; growth samples the near part of the
	//    ray densely, where precision shows, and the far part cheaply.
	//  - the first step is proportional to DEPTH (NGLighting: position.z*0.01).
	//  - the step is divided by how far the ray turns off the view direction
	//    (qUINT: rcp(1e-3 + saturate(1 - dot(ray.dir, eyedir)))), because a ray
	//    heading away from the camera barely moves across the screen. Clamped,
	//    since that reciprocal reaches 1000.
	const int STEPS = 24;
	const int REFINE = 6;
	const float RAY_INC = 1.18;
	float maxDistance = max(faroGtaoParams.z, 8.0);

	float grazing = clamp(1.0 - dot(reflectDir, viewDir), 0.0, 1.0);
	float stepScale = min(1.0 / (grazing + 1e-3), 8.0);
	float dither = dot(floor(mod(gl_FragCoord.xy, 4.0) + 0.1), vec2(0.0625, 0.25));
	float stepLen = centreZ * 0.01 * stepScale * (0.75 + 0.5 * dither);

	float t = stepLen;
	float tPrev = 0.0;
	bool hit = false;
	vec2 hitUV = vec2(0.0);

	for (int i = 0; i < STEPS; i++)
	{
		vec3 p = origin + reflectDir * t;
		if (p.z <= 1e-3 || t > maxDistance)
			break;
		vec2 uv = vec2(p.x / (p.z * 0.57735027 * aspect), p.y / (p.z * 0.57735027)) * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
			break;

		float sceneZ = WaterLinearize(min(max(texture(textureDepth, uv).x, 0.0), 1.0));
		// Behind a surface, but by less than the distance just travelled.
		// Tying the tolerance to the STEP is what makes geometric growth safe:
		// a long stride would otherwise claim a hit on geometry it flew past,
		// smearing distant walls across the water.
		float travelled = t - tPrev;
		float behind = p.z - sceneZ;
		if (behind > 0.0 && behind < max(travelled, p.z * 0.04))
		{
			float lo = tPrev;
			float hi = t;
			for (int r = 0; r < REFINE; r++)
			{
				float mid = (lo + hi) * 0.5;
				vec3 pm = origin + reflectDir * mid;
				vec2 uvm = vec2(pm.x / (pm.z * 0.57735027 * aspect), pm.y / (pm.z * 0.57735027)) * 0.5 + 0.5;
				float zm = WaterLinearize(min(max(texture(textureDepth, uvm).x, 0.0), 1.0));
				if (zm < pm.z)
					hi = mid;
				else
					lo = mid;
			}
			vec3 pf = origin + reflectDir * hi;
			hitUV = vec2(pf.x / (pf.z * 0.57735027 * aspect), pf.y / (pf.z * 0.57735027)) * 0.5 + 0.5;
			hit = true;
			t = hi;
			break;
		}
		tPrev = t;
		t += stepLen;
		stepLen *= RAY_INC;
	}

	if (waterDebug == 9)
	{
		// Green hit, red miss, and ONLY on water - everything else returned at
		// the mask gate, so this doubles as a view of the mask. Visible
		// directly now: this pass writes the screen, not a buffer the water
		// surface shader attenuates, which is why every earlier version of
		// this view showed nothing.
		colorOut0 = hit ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
		return;
	}

	if (!hit)
	{
		colorOut0 = vec4(sceneColor, 1.0);
		return;
	}

	vec3 reflected = texture(textureSrc, hitUV).rgb;

	// Screen-space has no data past the frame, so a hit against the border is
	// reflecting whatever happened to be visible there. Cutting hard gives a
	// seam that swims as the camera turns.
	vec2 edge = min(hitUV, vec2(1.0) - hitUV);
	float edgeFade = clamp(min(edge.x, edge.y) / 0.08, 0.0, 1.0);
	// A far hit is likelier to be a thickness-test false positive than a near
	// one, and a ray turning back toward the camera carries less information.
	float lengthFade = 1.0 - clamp(t / maxDistance, 0.0, 1.0);
	float dirFade = clamp(dot(viewDir, reflectDir), 0.0, 1.0);
	// Schlick, with the 0.04 base reflectance qUINT uses for dielectrics.
	// Strong at grazing angles, weak looking straight down - which is how water
	// behaves, and without it the result reads as a mirror laid over the scene.
	float grazeFade = clamp(1.0 - dot(-viewDir, normal), 0.0, 1.0);
	float fresnel = mix(0.04, 1.0, pow(grazeFade, 4.0));

	float strength = clamp(faroGtaoParams.x, 0.0, 2.0);
	float weight = edgeFade * mix(0.5, 1.0, lengthFade) * mix(0.4, 1.0, dirFade) * fresnel * strength;
	// Also scaled by how strongly the game marked this pixel, so a surface it
	// treated as barely reflective does not become a mirror.
	weight *= clamp(maskStrength * 4.0, 0.0, 1.0);

	colorOut0 = vec4(mix(sceneColor, reflected, clamp(weight, 0.0, 1.0)), 1.0);
}
)";

const std::string RendererOutputShader::s_anubis_sssr_depth_downsample_source =
R"(
layout(binding = 0) uniform sampler2D textureSrc;

void outputShader()
{
	// Simple 2x2 average downsample for SPD depth pyramid
	vec2 texel = 1.0 / vec2(textureSize(textureSrc, 0));
	vec2 uv = passUV * vec2(textureSize(textureSrc, 0)) - 0.5;
	vec2 uv0 = (uv + vec2(-0.5, -0.5)) * texel;
	vec2 uv1 = (uv + vec2(+0.5, -0.5)) * texel;
	vec2 uv2 = (uv + vec2(-0.5, +0.5)) * texel;
	vec2 uv3 = (uv + vec2(+0.5, +0.5)) * texel;
	float s0 = texture(textureSrc, uv0).r;
	float s1 = texture(textureSrc, uv1).r;
	float s2 = texture(textureSrc, uv2).r;
	float s3 = texture(textureSrc, uv3).r;
	float avg = (s0 + s1 + s2 + s3) * 0.25;
	colorOut0 = vec4(avg, avg, avg, 1.0);
}
)";

const std::string RendererOutputShader::s_anubis_sssr_normal_reconstruct_source =
R"(
layout(binding = 0) uniform sampler2D textureDepth;
layout(binding = 1) uniform sampler2D textureNormalHelper; // unused

float WaterLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

vec3 WaterReconstruct(vec2 uv, float viewZ, float aspect)
{
	float tanFov = 0.57735027;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * tanFov * aspect, ndc.y * tanFov, 1.0) * viewZ;
}

void outputShader()
{
	vec2 depthRes = max(faroBitmaskAoParams.xy, vec2(1.0));
	float aspect = depthRes.x / depthRes.y;
	float centreDepth = min(max(texture(textureDepth, passUV).x, 0.0), 1.0);
	float centreZ = WaterLinearize(centreDepth);
	vec3 origin = WaterReconstruct(passUV, centreZ, aspect);

	vec2 texel = 1.0 / depthRes;
	vec2 off = texel * 2.0;
	float zL = WaterLinearize(min(max(texture(textureDepth, passUV - vec2(off.x,0.0)).x, 0.0), 1.0));
	float zR = WaterLinearize(min(max(texture(textureDepth, passUV + vec2(off.x,0.0)).x, 0.0), 1.0));
	float zU = WaterLinearize(min(max(texture(textureDepth, passUV - vec2(0.0,off.y)).x, 0.0), 1.0));
	float zD = WaterLinearize(min(max(texture(textureDepth, passUV + vec2(0.0,off.y)).x, 0.0), 1.0));
	vec3 pL = WaterReconstruct(passUV - vec2(off.x,0.0), zL, aspect);
	vec3 pR = WaterReconstruct(passUV + vec2(off.x,0.0), zR, aspect);
	vec3 pU = WaterReconstruct(passUV - vec2(0.0,off.y), zU, aspect);
	vec3 pD = WaterReconstruct(passUV + vec2(0.0,off.y), zD, aspect);
	vec3 nRaw = cross(pR - pL, pD - pU);
	float nLen = length(nRaw);
	vec3 normal = nLen > 1e-6 ? normalize(nRaw) : vec3(0.0,0.0,1.0);
	if(dot(normal, normalize(origin)) > 0.0) normal = -normal;
	normal = normalize(normal);
	// Encode to [0,1]
	colorOut0 = vec4(normal * 0.5 + 0.5, 1.0);
}
)";

const std::string RendererOutputShader::s_anubis_sssr_march_source =
R"(
layout(binding = 0) uniform sampler2D textureScene;
layout(binding = 1) uniform sampler2D textureDepthPyramid; // depth mips packed as layers? simplified to base
layout(binding = 2) uniform sampler2D textureNormal;
layout(binding = 3) uniform sampler2D textureWaterMask;
layout(binding = 4) uniform sampler2D textureBlueNoise;

float WaterLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

vec3 WaterReconstruct(vec2 uv, float viewZ, float aspect)
{
	float tanFov = 0.57735027;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * tanFov * aspect, ndc.y * tanFov, 1.0) * viewZ;
}

float LoadDepthMip(vec2 uv, int mip)
{
	// Simplificado: usamos textura base para prototipo
	return texture(textureDepthPyramid, uv).r;
}

vec3 ScreenToView(vec3 p)
{
	return p;
}

void outputShader()
{
	vec3 sceneColor = texture(textureScene, passUV).rgb;
	vec3 maskRgb = texture(textureWaterMask, passUV).rgb;
	float maskStrength = max(max(maskRgb.r, maskRgb.g), maskRgb.b);
	if(maskStrength <= 0.004){
		colorOut0 = vec4(sceneColor,1.0);
		return;
	}

	vec2 depthRes = max(faroBitmaskAoParams.xy, vec2(1.0));
	float aspect = depthRes.x / depthRes.y;
	float centreDepth = min(max(texture(textureDepthPyramid, passUV).x, 0.0), 1.0);
	float centreZ = WaterLinearize(centreDepth);
	vec3 origin = WaterReconstruct(passUV, centreZ, aspect);
	vec3 viewDir = normalize(origin);

	vec3 normalEnc = texture(textureNormal, passUV).rgb;
	vec3 normal = normalize(normalEnc * 2.0 - 1.0);
	if(dot(normal, viewDir) > 0.0) normal = -normal;

	vec3 reflectDir = reflect(viewDir, normal);

	// Marchado jerárquico simplificado: 1 muestra, 32 pasos con incremento geométrico
	const int STEPS = 32;
	const float RAY_INC = 1.18;
	float maxDistance = max(faroGtaoParams.z, 8.0);
	float grazing = clamp(1.0 - dot(reflectDir, viewDir), 0.0, 1.0);
	float stepScale = min(1.0 / (grazing + 1e-3), 8.0);
	float dither = texture(textureBlueNoise, passUV * 4.0).r;
	float stepLen = centreZ * 0.01 * stepScale * (0.75 + 0.5 * dither);
	float t = stepLen;
	float tPrev = 0.0;
	bool hit = false;
	vec2 hitUV = vec2(0.0);
	for(int i=0;i<STEPS;i++){
		vec3 p = origin + reflectDir * t;
		if(p.z <= 1e-3 || t > maxDistance) break;
		vec2 uv = vec2(p.x / (p.z * 0.57735027 * aspect), p.y / (p.z * 0.57735027)) * 0.5 + 0.5;
		if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
		float sceneZ = WaterLinearize(min(max(texture(textureDepthPyramid, uv).x, 0.0), 1.0));
		float travelled = t - tPrev;
		float behind = p.z - sceneZ;
		if(behind > 0.0 && behind < max(travelled, p.z * 0.04)){
			hitUV = uv;
			hit = true;
			break;
		}
		tPrev = t;
		t += stepLen;
		stepLen *= RAY_INC;
	}

	if(!hit){
		colorOut0 = vec4(sceneColor,1.0);
		return;
	}

	vec3 reflected = texture(textureScene, hitUV).rgb;
	vec2 edge = min(hitUV, vec2(1.0) - hitUV);
	float edgeFade = clamp(min(edge.x, edge.y) / 0.08, 0.0, 1.0);
	float lengthFade = 1.0 - clamp(t / maxDistance, 0.0, 1.0);
	float dirFade = clamp(dot(viewDir, reflectDir), 0.0, 1.0);
	float grazeFade = clamp(1.0 - dot(-viewDir, normal), 0.0, 1.0);
	float fresnel = mix(0.04, 1.0, pow(grazeFade, 4.0));
	float strength = clamp(faroGtaoParams.x, 0.0, 2.0);
	float weight = edgeFade * mix(0.5, 1.0, lengthFade) * mix(0.4, 1.0, dirFade) * fresnel * strength;
	weight *= clamp(maskStrength * 4.0, 0.0, 1.0);

	colorOut0 = vec4(mix(sceneColor, reflected, clamp(weight,0.0,1.0)), 1.0);
}
)";

const std::string RendererOutputShader::s_anubis_sssr_temporal_accumulate_source =
R"(
layout(binding = 0) uniform sampler2D textureCurrent;
layout(binding = 1) uniform sampler2D textureHistory;
layout(binding = 2) uniform sampler2D textureDepthCurr;
layout(binding = 3) uniform sampler2D textureDepthPrev;
layout(binding = 4) uniform sampler2D textureNormalCurr;
layout(binding = 5) uniform sampler2D textureNormalPrev;
layout(binding = 6) uniform sampler2D textureWaterMask;

float WaterLinearize(float d)
{
	float farPlane = max(faroGtaoParams.w, 1.0);
	return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}

void outputShader()
{
	vec3 current = texture(textureCurrent, passUV).rgb;
	vec3 history = texture(textureHistory, passUV).rgb;
	vec3 maskRgb = texture(textureWaterMask, passUV).rgb;
	float maskStrength = max(max(maskRgb.r, maskRgb.g), maskRgb.b);
	if(maskStrength <= 0.004){
		colorOut0 = vec4(current,1.0);
		return;
	}

	float depthCurr = WaterLinearize(min(max(texture(textureDepthCurr, passUV).x, 0.0), 1.0));
	float depthPrev = WaterLinearize(min(max(texture(textureDepthPrev, passUV).x, 0.0), 1.0));
	vec3 normalCurrEnc = texture(textureNormalCurr, passUV).rgb;
	vec3 normalPrevEnc = texture(textureNormalPrev, passUV).rgb;
	vec3 normalCurr = normalize(normalCurrEnc * 2.0 - 1.0);
	vec3 normalPrev = normalize(normalPrevEnc * 2.0 - 1.0);

	float depthDiff = abs(depthCurr - depthPrev) / max(depthCurr, 1e-5);
	float normalSim = max(0.0, dot(normalCurr, normalPrev));

	// Rejection simple sin motion vectors: misma coordenada
	float accept = 0.0;
	if(depthDiff < 0.02 && normalSim > 0.98){
		accept = 1.0;
	}

	// EMA temporal con factor de estabilidad
	float stability = clamp(faroGtaoParams.y, 0.0, 1.0);
	float blend = mix(1.0, 0.9, stability) * accept;
	vec3 outCol = mix(current, history, blend);

	colorOut0 = vec4(outCol,1.0);
}
)";
