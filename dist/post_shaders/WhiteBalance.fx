// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Temperature <
    ui_type = "slider";
    ui_label = "Temperature";
    ui_tooltip = "Negative cools the picture towards blue, positive warms it towards orange.";
    ui_min = -100.0; ui_max = 100.0; ui_step = 1.0;
> = 0.0;

uniform float Tint <
    ui_type = "slider";
    ui_label = "Tint";
    ui_tooltip = "Negative shifts towards green, positive towards magenta.";
    ui_min = -100.0; ui_max = 100.0; ui_step = 1.0;
> = 0.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 WhitePointLMS(float t1, float t2)
{
    float shift = 0.05;
    if (t1 < 0.0)
    {
        shift = 0.10;
    }
    float x = 0.31271 - t1 * shift;
    float y = 2.87 * x - 3.0 * x * x - 0.27509507 + t2 * 0.05;

    float big_y = 1.0;
    float big_x = big_y * x / y;
    float big_z = big_y * (1.0 - x - y) / y;

    return float3( 0.7328 * big_x + 0.4296 * big_y - 0.1624 * big_z,
                  -0.7036 * big_x + 1.6975 * big_y + 0.0061 * big_z,
                   0.0030 * big_x + 0.0136 * big_y + 0.9834 * big_z);
}

float4 PS_WhiteBalance(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = pow(max(tex2D(BackBuffer, uv).rgb, 0.0), 2.2);

    float3 balance = float3(0.949237, 1.03542, 1.08728) /
                     WhitePointLMS(Temperature / 65.0, Tint / 65.0);

    const float3x3 rgb_to_lms = float3x3(0.390405, 0.549941, 0.008926,
                                         0.070841, 0.963172, 0.001358,
                                         0.023108, 0.128021, 0.936245);
    const float3x3 lms_to_rgb = float3x3( 2.858470, -1.628790, -0.024891,
                                         -0.210182,  1.158200,  0.000324,
                                         -0.041812, -0.118169,  1.068670);

    float3 lms = mul(rgb_to_lms, rgb) * balance;
    rgb = mul(lms_to_rgb, lms);

    return float4(saturate(pow(max(rgb, 0.0), 1.0 / 2.2)), 1.0);
}

technique WhiteBalance <
    ui_label = "White Balance";
    ui_tooltip = "Corrects a picture that looks too cool, too warm or tinted.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_WhiteBalance;
    }
}
