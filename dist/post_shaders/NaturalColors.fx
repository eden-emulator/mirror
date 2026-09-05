// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from naturalA.fsh in PPSSPP, by ShadX, modified by SimoneT.

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Luma <
    ui_type = "slider";
    ui_label = "Luma Curve";
    ui_tooltip = "Gamma applied to the luminance channel in YIQ space.";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
> = 1.2;

uniform float Chroma <
    ui_type = "slider";
    ui_label = "Chroma Gain";
    ui_tooltip = "Boost applied to the two colour difference channels.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 1.2;

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

float4 PS_Natural(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float3x3 RGBtoYIQ = float3x3(0.299, 0.587, 0.114,
                                       0.596, -0.275, -0.321,
                                       0.212, -0.523, 0.311);

    const float3x3 YIQtoRGB = float3x3(1.0, 0.95568806, 0.61985809,
                                       1.0, -0.27158180, -0.64687382,
                                       1.0, -1.10817733, 1.70506456);

    float3 rgb = tex2D(BackBuffer, uv).rgb;
    float3 yiq = mul(RGBtoYIQ, rgb);

    yiq.x = pow(max(yiq.x, 0.0), Luma);
    yiq.yz *= Chroma;

    return float4(saturate(mul(YIQtoRGB, yiq)), 1.0);
}

technique NaturalColors
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Natural;
    }
}
