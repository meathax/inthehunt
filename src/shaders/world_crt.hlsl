/*
 * Flat native CRT presentation.
 *
 * Gaussian reconstruction, bloom and VGA mask adapted from Timothy Lottes'
 * PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER.
 * https://github.com/libretro/slang-shaders/blob/2b0530b/crt/shaders/crt-lottes.slang
 *
 * Cubic reconstruction and variable beam profile adapted from Hyllian's
 * CRT Shader. Copyright (C) 2011-2024 Hyllian.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Source is an ordinary UNORM texture containing encoded RGB. Output targets
 * ordinary UNORM, with sRGB conversion performed here for the CRT profiles.
 * The host supplies a whole-number viewport. Coordinates stay unwarped.
 */

Texture2D<float4> Source : register(t0);
SamplerState PointClamp : register(s0);

cbuffer CrtConstants : register(b0)
{
    float2 SourceSize;
    float2 ViewportSize;
    float2 ViewportOrigin;
    float HardScan;
    float HardPixel;
    float MaskDark;
    float MaskLight;
    float BloomAmount;
    float Brightness;
    float BeamMin;
    float BeamMax;
    float ScanlineStrength;
    float Profile;
};

float3 ToLinear(float3 c)
{
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.r <= 0.04045 ? low.r : high.r,
                  c.g <= 0.04045 ? low.g : high.g,
                  c.b <= 0.04045 ? low.b : high.b);
}

float3 ToSrgb(float3 c)
{
    c = max(c, 0.0);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return float3(c.r < 0.0031308 ? low.r : high.r,
                  c.g < 0.0031308 ? low.g : high.g,
                  c.b < 0.0031308 ? low.b : high.b);
}

float3 Fetch(float2 uv, float2 offset)
{
    float2 texel = clamp(floor(uv * SourceSize + offset),
                         0.0, SourceSize - 1.0);
    return ToLinear(Source.SampleLevel(PointClamp, (texel + 0.5) / SourceSize, 0).rgb);
}

float2 DistanceToTexel(float2 uv)
{
    return 0.5 - frac(uv * SourceSize);
}

float Gaussian(float distance, float hardness)
{
    return exp2(hardness * distance * distance);
}

float3 Horizontal3(float2 uv, float row)
{
    float distance = DistanceToTexel(uv).x;
    float wa = Gaussian(distance - 1.0, HardPixel);
    float wb = Gaussian(distance, HardPixel);
    float wc = Gaussian(distance + 1.0, HardPixel);
    return (Fetch(uv, float2(-1, row)) * wa +
            Fetch(uv, float2(0, row)) * wb +
            Fetch(uv, float2(1, row)) * wc) / (wa + wb + wc);
}

float3 Horizontal5(float2 uv, float row, float hardness)
{
    float distance = DistanceToTexel(uv).x;
    float3 color = 0.0;
    float weights = 0.0;
    [unroll] for (int x = -2; x <= 2; ++x)
    {
        float weight = Gaussian(distance + x, hardness);
        color += Fetch(uv, float2(x, row)) * weight;
        weights += weight;
    }
    return color / weights;
}

float3 Horizontal7(float2 uv, float row)
{
    float distance = DistanceToTexel(uv).x;
    float3 color = 0.0;
    float weights = 0.0;
    [unroll] for (int x = -3; x <= 3; ++x)
    {
        float weight = Gaussian(distance + x, -1.5);
        color += Fetch(uv, float2(x, row)) * weight;
        weights += weight;
    }
    return color / weights;
}

float3 Lottes(float2 uv)
{
    float y = DistanceToTexel(uv).y;
    float3 color = Horizontal3(uv, -1) * Gaussian(y - 1.0, HardScan) +
                   Horizontal5(uv, 0, HardPixel) * Gaussian(y, HardScan) +
                   Horizontal3(uv, 1) * Gaussian(y + 1.0, HardScan);
    if (BloomAmount > 0.0)
    {
        float3 bloom = Horizontal5(uv, -2, HardPixel) * Gaussian(y - 2.0, -2.0) +
                       Horizontal7(uv, -1) * Gaussian(y - 1.0, -2.0) +
                       Horizontal7(uv, 0) * Gaussian(y, -2.0) +
                       Horizontal7(uv, 1) * Gaussian(y + 1.0, -2.0) +
                       Horizontal5(uv, 2, HardPixel) * Gaussian(y + 2.0, -2.0);
        color += bloom * BloomAmount;
    }
    return color;
}

float3 CubicRow(float2 uv, float row, float fraction)
{
    float3 a = Fetch(uv, float2(-1, row));
    float3 b = Fetch(uv, float2(0, row));
    float3 c = Fetch(uv, float2(1, row));
    float3 d = Fetch(uv, float2(2, row));
    float t2 = fraction * fraction;
    float t3 = t2 * fraction;
    float4 weights = float4(-0.5 * t3 + t2 - 0.5 * fraction,
                            1.5 * t3 - 2.5 * t2 + 1.0,
                            -1.5 * t3 + 2.0 * t2 + 0.5 * fraction,
                            0.5 * t3 - 0.5 * t2);
    float3 result = a * weights.x + b * weights.y + c * weights.z + d * weights.w;
    return clamp(result, min(b, c), max(b, c));
}

float3 HyllianBeam(float2 uv)
{
    float2 pixel = uv * SourceSize - 0.5;
    float2 base = (floor(pixel) + 0.5) / SourceSize;
    float2 fraction = frac(pixel);
    float3 a = CubicRow(base, 0, fraction.x);
    float3 b = CubicRow(base, 1, fraction.x);
    float3 widthA = lerp(BeamMin.xxx, BeamMax.xxx, a);
    float3 widthB = lerp(BeamMin.xxx, BeamMax.xxx, b);
    float3 distanceA = ScanlineStrength * fraction.y / (widthA * widthA + 0.0000001);
    float3 distanceB = ScanlineStrength * (1.0 - fraction.y) / (widthB * widthB + 0.0000001);
    return a * exp(-16.0 * distanceA * distanceA) +
           b * exp(-16.0 * distanceB * distanceB);
}

float3 PhosphorMask(float2 position)
{
    float3 mask = MaskDark.xxx;
    float phase;
    if (Profile >= 2.5)
        phase = frac(position.x / 3.0);
    else
        phase = frac((position.x + floor(position.y) * 3.0) / 6.0);
    if (phase < 1.0 / 3.0) mask.r = MaskLight;
    else if (phase < 2.0 / 3.0) mask.g = MaskLight;
    else mask.b = MaskLight;
    return mask;
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    if (Profile < 0.5 || ViewportSize.y < SourceSize.y * 2.0)
        return float4(Source.SampleLevel(PointClamp, uv, 0).rgb, 1.0);
    float3 color = Profile >= 2.5 ? HyllianBeam(uv) : Lottes(uv);
    color *= PhosphorMask(position.xy - ViewportOrigin);
    return float4(saturate(ToSrgb(color * Brightness)), 1.0);
}
