// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from fakereflections.fsh in PPSSPP.

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Amount <
    ui_type = "slider";
    ui_label = "Amount";
    ui_tooltip = "Strength of the reflection added to the lower half of the screen.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.6;

uniform float Power <
    ui_type = "slider";
    ui_label = "Power";
    ui_tooltip = "How sharply the reflection falls off away from bright areas.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.5;

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

float Wrap(float value, float period)
{
    return period * frac(value / period);
}

float4 PS_FakeReflections(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 color = tex2D(BackBuffer, uv).rgb;

    float gray = (color.r + color.g + color.b) / 3.0;
    float saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;

    float rndx = Wrap(uv.x + gray, 0.03) + Wrap(uv.y + saturation, 0.05);
    float rndy = Wrap(uv.y + saturation, 0.03) + Wrap(uv.x + gray, 0.05);

    float falloff = (max(gray, saturation) + 0.1) * uv.y;
    float2 offset = float2(rndx, rndy - min(uv.y, 0.25)) * falloff;

    float3 reflection = tex2D(BackBuffer, uv + offset).rgb;
    reflection *= 4.0 * (1.0 - gray) * Amount;
    reflection *= reflection * falloff * Power;

    return float4(saturate(color + reflection), 1.0);
}

technique FakeReflections
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_FakeReflections;
    }
}
