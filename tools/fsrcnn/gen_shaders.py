#!/usr/bin/env python3
"""Generates the FSRCNN-small shader chain from the extracted .pb weights.

Generated rather than written by hand: the model carries ~600 weights across
five layers, and one transposed index would produce an image that looks
plausible and is wrong everywhere - the kind of bug that cannot be found by
reading the code.

Architecture, read from the checkpoint rather than assumed:
    f1 [5,5,1,32]   feature extraction   1 -> 32
    f2 [1,1,32,5]   shrinking           32 -> 5
    f3 [3,3,5,5]    mapping              5 -> 5
    f4 [1,1,5,32]   expanding            5 -> 32
    f5 [1,1,32,4]   sub-pixel           32 -> 4   (the 2x2 block for x2)
f1..f4 are each followed by PReLU: max(x,0) + alpha*min(x,0), alpha per channel.

Two facts drive the structure:

1. FSRCNN works on LUMA ONLY - f1 takes a single input channel. Chroma is
   scaled by interpolation, so colour detail is not reconstructed, only
   luminance. Standard for super-resolution, but it bounds the result.

2. A 32-channel feature map is eight RGBA textures and this renderer's output
   shader layout offers three sampler bindings. The way out: f2, f4 and f5 are
   1x1 convolutions, hence plain linear sums, so they split into passes that
   ACCUMULATE with additive blending while each reads only two textures. No new
   descriptor layout, and nothing shared with FSR1/SMAA/TAA/GI changes.

TensorFlow stores kernels as [kh, kw, in, out], last dimension innermost, so
index = ((ky*kw + kx)*in + ic)*out + oc.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_weights import parse_graph

pb_path, out_path = sys.argv[1], sys.argv[2]
consts = {name: (shape, vals) for name, shape, vals in parse_graph(pb_path)}

required = ("f1", "f2", "f3", "f4", "f5", "b1", "b2", "b3", "b4",
            "alpha1", "alpha2", "alpha3", "alpha4")
for key in required:
    if key not in consts:
        sys.exit(f"missing tensor {key} - model layout differs from FSRCNN-small")


def w(n):
    return consts[n][1]


def shp(n):
    return consts[n][0]


def kern(n, ky, kx, ic, oc):
    kh, kw, nin, nout = shp(n)
    return w(n)[((ky * kw + kx) * nin + ic) * nout + oc]


def fl(x):
    return f"{x:.8g}"


def vec4_of(vals):
    return "vec4(" + ", ".join(fl(v) if isinstance(v, float) else v for v in vals) + ")"


def prelu_line(alpha_name, base, count=4):
    a = w(alpha_name)
    slopes = [a[base + c] if c < count else 0.0 for c in range(4)]
    return ("\tresult = max(result, vec4(0.0)) + " + vec4_of(slopes) + " * min(result, vec4(0.0));")


LUMA = "vec3(0.299, 0.587, 0.114)"
passes = []

# ---------- f1: luma -> 32 features, eight passes of four channels ----------
# Luma is taken here rather than in a separate pass: a dedicated luma target
# would cost another full-resolution write for one dot product.
kh, kw, _, _ = shp("f1")
for p in range(8):
    L = ["void outputShader()", "{",
         "\tvec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));",
         "\tvec4 result = vec4(0.0);"]
    for ky in range(kh):
        for kx in range(kw):
            oy, ox = ky - kh // 2, kx - kw // 2
            co = [kern("f1", ky, kx, 0, p * 4 + c) for c in range(4)]
            L.append(f"\tresult += {vec4_of(co)} * dot(texture(textureSrc, passUV + "
                     f"vec2({ox}.0, {oy}.0) * texel).rgb, {LUMA});")
    L.append(f"\tresult += {vec4_of([w('b1')[p * 4 + c] for c in range(4)])};")
    L.append(prelu_line("alpha1", p * 4))
    L += ["\tcolorOut0 = result;", "}"]
    passes.append((f"f1_{p}", "\n".join(L)))

# ---------- f2: 32 -> 5, accumulated eight input channels per pass ----------
# The 5 channels occupy two targets: four in the first, one in the second.
# Bias and PReLU are applied by the mapping pass instead of here, because while
# these accumulation passes run the sum is still incomplete.
for ot, ocount in ((0, 4), (1, 1)):
    for acc in range(4):
        L = ["void outputShader()", "{",
             "\tvec4 a = texture(textureSrc, passUV);",
             "\tvec4 b = texture(textureSrc2, passUV);",
             "\tvec4 result = vec4(0.0);"]
        for lane in range(8):
            ic = acc * 8 + lane
            src = ("a" if lane < 4 else "b") + "." + "xyzw"[lane % 4]
            co = [kern("f2", 0, 0, ic, ot * 4 + c) if c < ocount else 0.0 for c in range(4)]
            L.append(f"\tresult += {vec4_of(co)} * {src};")
        L += ["\tcolorOut0 = result;", "}"]
        passes.append((f"f2_t{ot}_a{acc}", "\n".join(L)))

# ---------- f3: mapping, 5 -> 5, 3x3, reading both shrink targets ----------
kh3, kw3, _, _ = shp("f3")
for ot, ocount in ((0, 4), (1, 1)):
    L = ["void outputShader()", "{",
         "\tvec2 texel = 1.0 / max(textureSrcResolution, vec2(1.0));",
         "\tvec4 result = vec4(0.0);"]
    for ky in range(kh3):
        for kx in range(kw3):
            oy, ox = ky - kh3 // 2, kx - kw3 // 2
            L.append(f"\t{{")
            L.append(f"\t\tvec2 o = vec2({ox}.0, {oy}.0) * texel;")
            # Bias and PReLU from f2 are applied on the fly, since f2's passes
            # could not: the sum was still partial while they ran.
            b2 = w("b2")
            bias0 = vec4_of([b2[c] for c in range(4)])
            bias1 = vec4_of([b2[4] if c == 0 else 0.0 for c in range(4)])
            L.append(f"\t\tvec4 s0 = texture(textureSrc, passUV + o) + {bias0};")
            L.append(f"\t\tvec4 s1 = texture(textureSrc2, passUV + o) + {bias1};")
            L.append("\t\ts0 = max(s0, vec4(0.0)) + " + vec4_of([w("alpha2")[c] for c in range(4)]) + " * min(s0, vec4(0.0));")
            L.append("\t\ts1 = max(s1, vec4(0.0)) + " + vec4_of([w("alpha2")[4] if c == 0 else 0.0 for c in range(4)]) + " * min(s1, vec4(0.0));")
            for ic in range(5):
                src = ("s0." + "xyzw"[ic]) if ic < 4 else "s1.x"
                co = [kern("f3", ky, kx, ic, ot * 4 + c) if c < ocount else 0.0 for c in range(4)]
                L.append(f"\t\tresult += {vec4_of(co)} * {src};")
            L.append("\t}")
    b3 = w("b3")
    L.append(f"\tresult += {vec4_of([b3[ot * 4 + c] if ot * 4 + c < 5 else 0.0 for c in range(4)])};")
    L.append(prelu_line("alpha3", ot * 4, ocount if ot == 0 else 1))
    L += ["\tcolorOut0 = result;", "}"]
    passes.append((f"f3_t{ot}", "\n".join(L)))

# ---------- f4: expanding, 5 -> 32, eight passes of four channels ----------
for p in range(8):
    L = ["void outputShader()", "{",
         "\tvec4 s0 = texture(textureSrc, passUV);",
         "\tvec4 s1 = texture(textureSrc2, passUV);",
         "\tvec4 result = vec4(0.0);"]
    for ic in range(5):
        src = ("s0." + "xyzw"[ic]) if ic < 4 else "s1.x"
        co = [kern("f4", 0, 0, ic, p * 4 + c) for c in range(4)]
        L.append(f"\tresult += {vec4_of(co)} * {src};")
    L.append(f"\tresult += {vec4_of([w('b4')[p * 4 + c] for c in range(4)])};")
    L.append(prelu_line("alpha4", p * 4))
    L += ["\tcolorOut0 = result;", "}"]
    passes.append((f"f4_{p}", "\n".join(L)))

# ---------- f5: 32 -> 4 sub-pixel values, accumulated ----------
# No activation: this is the output layer, and the four channels are the 2x2
# block that the reconstruction pass unpacks.
for acc in range(4):
    L = ["void outputShader()", "{",
         "\tvec4 a = texture(textureSrc, passUV);",
         "\tvec4 b = texture(textureSrc2, passUV);",
         "\tvec4 result = vec4(0.0);"]
    for lane in range(8):
        ic = acc * 8 + lane
        src = ("a" if lane < 4 else "b") + "." + "xyzw"[lane % 4]
        co = [kern("f5", 0, 0, ic, c) for c in range(4)]
        L.append(f"\tresult += {vec4_of(co)} * {src};")
    L += ["\tcolorOut0 = result;", "}"]
    passes.append((f"f5_a{acc}", "\n".join(L)))

# ---------- reconstruction at 2x ----------
# Unpacks the sub-pixel block into the enlarged image and restores colour.
#
# The network produced LUMA only, so chroma comes from interpolating the source.
# Working in ratios rather than converting through YCbCr keeps the original hue
# and saturation exactly: only brightness is replaced, which is all the network
# predicted.
recon = """void outputShader()
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
}"""
passes.append(("recon", recon))

with open(out_path, "w", encoding="utf-8") as f:
    f.write("// GENERATED by tools/fsrcnn/gen_shaders.py - do not hand-edit, regenerate.\n")
    f.write("// Weights from Saafke/FSRCNN_Tensorflow (Apache-2.0); licence text in\n")
    f.write("// tools/fsrcnn/LICENSE-FSRCNN_Tensorflow.\n//\n")
    f.write("// Architecture as read from the checkpoint:\n")
    for n in ("f1", "f2", "f3", "f4", "f5"):
        f.write(f"//   {n} {shp(n)}\n")
    f.write("\n")
    for name, src in passes:
        f.write(f"const std::string RendererOutputShader::s_fsrcnn_{name}_source =\nR\"(\n{src}\n)\";\n\n")

print(f"{len(passes)} passes -> {out_path}")
for n in ("f1", "f2", "f3", "f4", "f5"):
    print(f"  {n} {shp(n)}  n={len(w(n))}")
print("  pass names: " + ", ".join(n for n, _ in passes))
