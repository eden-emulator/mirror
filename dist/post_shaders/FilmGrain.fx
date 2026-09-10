// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Intensity <
    ui_type = "slider";
    ui_label = "Intensity";
    ui_min = 0.0; ui_max = 0.2; ui_step = 0.005;
> = 0.03;

uniform float Size <
    ui_type = "slider";
    ui_label = "Size";
    ui_tooltip = "Grain cell size in pixels.";
    ui_min = 1.0; ui_max = 4.0; ui_step = 1.0;
> = 1.0;

uniform float Colored <
    ui_type = "slider";
    ui_label = "Colour";
    ui_tooltip = "Zero gives monochrome grain, one gives independent noise per channel.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float Hash(float2 p)
{
    float3 scattered = frac(float3(p.x, p.y, p.x) * 0.1031);
    scattered += dot(scattered, scattered.yzx + 33.33);
    return frac((scattered.x + scattered.y) * scattered.z);
}

float4 PS_FilmGrain(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = tex2D(BackBuffer, uv).rgb;

    float2 cell = floor(pos.xy / max(Size, 1.0));
    float mono = Hash(cell) - 0.5;
    float3 chroma = float3(Hash(cell + 11.7), Hash(cell + 23.1), Hash(cell + 37.5)) - 0.5;
    float3 noise = lerp(float3(mono, mono, mono), chroma, Colored);

    float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    float response = 1.0 - abs(luma * 2.0 - 1.0);

    return float4(saturate(rgb + noise * Intensity * response), 1.0);
}

technique FilmGrain <
    ui_label = "Film Grain";
    ui_tooltip = "Adds photographic grain, strongest in the midtones and fading out in blacks and whites.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_FilmGrain;
    }
}
