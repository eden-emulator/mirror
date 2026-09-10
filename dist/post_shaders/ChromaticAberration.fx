// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Strength <
    ui_type = "slider";
    ui_label = "Strength";
    ui_tooltip = "Channel separation in pixels, measured at the edge of the screen.";
    ui_min = 0.0; ui_max = 8.0; ui_step = 0.1;
> = 1.5;

uniform float Falloff <
    ui_type = "slider";
    ui_label = "Falloff";
    ui_tooltip = "How fast the separation grows away from the centre. Higher keeps the middle clean.";
    ui_min = 1.0; ui_max = 4.0; ui_step = 0.1;
> = 2.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_ChromaticAberration(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float2 direction = uv - float2(0.5, 0.5);
    float radius = length(direction);
    float2 unit = direction / max(radius, 0.0001);
    float2 offset = unit * Strength * pow(radius * 2.0, Falloff) * texel;

    float red = tex2D(BackBuffer, uv + offset).r;
    float green = tex2D(BackBuffer, uv).g;
    float blue = tex2D(BackBuffer, uv - offset).b;

    return float4(red, green, blue, 1.0);
}

technique ChromaticAberration <
    ui_label = "Chromatic Aberration";
    ui_tooltip = "Splits the colour channels apart towards the edges of the screen, the way a cheap lens fails to focus every colour on the same spot.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_ChromaticAberration;
    }
}
