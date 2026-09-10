// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-FileCopyrightText: guest(r)
// SPDX-License-Identifier: GPL-2.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float EdgeStrength <
    ui_type = "slider";
    ui_label = "Edge Strength";
    ui_tooltip = "Darkness of the ink outline drawn around detected edges.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 0.45;

uniform float ShadowGuard <
    ui_type = "slider";
    ui_label = "Shadow Guard";
    ui_tooltip = "Holds the outline back in dark areas. Raise it if shadows turn into black blobs.";
    ui_min = 0.1; ui_max = 2.0; ui_step = 0.05;
> = 0.8;

uniform float Levels <
    ui_type = "slider";
    ui_label = "Colour Levels";
    ui_tooltip = "How many bands the colours are quantised into.";
    ui_min = 2.0; ui_max = 16.0; ui_step = 1.0;
> = 6.0;

uniform float Smoothing <
    ui_type = "slider";
    ui_label = "Banding";
    ui_tooltip = "How far the picture is pushed towards flat bands.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.75;

uniform float Saturation <
    ui_type = "slider";
    ui_label = "Saturation";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 1.15;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_CartoonSoft(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 c00 = tex2D(BackBuffer, uv + texel * float2(-1.0, -1.0)).rgb;
    float3 c10 = tex2D(BackBuffer, uv + texel * float2( 0.0, -1.0)).rgb;
    float3 c20 = tex2D(BackBuffer, uv + texel * float2( 1.0, -1.0)).rgb;
    float3 c01 = tex2D(BackBuffer, uv + texel * float2(-1.0,  0.0)).rgb;
    float3 c11 = tex2D(BackBuffer, uv).rgb;
    float3 c21 = tex2D(BackBuffer, uv + texel * float2( 1.0,  0.0)).rgb;
    float3 c02 = tex2D(BackBuffer, uv + texel * float2(-1.0,  1.0)).rgb;
    float3 c12 = tex2D(BackBuffer, uv + texel * float2( 0.0,  1.0)).rgb;
    float3 c22 = tex2D(BackBuffer, uv + texel * float2( 1.0,  1.0)).rgb;

    const float3 dt = float3(1.0, 1.0, 1.0);
    const float3 luma_weights = float3(0.299, 0.587, 0.114);

    float d1 = dot(abs(c00 - c22), dt);
    float d2 = dot(abs(c20 - c02), dt);
    float hl = dot(abs(c01 - c21), dt);
    float vl = dot(abs(c10 - c12), dt);

    float luma = dot(c11, luma_weights);
    float response = (d1 + d2 + hl + vl) / (luma * 2.0 + ShadowGuard);
    float ink = 1.0 - saturate(response * EdgeStrength);

    float scaled = luma * Levels;
    float step_position = frac(scaled);
    float eased = step_position * step_position * (3.0 - 2.0 * step_position);
    float banded = (floor(scaled) + eased) / Levels;
    float target = lerp(luma, banded, Smoothing);

    float3 tinted = c11 * (target / max(luma, 0.001));
    float3 grey = dot(tinted, luma_weights);
    float3 color = lerp(grey, tinted, Saturation) * ink;

    return float4(saturate(color), 1.0);
}

technique CartoonSoft <
    ui_label = "Cartoon Soft";
    ui_tooltip = "Ink outlines and flat colour bands, with the outline held back in the shadows so dark scenes do not turn into black blobs.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_CartoonSoft;
    }
}
