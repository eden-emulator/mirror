// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float ShadowHue <
    ui_type = "slider";
    ui_label = "Shadow Hue";
    ui_min = 0.0; ui_max = 360.0; ui_step = 1.0;
> = 210.0;

uniform float ShadowStrength <
    ui_type = "slider";
    ui_label = "Shadow Strength";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.0;

uniform float HighlightHue <
    ui_type = "slider";
    ui_label = "Highlight Hue";
    ui_min = 0.0; ui_max = 360.0; ui_step = 1.0;
> = 45.0;

uniform float HighlightStrength <
    ui_type = "slider";
    ui_label = "Highlight Strength";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.0;

uniform float Balance <
    ui_type = "slider";
    ui_label = "Balance";
    ui_tooltip = "Moves the split between what counts as shadow and what counts as highlight.";
    ui_min = -0.5; ui_max = 0.5; ui_step = 0.01;
> = 0.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 HueToRGB(float hue)
{
    float h = frac(hue / 360.0) * 6.0;
    return saturate(float3(abs(h - 3.0) - 1.0, 2.0 - abs(h - 2.0), 2.0 - abs(h - 4.0)));
}

float4 PS_SplitToning(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = tex2D(BackBuffer, uv).rgb;

    float luma = saturate(dot(rgb, float3(0.2126, 0.7152, 0.0722)) + Balance);

    float3 shadow_tint = HueToRGB(ShadowHue) - 0.5;
    float3 highlight_tint = HueToRGB(HighlightHue) - 0.5;

    rgb += shadow_tint * ShadowStrength * 0.25 * (1.0 - luma);
    rgb += highlight_tint * HighlightStrength * 0.25 * luma;

    return float4(saturate(rgb), 1.0);
}

technique SplitToning <
    ui_label = "Split Toning";
    ui_tooltip = "Tints the shadows and the highlights towards two different hues.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_SplitToning;
    }
}
