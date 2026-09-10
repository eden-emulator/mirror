// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float InputBlack <
    ui_type = "slider";
    ui_label = "Input Black";
    ui_tooltip = "Input level mapped to black. Raise it to deepen washed out shadows.";
    ui_min = 0.0; ui_max = 0.5; ui_step = 0.005;
> = 0.0;

uniform float InputWhite <
    ui_type = "slider";
    ui_label = "Input White";
    ui_min = 0.5; ui_max = 1.0; ui_step = 0.005;
> = 1.0;

uniform float Gamma <
    ui_type = "slider";
    ui_label = "Gamma";
    ui_min = 0.2; ui_max = 3.0; ui_step = 0.01;
> = 1.0;

uniform float OutputBlack <
    ui_type = "slider";
    ui_label = "Output Black";
    ui_tooltip = "Lifts crushed shadows so detail stops collapsing into one flat black.";
    ui_min = 0.0; ui_max = 0.5; ui_step = 0.005;
> = 0.0;

uniform float OutputWhite <
    ui_type = "slider";
    ui_label = "Output White";
    ui_min = 0.5; ui_max = 1.0; ui_step = 0.005;
> = 1.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_Levels(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = tex2D(BackBuffer, uv).rgb;

    rgb = saturate((rgb - InputBlack) / max(InputWhite - InputBlack, 0.001));
    rgb = pow(max(rgb, 0.0), 1.0 / max(Gamma, 0.001));
    rgb = lerp(OutputBlack, OutputWhite, rgb);

    return float4(saturate(rgb), 1.0);
}

technique Levels <
    ui_label = "Levels";
    ui_tooltip = "Black point, white point, gamma and output range. Use it to fix crushed or washed out shadows.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Levels;
    }
}
