#pragma once
// Onboarding shaders (Paper p-R-0 « Onboarding — fenêtre de lancement »).
//
// The three Paper shader nodes of the onboarding are @paper-design/shaders 0.0.82 (MIT-style licence, see
// https://github.com/paper-design/shaders): HalftoneDots (screens 1 and 7), GemSmoke (screens 4, 5, 5.1) and
// GrainGradient (screen 6). This file is a line-for-line HLSL port of that library's vertex shader (sizing: fit,
// scale, origin, offset) and of the three fragment shaders, run on Direct3D 11 and read back as premultiplied
// BGRA pixels the Direct2D chrome draws like any bitmap (HWND target on screen, WIC target in tests).
//
//  * Same maths, same inputs: the library renders at max(devicePixelRatio, 2) canvas pixels per CSS pixel; so
//    does Render() (`pixelRatio`), then a second pass brings the image down to device pixels.
//  * Time: GemSmoke and GrainGradient are animated in Paper (speed 1) and periodic by construction (2π s and
//    10π s); HalftoneDots is a still image filter in Paper. Its loop is ours: the dot lattice slides by exactly
//    one lattice step (`drift`), which gives back the very same picture — a seamless loop with no blend.
//  * Hardware device first; WARP (software) only renders still frames (Animated() is false): 36 texture reads
//    per pixel is not something to run on the CPU at the display's rate.
//  * Cost: each node keeps its own targets, so a frame of the loop can redraw and read back only the part
//    that moves (`region`: the logo of the halftone, a fifth of the node; the grain around it never changes).
//  * The compiler (d3dcompiler_47.dll, part of Windows) is loaded on demand; any failure leaves Ready() false
//    and the caller falls back to the plain artwork.
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Onboarding {
namespace Shaders {

enum class Kind { Halftone = 0, GemSmoke = 1, GrainGradient = 2 };

// One constant buffer for the three shaders, 16-byte rows (HLSL packing).
struct Uniforms {
    float resolution[2] = {1, 1}; float pixelRatio = 2, imageAspect = 1;
    float originX = .5f, originY = .5f, worldW = 0, worldH = 0;
    float fit = 1, scale = 1, rotation = 0, offsetX = 0;
    float offsetY = 0, time = 0; float drift[2] = {0, 0};
    float colorFront[4] = {1, 1, 1, 1}, colorBack[4] = {0, 0, 0, 0}, colorInner[4] = {0, 0, 0, 0};
    float colors[6][4] = {};
    float p0[4] = {}, p1[4] = {}, p2[4] = {};
};
static_assert(sizeof(Uniforms) == 16 * 16, "constant buffer layout");

inline void SetColor(float out[4], unsigned rgb, float alpha = 1) {
    out[0] = ((rgb >> 16) & 255) / 255.f; out[1] = ((rgb >> 8) & 255) / 255.f; out[2] = (rgb & 255) / 255.f; out[3] = alpha;
}

// ---- Paper values (get_jsx of the shader nodes) ---------------------------------------------------------
// HalftoneDots 1DNQ-0 / 1CEY-0: contrast .58, inverted, grid square, radius 1.19, size .67, scale .46,
// grainSize .39, type gooey, fit cover, grainOverlay .17, grainMixer .12, front #FFFFFF, back transparent.
// `drift`: 0..1, one full turn of our loop (0 = the Paper frame).
// `wholeNode` (ours, asked by the designer): Paper's shader only draws dots inside the picture's box, which shows
// as a square of faint dots around the logo; with it the lattice covers the whole node — outside the box the
// picture's own edge colour is read (its dark background), so the faint dots simply go on to the window's edges.
// `raise`: the picture's centre moved up by this share of the picture box (the logo stays where board 1 has it
// when the node is the whole window).
// `logoScale`, `logoAlpha` (ours, the end of the flow): the picture shrinks about its centre and fades towards
// its own background while the lattice stays where it is — the logo leaves, the faint dots go on.
inline Uniforms Halftone(float drift = 0, bool wholeNode = false, float raise = 0, float logoScale = 1, float logoAlpha = 1) {
    Uniforms u; u.fit = 2; u.scale = .46f; u.imageAspect = 1; u.offsetY = -raise; u.p2[2] = wholeNode ? 1.f : 0.f;
    u.colorInner[0] = logoScale; u.colorInner[1] = logoAlpha;
    SetColor(u.colorFront, 0xFFFFFF, 1); SetColor(u.colorBack, 0x000000, 0);
    u.p0[0] = 1.19f; u.p0[1] = .58f; u.p0[2] = .67f; u.p0[3] = .12f;     // radius, contrast, size, grainMixer
    u.p1[0] = .17f; u.p1[1] = .39f; u.p1[2] = 0; u.p1[3] = 1;            // grainOverlay, grainSize, grid (square), type (gooey)
    u.p2[0] = 0; u.p2[1] = 1;                                            // originalColors, inverted
    // The lattice is six interleaved grids, 1/6 cell apart: a slide of 1/6 cell is the identity.
    const float turn = drift - std::floor(drift);
    u.drift[0] = turn / 6; u.drift[1] = -turn / 6;
    return u;
}
// The Réglages drawer's « navigateur par défaut » card (Paper « 01 · Réglages — racine », HalftoneDots 1DQY-0): the
// same node and picture as the onboarding with a larger cell and picture — size .69, scale .63 — and, as the
// onboarding (designer's request, 2026-11-23), the lattice over the whole node (`wholeNode`): no square of dots
// around the logo, the faint dots run to the card's edges. Same loop as the onboarding (`drift`).
inline Uniforms HalftoneCard(float drift = 0) {
    Uniforms u = Halftone(drift, true); u.scale = .63f; u.p0[2] = .69f;
    return u;
}
// GemSmoke 1CCQ-0 / 1CE9-0 / 1CAL-0: size .8, outerDistortion .6, innerDistortion .8, outerGlow 0, innerGlow 1,
// offset -.58, scale .65, angle 0, colors #333333 #E7E6DF, inner #FAFAF5, back transparent, contrast(108%).
// Still frame (reduced motion, software device, tests): the instant of the Paper capture kept in
// design/onboarding-evidence/paper/ (found by scanning the period; Paper's exported `frame` is the editor's
// running clock, not the frame of its screenshot).
inline constexpr float GemStillSeconds = 3.35f;
inline constexpr float GemPeriod = 6.283185307179586f;          // every time term is cos(time + …)
inline Uniforms GemSmoke(float seconds) {
    Uniforms u; u.fit = 1; u.scale = .65f; u.imageAspect = 474.f / 447.f; u.time = std::fmod(seconds, GemPeriod);
    SetColor(u.colors[0], 0x333333); SetColor(u.colors[1], 0xE7E6DF); SetColor(u.colorInner, 0xFAFAF5); SetColor(u.colorBack, 0, 0);
    u.p0[0] = .8f; u.p0[1] = .6f; u.p0[2] = 0; u.p0[3] = 1;              // innerDistortion, outerDistortion, outerGlow, innerGlow
    u.p1[0] = -.58f; u.p1[1] = 0; u.p1[2] = .8f; u.p1[3] = 2;            // offset, angle, size, colorsCount
    u.p2[0] = 1 / 4096.f; u.p2[1] = 1 / 3863.f; u.p2[2] = 1.08f;         // the library's texel (4096-wide processed image), CSS contrast
    return u;
}
// GrainGradient 1CJ9-0: speed 1, scale 1, softness .7, intensity .15, noise 0, shape wave,
// colors #0AF4FF #0099A0 #01292C, back transparent (over the node's own #DEFDFF background).
inline constexpr float GrainStillSeconds = 13.6f;                // still frame: as GemStillSeconds
inline constexpr float GrainPeriod = 31.41592653589793f;        // wave: cos(.5x - .4T) sin(1.5x + .2T) (.75 + .25 cos(.6T))
inline Uniforms GrainGradient(float seconds) {
    Uniforms u; u.fit = 1; u.scale = 1; u.time = std::fmod(seconds, GrainPeriod);
    SetColor(u.colors[0], 0x0AF4FF); SetColor(u.colors[1], 0x0099A0); SetColor(u.colors[2], 0x01292C); SetColor(u.colorBack, 0, 0);
    u.p0[0] = .7f; u.p0[1] = .15f; u.p0[2] = 0; u.p0[3] = 1;             // softness, intensity, noise, shape (wave)
    u.p1[0] = 3;                                                         // colorsCount
    return u;
}

namespace Detail {
inline const char* Source() {
    return R"HLSL(
cbuffer U : register(b0) {
  float2 u_resolution; float u_pixelRatio; float u_imageAspectRatio;
  float u_originX; float u_originY; float u_worldWidth; float u_worldHeight;
  float u_fit; float u_scale; float u_rotation; float u_offsetX;
  float u_offsetY; float u_time; float2 u_drift;
  float4 u_colorFront; float4 u_colorBack; float4 u_colorInner;
  float4 u_colors[6];
  float4 p0; float4 p1; float4 p2;
};
Texture2D u_image : register(t0);
SamplerState u_linear : register(s0);
#define PI 3.14159265358979323846
#define TWO_PI 6.28318530718

struct V {
  float4 pos : SV_Position;
  float2 objectUV : TEXCOORD0; float2 objectBoxSize : TEXCOORD1;
  float2 patternUV : TEXCOORD2; float2 patternBoxSize : TEXCOORD3;
  float2 imageUV : TEXCOORD4;
};
float2 rotate(float2 uv, float th) { float c = cos(th), s = sin(th); return float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y); }
float glmod(float x, float y) { return x - y * floor(x / y); }
float3 glmod3(float3 x, float y) { return x - y * floor(x / y); }
float2 glmod2(float2 x, float y) { return x - y * floor(x / y); }
float sst(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
float2 sst2(float2 a, float2 b, float2 x) { float2 t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
float lst(float a, float b, float x) { return saturate((x - a) / (b - a)); }

float3 getBoxSize(float boxRatio, float2 givenBoxSize) {
  float2 box = float2(0., 0.);
  box.x = boxRatio * min(givenBoxSize.x / boxRatio, givenBoxSize.y);
  float noFitBoxWidth = box.x;
  if (u_fit == 1.) box.x = boxRatio * min(u_resolution.x / boxRatio, u_resolution.y);
  else if (u_fit == 2.) box.x = boxRatio * max(u_resolution.x / boxRatio, u_resolution.y);
  box.y = box.x / boxRatio;
  return float3(box, noFitBoxWidth);
}
// @paper-design/shaders vertex-shader.ts, on a full-target triangle.
V VS(uint id : SV_VertexID) {
  V o;
  float2 p = float2(id == 2 ? 3. : -1., id == 1 ? 3. : -1.);
  o.pos = float4(p, 0., 1.);
  float2 uv = p * .5;
  float2 boxOrigin = float2(.5 - u_originX, u_originY - .5);
  float2 givenBoxSize = max(float2(u_worldWidth, u_worldHeight), float2(1., 1.)) * u_pixelRatio;
  float r = u_rotation * PI / 180.;
  float2 graphicOffset = float2(-u_offsetX, u_offsetY);

  float2 fixedGiven = float2(u_worldWidth == 0. ? u_resolution.x : givenBoxSize.x, u_worldHeight == 0. ? u_resolution.y : givenBoxSize.y);
  o.objectBoxSize = getBoxSize(1., fixedGiven).xy;
  float2 objectWorldScale = u_resolution / o.objectBoxSize;
  o.objectUV = uv * objectWorldScale;
  o.objectUV += boxOrigin * (objectWorldScale - 1.);
  o.objectUV += graphicOffset;
  o.objectUV /= u_scale;
  o.objectUV = rotate(o.objectUV, r);

  float2 patternGiven = fixedGiven;
  float patternBoxRatio = patternGiven.x / patternGiven.y;
  float3 boxSizeData = getBoxSize(patternBoxRatio, patternGiven);
  o.patternBoxSize = boxSizeData.xy;
  float2 patternBoxScale = u_resolution / o.patternBoxSize;
  o.patternUV = uv;
  o.patternUV += graphicOffset / patternBoxScale;
  o.patternUV += boxOrigin;
  o.patternUV -= boxOrigin / patternBoxScale;
  o.patternUV *= u_resolution;
  o.patternUV /= u_pixelRatio;
  if (u_fit > 0.) o.patternUV *= (boxSizeData.z / o.patternBoxSize.x);
  o.patternUV /= u_scale;
  o.patternUV = rotate(o.patternUV, r);
  o.patternUV += boxOrigin / patternBoxScale;
  o.patternUV -= boxOrigin;
  o.patternUV *= .01;

  float2 imageBoxSize;
  if (u_fit == 1.) imageBoxSize.x = min(u_resolution.x / u_imageAspectRatio, u_resolution.y) * u_imageAspectRatio;
  else if (u_fit == 2.) imageBoxSize.x = max(u_resolution.x / u_imageAspectRatio, u_resolution.y) * u_imageAspectRatio;
  else imageBoxSize.x = min(10.0, 10.0 / u_imageAspectRatio * u_imageAspectRatio);
  imageBoxSize.y = imageBoxSize.x / u_imageAspectRatio;
  float2 imageBoxScale = u_resolution / imageBoxSize;
  o.imageUV = uv * imageBoxScale;
  o.imageUV += boxOrigin * (imageBoxScale - 1.);
  o.imageUV += graphicOffset;
  o.imageUV /= u_scale;
  o.imageUV.x *= u_imageAspectRatio;
  o.imageUV = rotate(o.imageUV, r);
  o.imageUV.x /= u_imageAspectRatio;
  o.imageUV += .5;
  o.imageUV.y = 1. - o.imageUV.y;
  return o;
}

// ---------------------------------------------------------------- HalftoneDots (halftone-dots.ts)
float hash21(float2 p) {
  p = frac(p * float2(0.3183099, 0.3678794)) + 0.1;
  p += dot(p, p + 19.19);
  return frac(p.x * p.y);
}
float valueNoise(float2 st) {
  float2 i = floor(st); float2 f = frac(st);
  float a = hash21(i), b = hash21(i + float2(1.0, 0.0)), c = hash21(i + float2(0.0, 1.0)), d = hash21(i + float2(1.0, 1.0));
  float2 u = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float getUvFrame(float2 uv, float2 pad) {
  float aa = 0.0001;
  float left = sst(-pad.x, -pad.x + aa, uv.x);
  float right = sst(1.0 + pad.x, 1.0 + pad.x - aa, uv.x);
  float bottom = sst(-pad.y, -pad.y + aa, uv.y);
  float top = sst(1.0 + pad.y, 1.0 + pad.y - aa, uv.y);
  return left * right * bottom * top;
}
float sigmoid(float x, float k) { return 1.0 / (1.0 + exp(-k * (x - 0.5))); }
float getLumAtPx(float2 uv, float contrast) {
  float4 tex = u_image.SampleLevel(u_linear, uv, 0);
  float3 color = float3(sigmoid(tex.r, contrast), sigmoid(tex.g, contrast), sigmoid(tex.b, contrast));
  float lum = dot(float3(0.2126, 0.7152, 0.0722), color);
  lum = lerp(1., lum, tex.a);
  return p2.y > .5 ? (1. - lum) : lum;
}
float getGooeyBall(float2 uv, float r, float baseR) {
  float d = length(uv - .5);
  float sizeRadius = p1.z == 1. ? .42 : .3;
  sizeRadius = lerp(sizeRadius * baseR, 0., r);
  d = 1. - sst(0., sizeRadius, d);
  return pow(abs(d), 2. + baseR);
}
float getLumBall(float2 p, float2 pad, float2 inCellOffset, float contrast, float baseR, float stepSize, float restLum) {
  p += inCellOffset;
  float2 uv_i = floor(p); float2 uv_f = frac(p);
  // `u_drift` (ours): the lattice slides, the picture it samples stays where it is.
  float2 samplingUV = (uv_i + .5 - inCellOffset - u_drift) * pad + float2(.5, .5);
  float outOfFrame = p2.z > .5 ? 1. : getUvFrame(samplingUV, pad * stepSize);   // p2.z (ours): dots over the whole node
  // u_colorInner.xy (ours): the picture's scale about its centre and how much of it is left.
  float2 pictureUV = u_colorInner.x > 0. ? (samplingUV - .5) / u_colorInner.x + .5 : samplingUV;
  float lum = lerp(restLum, getLumAtPx(pictureUV, contrast), saturate(u_colorInner.y));
  return getGooeyBall(uv_f, lum, baseR) * outOfFrame;
}
float4 PSHalftone(V v) : SV_Target {
  float u_radius = p0.x, u_contrast = p0.y, u_size = p0.z, u_grainMixer = p0.w, u_grainOverlay = p1.x, u_grainSize = p1.y;
  float stepMultiplier = 6.;   // gooey
  float cellsPerSide = lerp(300., 7., pow(u_size, .7)) / stepMultiplier;
  float cellSizeY = 1. / cellsPerSide;
  float2 pad = cellSizeY * float2(1. / u_imageAspectRatio, 1.);
  float2 uv = (v.imageUV - float2(.5, .5)) / pad + u_drift;
  float contrast = lerp(0., 15., pow(u_contrast, 1.5));
  float baseRadius = u_radius;
  float totalShape = 0.;
  float restLum = getLumAtPx(float2(.002, .002), contrast);   // the picture's background: what the dots show without the logo
  float stepSize = 1. / stepMultiplier;
  for (int ix = 0; ix < 6; ix++) {
    for (int iy = 0; iy < 6; iy++) {
      float2 offset = float2(-.5 + ix * stepSize, -.5 + iy * stepSize);
      totalShape += getLumBall(uv, pad, offset, contrast, baseRadius, stepSize, restLum);
    }
  }
  float aa = fwidth(totalShape);
  float finalShape = sst(.5 - aa, .5 + aa, totalShape);
  float2 grainSize = lerp(2000., 200., u_grainSize) * float2(1., 1. / u_imageAspectRatio);
  float2 grainUV = (v.imageUV - .5) * grainSize + .5;
  if (u_grainMixer > 0.) {
    float grain = valueNoise(grainUV);
    grain = sst(.55, .7 + .2 * u_grainMixer, grain) * u_grainMixer;
    finalShape = lerp(finalShape, 0., grain);
  }
  float3 fgColor = u_colorFront.rgb * u_colorFront.a;
  float3 bgColor = u_colorBack.rgb * u_colorBack.a;
  float3 color = fgColor * finalShape;
  float opacity = u_colorFront.a * finalShape;
  color += bgColor * (1. - opacity);
  opacity += u_colorBack.a * (1. - opacity);
  if (u_grainOverlay > 0.) {
    float grainOverlay = valueNoise(rotate(grainUV, 1.) + float2(3., 3.));
    grainOverlay = lerp(grainOverlay, valueNoise(rotate(grainUV, 2.) + float2(-1., -1.)), .5);
    grainOverlay = pow(abs(grainOverlay), 1.3);
    float grainOverlayV = grainOverlay * 2. - 1.;
    float g = step(0., grainOverlayV);
    float strength = pow(abs(u_grainOverlay * abs(grainOverlayV)), .8);
    color = lerp(color, float3(g, g, g), .5 * strength);
    opacity += .5 * strength;
  }
  return float4(color, saturate(opacity));
}

// ---------------------------------------------------------------- GemSmoke (gem-smoke.ts), image branch
float4 PSGem(V v) : SV_Target {
  float u_innerDistortion = p0.x, u_outerDistortion = p0.y, u_outerGlow = p0.z, u_innerGlow = p0.w;
  float u_offset = p1.x, u_angle = p1.y, u_size = p1.z, u_colorsCount = p1.w;
  float time = u_time;
  float2 imageUV = (v.imageUV - .5) * .95 + .5;
  // The library samples its 4096-wide processed image with trilinear mipmaps; our copy is smaller, the
  // footprint on screen is the same (the level of detail is taken from the same derivatives).
  float lod = u_image.CalculateLevelOfDetail(u_linear, imageUV);
  float2 texel = p2.xy;
  static const float k[9] = {1.0, 8.0, 28.0, 56.0, 70.0, 56.0, 28.0, 8.0, 1.0};
  float2 sum = float2(0., 0.);
  for (int j = -4; j <= 4; ++j) for (int i = -4; i <= 4; ++i)
    sum += k[i + 4] * k[j + 4] * u_image.SampleLevel(u_linear, imageUV + float2(i * 10. * texel.x, j * 10. * texel.y), lod).rg;
  float roundness = 1. - sum.x / 65536.0;
  static const float k3[3] = {1.0, 2.0, 1.0};
  float imgAlpha = 0.;
  for (int b = -1; b <= 1; ++b) for (int a = -1; a <= 1; ++a)
    imgAlpha += k3[a + 1] * k3[b + 1] * u_image.SampleLevel(u_linear, imageUV + float2(a * texel.x, b * texel.y), lod).g;
  imgAlpha /= 16.0;

  float2 smokeUV = rotate(v.objectUV, u_angle * PI / 180.) * lerp(4., 1., u_size);
  float2 innerUV = smokeUV, outerUV = smokeUV;
  innerUV.y += u_innerDistortion * (1. - sst(0., 1., length(.4 * innerUV)));
  innerUV.y -= .4 * u_innerDistortion;
  innerUV.y += .7 * u_offset * roundness;
  outerUV.y += u_outerDistortion * (1. - sst(0., 1., length(.4 * outerUV)));
  outerUV.y -= .4 * u_outerDistortion;
  float innerSwirl = u_innerDistortion * roundness;
  float outerSwirl = u_outerDistortion;
  for (int n = 1; n < 5; n++) {
    float fi = float(n);
    float stretchIn = max(length(ddx(innerUV)), length(ddy(innerUV)));
    float sIn = innerSwirl / (1. + stretchIn * 8.);
    innerUV.x += sIn / fi * cos(time + fi * 2.9 * innerUV.y);
    innerUV.y += sIn / fi * cos(time + fi * 1.5 * innerUV.x);
    float stretchOut = max(length(ddx(outerUV)), length(ddy(outerUV)));
    float sOut = outerSwirl / (1. + stretchOut * 8.);
    outerUV.x += sOut / fi * cos(time + fi * 2.9 * outerUV.y);
    outerUV.y += sOut / fi * cos(time + fi * 1.5 * outerUV.x);
  }
  float innerShape = exp(-1.5 * dot(innerUV, innerUV)) * (.01 + .99 * u_innerGlow) * imgAlpha;
  float outerShape = exp(-1.5 * dot(outerUV, outerUV)) * pow(abs(u_outerGlow), 2.) * (1. - imgAlpha);
  float mixer = (innerShape + outerShape) * u_colorsCount;
  float4 gradient = u_colors[0];
  gradient.rgb *= gradient.a;
  float smokeMask = 0.;
  for (int c = 1; c < 7; c++) {
    if (c > int(u_colorsCount)) break;
    float m = sst(0., 1., saturate(mixer - float(c - 1)));
    if (c == 1) smokeMask = m;
    float4 col = u_colors[c - 1];
    col.rgb *= col.a;
    gradient = lerp(gradient, col, m);
  }
  float3 color = gradient.rgb * smokeMask;
  float opacity = gradient.a * smokeMask;
  float innerOpacity = u_colorInner.a * imgAlpha;
  color += u_colorInner.rgb * innerOpacity * (1.0 - opacity);
  opacity += innerOpacity * (1.0 - opacity);
  color += u_colorBack.rgb * u_colorBack.a * (1.0 - opacity);
  opacity += u_colorBack.a * (1.0 - opacity);
  // CSS filter: contrast(108%) on the node (straight colour, then premultiplied again).
  if (p2.z > 0. && opacity > 0.) { float3 s = saturate((color / opacity - .5) * p2.z + .5); color = s * opacity; }
  return float4(color, opacity);
}

// ---------------------------------------------------------------- GrainGradient (grain-gradient.ts), wave
float3 permute(float3 x) { return glmod3(((x * 34.0) + 1.0) * x, 289.0); }
float snoise(float2 v) {
  const float4 C = float4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
  float2 i = floor(v + dot(v, C.yy));
  float2 x0 = v - i + dot(i, C.xx);
  float2 i1 = (x0.x > x0.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
  float4 x12 = x0.xyxy + C.xxzz;
  x12.xy -= i1;
  i = glmod2(i, 289.0);
  float3 p = permute(permute(i.y + float3(0.0, i1.y, 1.0)) + i.x + float3(0.0, i1.x, 1.0));
  float3 m = max(0.5 - float3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
  m = m * m; m = m * m;
  float3 x = 2.0 * frac(p * C.www) - 1.0;
  float3 h = abs(x) - 0.5;
  float3 ox = floor(x + 0.5);
  float3 a0 = x - ox;
  m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
  float3 g;
  g.x = a0.x * x0.x + h.x * x0.y;
  g.yz = a0.yz * x12.xz + h.yz * x12.yw;
  return 130.0 * dot(m, g);
}
float randomR(float2 p) { float2 uv = floor(p) / 100. + .5; return u_image.SampleLevel(u_linear, frac(uv), 0).r; }
float valueNoiseR(float2 st) {
  float2 i = floor(st); float2 f = frac(st);
  float a = randomR(i), b = randomR(i + float2(1.0, 0.0)), c = randomR(i + float2(0.0, 1.0)), d = randomR(i + float2(1.0, 1.0));
  float2 u = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float4 fbmR(float2 n0, float2 n1, float2 n2, float2 n3) {
  float amplitude = 0.2;
  float4 total = float4(0., 0., 0., 0.);
  for (int i = 0; i < 3; i++) {
    n0 = rotate(n0, 0.3); n1 = rotate(n1, 0.3); n2 = rotate(n2, 0.3); n3 = rotate(n3, 0.3);
    total.x += valueNoiseR(n0) * amplitude;
    total.y += valueNoiseR(n1) * amplitude;
    total.z += valueNoiseR(n2) * amplitude;
    total.z += valueNoiseR(n3) * amplitude;
    n0 *= 1.99; n1 *= 1.99; n2 *= 1.99; n3 *= 1.99;
    amplitude *= 0.6;
  }
  return total;
}
float4 PSGrain(V v) : SV_Target {
  float u_softness = p0.x, u_intensity = p0.y, u_noise = p0.z, u_colorsCount = p1.x;
  float t = .1 * (u_time + 7.);
  float2 graphicOffset = float2(-u_offsetX, u_offsetY);
  float2 shape_uv = .5 * v.patternUV;
  float2 grain_uv = 100. * v.patternUV;
  grain_uv = rotate(grain_uv, -u_rotation * PI / 180.);
  grain_uv *= u_scale;
  if (u_fit > 0.) {
    float2 givenBoxSize = max(float2(u_worldWidth, u_worldHeight), float2(1., 1.)) * u_pixelRatio;
    float2 patternBoxGivenSize = float2(u_worldWidth == 0. ? u_resolution.x : givenBoxSize.x, u_worldHeight == 0. ? u_resolution.y : givenBoxSize.y);
    float patternBoxRatio = patternBoxGivenSize.x / patternBoxGivenSize.y;
    float patternBoxNoFitBoxWidth = patternBoxRatio * min(patternBoxGivenSize.x / patternBoxRatio, patternBoxGivenSize.y);
    grain_uv /= (patternBoxNoFitBoxWidth / v.patternBoxSize.x);
  }
  float2 patternBoxScale = u_resolution / v.patternBoxSize;
  grain_uv -= graphicOffset / patternBoxScale;
  grain_uv *= 1.6;
  float wave = cos(.5 * shape_uv.x - 4. * t) * sin(1.5 * shape_uv.x + 2. * t) * (.75 + .25 * cos(6. * t));
  float shape = 1. - sst(-1., 1., shape_uv.y + wave);
  float baseNoise = snoise(grain_uv * .5);
  float4 fbmVals = fbmR(.002 * grain_uv + 10., .003 * grain_uv, .001 * grain_uv, rotate(.4 * grain_uv, 2.));
  float grainDist = baseNoise * snoise(grain_uv * .2) - fbmVals.x - fbmVals.y;
  float rawNoise = .75 * baseNoise - fbmVals.w - fbmVals.z;
  float noise = saturate(rawNoise);
  shape += u_intensity * 2. / u_colorsCount * (grainDist + .5);
  shape += u_noise * 10. / u_colorsCount * noise;
  float aa = fwidth(shape);
  shape = saturate(shape - .5 / u_colorsCount);
  float totalShape = sst(0., u_softness + 2. * aa, saturate(shape * u_colorsCount));
  float mixer = shape * (u_colorsCount - 1.);
  int cntStop = int(u_colorsCount) - 1;
  float4 gradient = u_colors[0];
  gradient.rgb *= gradient.a;
  for (int i = 1; i < 6; i++) {
    if (i > cntStop) break;
    float localT = saturate(mixer - float(i - 1));
    localT = sst(.5 - .5 * u_softness - aa, .5 + .5 * u_softness + aa, localT);
    float4 c = u_colors[i];
    c.rgb *= c.a;
    gradient = lerp(gradient, c, localT);
  }
  float3 color = gradient.rgb * totalShape;
  float opacity = gradient.a * totalShape;
  color += u_colorBack.rgb * u_colorBack.a * (1.0 - opacity);
  opacity += u_colorBack.a * (1.0 - opacity);
  return float4(color, opacity);
}

// ---------------------------------------------------------------- canvas pixels -> device pixels
float4 PSDown(V v) : SV_Target {
  float2 uv = float2(v.pos.x, v.pos.y) * p0.xy;   // 1 / output size
  if (p0.z < .5) return u_image.SampleLevel(u_linear, uv, 0);
  float2 d = p0.xy * .25;
  return .25 * (u_image.SampleLevel(u_linear, uv + float2(-d.x, -d.y), 0) + u_image.SampleLevel(u_linear, uv + float2(d.x, -d.y), 0) +
                u_image.SampleLevel(u_linear, uv + float2(-d.x, d.y), 0) + u_image.SampleLevel(u_linear, uv + float2(d.x, d.y), 0));
}
)HLSL";
}
template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
} // namespace Detail

// An RGBA image for a shader input: straight (not premultiplied) 8-bit RGBA rows, top row first.
struct Image { unsigned width = 0, height = 0; std::vector<std::uint8_t> rgba; bool mipmaps = false; };
// What Render() hands back: premultiplied BGRA rows (Direct2D's PBGRA), top row first.
struct Frame { unsigned width = 0, height = 0; std::vector<std::uint8_t> bgra; };

class Renderer {
    ID3D11Device* device_ = nullptr; ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr; ID3D11PixelShader* ps_[3] = {}; ID3D11PixelShader* down_ = nullptr;
    ID3D11Buffer* constants_ = nullptr; ID3D11SamplerState* sampler_ = nullptr; ID3D11RasterizerState* raster_ = nullptr;
    ID3D11ShaderResourceView* inputs_[3] = {};
    struct Target { ID3D11Texture2D* texture = nullptr; ID3D11RenderTargetView* view = nullptr; ID3D11ShaderResourceView* read = nullptr; unsigned w = 0, h = 0; };
    // One set of targets per node size: its last frame stays there, so a partial redraw has something to keep.
    struct Surface { Kind kind = Kind::Halftone; Target canvas, out; ID3D11Texture2D* staging = nullptr; unsigned stagingW = 0, stagingH = 0; bool drawn = false; };
    std::vector<Surface> surfaces_; ID3D11RasterizerState* scissor_ = nullptr;
    bool ready_ = false, hardware_ = false; std::string error_;
    void Free(Surface& s) { for (Target* t : {&s.canvas, &s.out}) { Detail::Release(t->read); Detail::Release(t->view); Detail::Release(t->texture); t->w = t->h = 0; } Detail::Release(s.staging); s.stagingW = s.stagingH = 0; s.drawn = false; }
    Surface* SurfaceFor(Kind kind, unsigned cw, unsigned ch, unsigned ow, unsigned oh) {
        for (auto& s : surfaces_) if (s.kind == kind && s.canvas.w == cw && s.canvas.h == ch && s.out.w == ow && s.out.h == oh) return &s;
        if (surfaces_.size() >= 6) { Free(surfaces_.front()); surfaces_.erase(surfaces_.begin()); }   // a DPI change leaves old sizes behind
        Surface s; s.kind = kind;
        if (!MakeTarget(s.canvas, cw, ch) || !MakeTarget(s.out, ow, oh)) { Free(s); return nullptr; }
        surfaces_.push_back(s); return &surfaces_.back();
    }

    bool MakeTarget(Target& t, unsigned w, unsigned h) {
        if (t.texture && t.w == w && t.h == h) return true;
        Detail::Release(t.read); Detail::Release(t.view); Detail::Release(t.texture);
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &t.texture)) || FAILED(device_->CreateRenderTargetView(t.texture, nullptr, &t.view)) ||
            FAILED(device_->CreateShaderResourceView(t.texture, nullptr, &t.read))) return false;
        t.w = w; t.h = h; return true;
    }
    bool Compile(Detail::CompileFn compile, const char* entry, const char* target, ID3DBlob** blob) {
        const char* src = Detail::Source(); ID3DBlob* errors = nullptr;
        const HRESULT hr = compile(src, std::strlen(src), "onboarding-shaders", nullptr, nullptr, entry, target, (1 << 15) /* OPTIMIZATION_LEVEL3 */, 0, blob, &errors);
        if (FAILED(hr)) { error_ = std::string(entry) + ": " + (errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string("compile failed")); }
        if (errors) errors->Release();
        return SUCCEEDED(hr);
    }
    // `part`: only these pixels of the target are drawn again (the rest keeps the previous frame).
    void Draw(ID3D11PixelShader* ps, ID3D11ShaderResourceView* input, const Target& to, const Uniforms& u, const D3D11_RECT* part = nullptr) {
        context_->UpdateSubresource(constants_, 0, nullptr, &u, 0, 0);
        ID3D11ShaderResourceView* none = nullptr; context_->PSSetShaderResources(0, 1, &none);
        context_->OMSetRenderTargets(1, &to.view, nullptr);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(to.w), static_cast<float>(to.h), 0, 1}; context_->RSSetViewports(1, &vp);
        if (part) { context_->RSSetScissorRects(1, part); context_->RSSetState(scissor_); } else context_->RSSetState(raster_);
        context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_, nullptr, 0); context_->VSSetConstantBuffers(0, 1, &constants_);
        context_->PSSetShader(ps, nullptr, 0); context_->PSSetConstantBuffers(0, 1, &constants_);
        context_->PSSetSamplers(0, 1, &sampler_); context_->PSSetShaderResources(0, 1, &input);
        const float clear[4] = {0, 0, 0, 0}; if (!part) context_->ClearRenderTargetView(to.view, clear);
        context_->Draw(3, 0);
        ID3D11RenderTargetView* noTarget = nullptr; context_->OMSetRenderTargets(1, &noTarget, nullptr);
    }
public:
    Renderer() = default; Renderer(const Renderer&) = delete; Renderer& operator=(const Renderer&) = delete;
    ~Renderer() { Dispose(); }
    bool Ready() const { return ready_; }
    // False on the software rasteriser: the caller draws one still frame.
    bool Animated() const { return ready_ && hardware_; }
    const std::string& Error() const { return error_; }
    void Dispose() {
        using Detail::Release;
        for (auto& s : surfaces_) Free(s);
        surfaces_.clear(); Release(scissor_);
        for (auto& i : inputs_) Release(i);
        for (auto& p : ps_) Release(p);
        Release(down_); Release(vs_); Release(constants_); Release(sampler_); Release(raster_); Release(context_); Release(device_);
        ready_ = false;
    }
    // `forceSoftware`: tests of the fallback path.
    bool Init(bool forceSoftware = false) {
        if (ready_) return true;
        Dispose(); error_.clear();
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};   // ps_4_1: CalculateLevelOfDetail
        HRESULT hr = E_FAIL; hardware_ = false;
        // d3d11.dll by name (system folder): no import, so the link lines of the executables do not change.
        using CreateFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
        static HMODULE d3d = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto create = d3d ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(d3d, "D3D11CreateDevice"))) : nullptr;
        if (!create) { error_ = "d3d11.dll unavailable"; return false; }
        if (!forceSoftware) { hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &device_, nullptr, &context_); hardware_ = SUCCEEDED(hr); }
        if (FAILED(hr)) hr = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &device_, nullptr, &context_);
        if (FAILED(hr) || !device_ || !context_) { error_ = "no Direct3D 11 device"; Dispose(); return false; }
        HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto compile = compiler ? reinterpret_cast<Detail::CompileFn>(reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile"))) : nullptr;
        if (!compile) { error_ = "d3dcompiler_47.dll unavailable"; if (compiler) FreeLibrary(compiler); Dispose(); return false; }
        bool ok = true; ID3DBlob* blob = nullptr;
        ok = Compile(compile, "VS", "vs_4_0", &blob) && SUCCEEDED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs_));
        Detail::Release(blob);
        const char* entries[3] = {"PSHalftone", "PSGem", "PSGrain"};
        for (int i = 0; ok && i < 3; ++i) { ok = Compile(compile, entries[i], "ps_4_1", &blob) && SUCCEEDED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps_[i])); Detail::Release(blob); }
        if (ok) { ok = Compile(compile, "PSDown", "ps_4_0", &blob) && SUCCEEDED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &down_)); Detail::Release(blob); }
        FreeLibrary(compiler);
        if (ok) { D3D11_BUFFER_DESC b{}; b.ByteWidth = sizeof(Uniforms); b.Usage = D3D11_USAGE_DEFAULT; b.BindFlags = D3D11_BIND_CONSTANT_BUFFER; ok = SUCCEEDED(device_->CreateBuffer(&b, nullptr, &constants_)); }
        if (ok) {
            D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = D3D11_FLOAT32_MAX; s.ComparisonFunc = D3D11_COMPARISON_NEVER;
            ok = SUCCEEDED(device_->CreateSamplerState(&s, &sampler_));
        }
        if (ok) { D3D11_RASTERIZER_DESC r{}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = TRUE; ok = SUCCEEDED(device_->CreateRasterizerState(&r, &raster_)); r.ScissorEnable = TRUE; ok = ok && SUCCEEDED(device_->CreateRasterizerState(&r, &scissor_)); }
        if (!ok) { if (error_.empty()) error_ = "Direct3D resources"; const std::string keep = error_; Dispose(); error_ = keep; return false; }
        ready_ = true; return true;
    }
    // The shader's input image (HalftoneDots: the logo; GemSmoke: the processed shape; GrainGradient: the noise).
    bool SetInput(Kind kind, const Image& image) {
        if (!ready_ || !image.width || !image.height || image.rgba.size() < static_cast<std::size_t>(image.width) * image.height * 4) return false;
        auto& view = inputs_[static_cast<int>(kind)]; Detail::Release(view);
        D3D11_TEXTURE2D_DESC d{}; d.Width = image.width; d.Height = image.height; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.MipLevels = image.mipmaps ? 0 : 1; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (image.mipmaps ? D3D11_BIND_RENDER_TARGET : 0); d.MiscFlags = image.mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &texture)) || !texture) return false;
        context_->UpdateSubresource(texture, 0, nullptr, image.rgba.data(), image.width * 4, 0);
        const bool ok = SUCCEEDED(device_->CreateShaderResourceView(texture, nullptr, &view));
        if (ok && image.mipmaps) context_->GenerateMips(view);
        texture->Release(); return ok;
    }
    bool HasInput(Kind kind) const { return inputs_[static_cast<int>(kind)] != nullptr; }
    // Renders `kind` for a node of `cssWidth` x `cssHeight` CSS pixels (DIP) shown on `outWidth` x `outHeight`
    // device pixels. Canvas resolution as the library: max(devicePixelRatio, 2) per CSS pixel.
    // `region` (device pixels, in/out): when the node was already rendered at this size into `frame`, only that
    // part is drawn and read back; otherwise the whole node is, and `region` comes back as the whole frame.
    bool Render(Kind kind, Uniforms u, float cssWidth, float cssHeight, unsigned outWidth, unsigned outHeight, Frame& frame, RECT* region = nullptr) {
        if (!ready_ || !outWidth || !outHeight || cssWidth <= 0 || cssHeight <= 0 || !HasInput(kind)) return false;
        const float dpr = outWidth / cssWidth, ratio = std::max(dpr, 2.f);
        const unsigned cw = std::min<unsigned>(4096, static_cast<unsigned>(std::lround(std::round(cssWidth) * ratio))), ch = std::min<unsigned>(4096, static_cast<unsigned>(std::lround(std::round(cssHeight) * ratio)));
        Surface* s = SurfaceFor(kind, cw, ch, outWidth, outHeight); if (!s) return false;
        RECT part{0, 0, static_cast<LONG>(outWidth), static_cast<LONG>(outHeight)};
        const bool partial = region && s->drawn && frame.width == outWidth && frame.height == outHeight && frame.bgra.size() == static_cast<std::size_t>(outWidth) * outHeight * 4;
        if (partial) { part.left = std::clamp<LONG>(region->left, 0, outWidth); part.top = std::clamp<LONG>(region->top, 0, outHeight); part.right = std::clamp<LONG>(region->right, part.left, outWidth); part.bottom = std::clamp<LONG>(region->bottom, part.top, outHeight); }
        if (region) *region = part;
        const unsigned pw = static_cast<unsigned>(part.right - part.left), ph = static_cast<unsigned>(part.bottom - part.top); if (!pw || !ph) return true;
        u.resolution[0] = static_cast<float>(cw); u.resolution[1] = static_cast<float>(ch); u.pixelRatio = cw / std::round(cssWidth);
        const float kx = static_cast<float>(cw) / outWidth, ky = static_cast<float>(ch) / outHeight;
        const D3D11_RECT canvasPart{std::max<LONG>(0, static_cast<LONG>(std::floor(part.left * kx)) - 2), std::max<LONG>(0, static_cast<LONG>(std::floor(part.top * ky)) - 2), std::min<LONG>(cw, static_cast<LONG>(std::ceil(part.right * kx)) + 2), std::min<LONG>(ch, static_cast<LONG>(std::ceil(part.bottom * ky)) + 2)};
        const D3D11_RECT outPart{part.left, part.top, part.right, part.bottom};
        Draw(ps_[static_cast<int>(kind)], inputs_[static_cast<int>(kind)], s->canvas, u, partial ? &canvasPart : nullptr);
        Uniforms d; d.p0[0] = 1.f / outWidth; d.p0[1] = 1.f / outHeight; d.p0[2] = (cw == outWidth * 2 && ch == outHeight * 2) || (cw == outWidth && ch == outHeight) ? 0.f : 1.f;
        Draw(down_, s->canvas.read, s->out, d, partial ? &outPart : nullptr);
        s->drawn = true;
        if (!s->staging || s->stagingW != pw || s->stagingH != ph) {
            Detail::Release(s->staging);
            D3D11_TEXTURE2D_DESC t{}; t.Width = pw; t.Height = ph; t.MipLevels = 1; t.ArraySize = 1; t.Format = DXGI_FORMAT_B8G8R8A8_UNORM; t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_STAGING; t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&t, nullptr, &s->staging))) { s->staging = nullptr; return false; }
            s->stagingW = pw; s->stagingH = ph;
        }
        const D3D11_BOX box{static_cast<UINT>(part.left), static_cast<UINT>(part.top), 0, static_cast<UINT>(part.right), static_cast<UINT>(part.bottom), 1};
        context_->CopySubresourceRegion(s->staging, 0, 0, 0, 0, s->out.texture, 0, &box);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(context_->Map(s->staging, 0, D3D11_MAP_READ, 0, &map))) { if (device_->GetDeviceRemovedReason() != S_OK) { ready_ = false; error_ = "device removed"; } return false; }
        if (!partial) { frame.width = outWidth; frame.height = outHeight; frame.bgra.resize(static_cast<std::size_t>(outWidth) * outHeight * 4); }
        for (unsigned y = 0; y < ph; ++y) std::memcpy(frame.bgra.data() + (static_cast<std::size_t>(part.top + y) * outWidth + part.left) * 4, static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch, static_cast<std::size_t>(pw) * 4);
        context_->Unmap(s->staging, 0);
        return true;
    }
};

// Decodes a bundled image (PNG) into straight RGBA for SetInput.
inline bool LoadPng(IWICImagingFactory* wic, const std::wstring& path, Image& out, bool mipmaps = false) {
    if (!wic) return false;
    IWICBitmapDecoder* decoder = nullptr; IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* converter = nullptr; bool ok = false;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(wic->CreateFormatConverter(&converter)) && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0; converter->GetSize(&w, &h);
        if (w && h && w <= 8192 && h <= 8192) { out.width = w; out.height = h; out.mipmaps = mipmaps; out.rgba.resize(static_cast<std::size_t>(w) * h * 4); ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data())); }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    return ok;
}

} // namespace Shaders
} // namespace Onboarding
