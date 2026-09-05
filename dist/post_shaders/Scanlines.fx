// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from scanlines.fsh in PPSSPP.


texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Density <
    ui_type = "slider";
    ui_label = "Line Density";
    ui_tooltip = "Number of scanline pairs across the screen.";
    ui_min = 60.0; ui_max = 720.0; ui_step = 10.0;
> = 340.0;

uniform float Intensity <
    ui_type = "slider";
    ui_label = "Intensity";
    ui_tooltip = "How dark the gaps between lines become.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.5;

uniform float Tint <
    ui_type = "slider";
    ui_label = "Phosphor Tint";
    ui_tooltip = "Strength of the green-warm phosphor cast.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 1.0;

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

float4 PS_Scanlines(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float line_pos = uv.y * Density * 0.5;
    float gate = cos((frac(line_pos) - 0.5) * 3.1415926 * Intensity) * 1.5;

    float3 rgb = tex2D(BackBuffer, uv).rgb;
    float3 color = rgb * 0.5 + 0.5 * rgb * rgb * 1.2;

    float3 phosphor = lerp(float3(1.0, 1.0, 1.0), float3(0.9, 1.0, 0.7), Tint);
    color *= phosphor;

    float2 diff = uv - 0.5;
    color *= 1.1 - 0.6 * (dot(diff, diff) * 2.0);

    return float4(saturate(color * saturate(gate)), 1.0);
}

technique Scanlines
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Scanlines;
    }
}
