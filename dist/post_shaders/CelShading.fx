// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Bands <
    ui_type = "slider";
    ui_label = "Shading Bands";
    ui_tooltip = "How many flat steps the lighting is collapsed into. Three or four gives the classic look.";
    ui_min = 2.0; ui_max = 8.0; ui_step = 1.0;
> = 4.0;

uniform float BandContrast <
    ui_type = "slider";
    ui_label = "Band Contrast";
    ui_tooltip = "Spreads the bands towards pure black and white. Lower it if the shadows crush.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.5;

uniform float BandEdge <
    ui_type = "slider";
    ui_label = "Band Edge";
    ui_tooltip = "Width of the transition between bands. Low values give hard cel steps.";
    ui_min = 0.02; ui_max = 1.0; ui_step = 0.02;
> = 0.15;

uniform float Detail <
    ui_type = "slider";
    ui_label = "Texture Detail";
    ui_tooltip = "How much surface texture survives the flattening. At zero the bands are completely flat.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.6;

uniform float OutlineStrength <
    ui_type = "slider";
    ui_label = "Outline Strength";
    ui_tooltip = "Opacity of the ink line drawn along detected edges.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.8;

uniform float OutlineThreshold <
    ui_type = "slider";
    ui_label = "Outline Threshold";
    ui_tooltip = "How strong an edge has to be before it is inked. Raise it to keep ink off textures and specular highlights.";
    ui_min = 0.01; ui_max = 0.4; ui_step = 0.01;
> = 0.18;

uniform float Saturation <
    ui_type = "slider";
    ui_label = "Saturation";
    ui_tooltip = "Colour intensity of the flat regions.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 1.25;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_CelShading(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float3 luma_weights = float3(0.2126, 0.7152, 0.0722);
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 centre = tex2D(BackBuffer, uv).rgb;
    float luma = dot(centre, luma_weights);

    float2 near = texel * 1.5;
    float2 far = texel * 3.5;

    float a00 = dot(tex2D(BackBuffer, uv + near * float2(-1.0, -1.0)).rgb, luma_weights);
    float a20 = dot(tex2D(BackBuffer, uv + near * float2( 1.0, -1.0)).rgb, luma_weights);
    float a02 = dot(tex2D(BackBuffer, uv + near * float2(-1.0,  1.0)).rgb, luma_weights);
    float a22 = dot(tex2D(BackBuffer, uv + near * float2( 1.0,  1.0)).rgb, luma_weights);

    float b00 = dot(tex2D(BackBuffer, uv + far * float2(-1.0, -1.0)).rgb, luma_weights);
    float b20 = dot(tex2D(BackBuffer, uv + far * float2( 1.0, -1.0)).rgb, luma_weights);
    float b02 = dot(tex2D(BackBuffer, uv + far * float2(-1.0,  1.0)).rgb, luma_weights);
    float b22 = dot(tex2D(BackBuffer, uv + far * float2( 1.0,  1.0)).rgb, luma_weights);

    float gx = (a00 + a02) - (a20 + a22);
    float gy = (a00 + a20) - (a02 + a22);
    float gradient = sqrt(gx * gx + gy * gy);
    float ink = smoothstep(OutlineThreshold, OutlineThreshold + 0.04, gradient);

    float lighting = (a00 + a20 + a02 + a22 + b00 + b20 + b02 + b22) * 0.125;
    float detail = luma - lighting;

    float softness = max(BandEdge, 0.001);
    float coord = lighting * Bands - softness * 0.5;
    float index = floor(coord) + smoothstep(1.0 - softness, 1.0, frac(coord));

    float centred = (index + 0.5) / Bands;
    float stretched = index / max(Bands - 1.0, 1.0);
    float banded = saturate(lerp(centred, stretched, BandContrast));

    float target = saturate(banded + detail * Detail);

    float gain = min(target / max(luma, 0.001), 4.0);
    float3 shaded = centre * gain;
    float3 grey = dot(shaded, luma_weights);
    float3 color = lerp(grey, shaded, Saturation);
    color *= 1.0 - ink * OutlineStrength;

    return float4(saturate(color), 1.0);
}

technique CelShading <
    ui_label = "Cel Shading";
    ui_tooltip = "Collapses the lighting into a few hard steps and inks the edges, keeping texture detail and hue intact.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_CelShading;
    }
}
