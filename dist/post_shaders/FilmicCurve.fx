// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Exposure <
    ui_type = "slider";
    ui_label = "Exposure";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
> = 1.0;

uniform float Toe <
    ui_type = "slider";
    ui_label = "Toe";
    ui_tooltip = "Above 1.0 deepens the shadows, below 1.0 lifts them.";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
> = 1.2;

uniform float Shoulder <
    ui_type = "slider";
    ui_label = "Shoulder";
    ui_tooltip = "Above 1.0 opens up the highlights, below 1.0 compresses them.";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
> = 1.2;

uniform float Amount <
    ui_type = "slider";
    ui_label = "Amount";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.7;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_FilmicCurve(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = tex2D(BackBuffer, uv).rgb;

    float3 curved = saturate(rgb * Exposure);
    curved = pow(max(curved, 0.0), Toe);
    curved = 1.0 - pow(max(1.0 - curved, 0.0), Shoulder);

    return float4(saturate(lerp(rgb, curved, Amount)), 1.0);
}

technique FilmicCurve <
    ui_label = "Filmic Curve";
    ui_tooltip = "Filmic contrast curve. Deepens the shadows and opens the highlights without clipping either end.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_FilmicCurve;
    }
}
