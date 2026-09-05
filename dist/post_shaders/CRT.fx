// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from crt.fsh in PPSSPP, by KillaMaaki.


texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Timer < source = "timer"; > = 0.0;

uniform float Density <
    ui_type = "slider";
    ui_label = "Line Density";
    ui_min = 60.0; ui_max = 720.0; ui_step = 10.0;
> = 272.0;

uniform float RollSpeed <
    ui_type = "slider";
    ui_label = "Roll Speed";
    ui_tooltip = "Speed of the rolling bar. Zero disables it.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 1.0;

uniform float Bleed <
    ui_type = "slider";
    ui_label = "Colour Bleed";
    ui_tooltip = "Horizontal separation of the red and green channels.";
    ui_min = 0.0; ui_max = 4.0; ui_step = 0.1;
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

float4 PS_CRT(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float seconds = Timer * 0.001;
    float scan = floor((uv.y + seconds * RollSpeed * 0.5) * Density);
    float line_intensity = frac(scan * 0.5) * 2.0;

    float2 shift = float2(line_intensity * 0.0005, 0.0);
    float2 bleed = float2(BUFFER_RCP_WIDTH * Bleed, 0.0);

    float r = tex2D(BackBuffer, uv + bleed + shift).r;
    float g = tex2D(BackBuffer, uv - bleed + shift).g;
    float b = tex2D(BackBuffer, uv).b;

    float3 color = float3(r, g * 0.99, b) * clamp(line_intensity, 0.85, 1.0);

    if (RollSpeed > 0.0)
    {
        float rollbar = sin((uv.y + seconds * RollSpeed) * 4.0);
        color += rollbar * 0.02;
    }

    return float4(saturate(color), 1.0);
}

technique CRT
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_CRT;
    }
}
