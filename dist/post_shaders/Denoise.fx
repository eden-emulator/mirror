// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019-2021 bloc97
// SPDX-License-Identifier: MIT

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Strength <
    ui_type = "slider";
    ui_label = "Strength";
    ui_min = 0.01; ui_max = 0.5; ui_step = 0.01;
> = 0.1;

uniform float Radius <
    ui_type = "slider";
    ui_label = "Radius";
    ui_min = 0.3; ui_max = 2.0; ui_step = 0.05;
> = 1.0;

uniform float Curve <
    ui_type = "slider";
    ui_label = "Shadow Bias";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 1.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 IntensityWeight(float3 value, float3 sigma, float3 centre)
{
    float3 scaled = (value - centre) / sigma;
    return exp(-0.5 * scaled * scaled);
}

float SpatialWeight(float distance, float sigma)
{
    float scaled = distance / sigma;
    return exp(-0.5 * scaled * scaled);
}

float4 PS_Denoise(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 centre = tex2D(BackBuffer, uv).rgb;
    float3 intensity_sigma = max(pow(centre + 0.0001, Curve) * Strength, 0.0001);
    float spatial_sigma = max(Radius, 0.05);

    float3 sum = float3(0.0, 0.0, 0.0);
    float3 total = float3(0.0, 0.0, 0.0);

    [unroll] for (int y = -2; y <= 2; ++y)
    {
        [unroll] for (int x = -2; x <= 2; ++x)
        {
            float2 offset = float2(x, y);
            float3 tap = tex2D(BackBuffer, uv + offset * texel).rgb;
            float3 weight = IntensityWeight(tap, intensity_sigma, centre) *
                            SpatialWeight(length(offset), spatial_sigma);
            sum += weight * tap;
            total += weight;
        }
    }

    return float4(sum / total, 1.0);
}

technique Denoise <
    ui_label = "Denoise";
    ui_tooltip = "Edge preserving blur that clears dithering and compression noise while leaving outlines sharp. Ported from Anime4K.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Denoise;
    }
}
