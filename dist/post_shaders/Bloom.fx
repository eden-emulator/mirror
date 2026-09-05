// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from bloomnoblur.fsh in PPSSPP.

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Radius <
    ui_type = "slider";
    ui_label = "Radius";
    ui_tooltip = "How far the glow spreads from bright areas.";
    ui_min = 0.0; ui_max = 4.0; ui_step = 0.05;
> = 1.0;

uniform float Amount <
    ui_type = "slider";
    ui_label = "Amount";
    ui_tooltip = "Strength of the glow added on top of the image.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 0.6;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(0.0, 0.0);
    if (id == 2)
    {
        uv.x = 2.0;
    }
    if (id == 1)
    {
        uv.y = 2.0;
    }
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float Weight(float3 color)
{
    float gray = (color.r + color.g + color.b) / 3.0;
    float saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;
    return gray * gray / max(saturation, 0.25);
}

float4 PS_Bloom(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 color = tex2D(BackBuffer, uv).rgb;

    float gray = (color.r + color.g + color.b) / 3.0;
    float saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;
    float spread = 0.002 * gray / max(saturation, 0.25) * Radius;

    float3 sum = float3(0.0, 0.0, 0.0);
    for (int x = -3; x <= 3; x += 2)
    {
        for (int y = -3; y <= 3; y += 2)
        {
            float3 tap = tex2D(BackBuffer, uv + float2(x, y) * spread).rgb;
            sum += tap * Weight(tap);
        }
    }
    sum /= 16.0;

    return float4(saturate(color + sum * Amount), 1.0);
}

technique Bloom
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Bloom;
    }
}
