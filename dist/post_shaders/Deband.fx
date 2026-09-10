// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2015 Niklas Haas
// SPDX-License-Identifier: MIT

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Threshold <
    ui_type = "slider";
    ui_label = "Threshold";
    ui_tooltip = "How flat a neighbourhood must be before it gets smoothed. Raise it to catch wider bands, lower it to keep more detail.";
    ui_min = 0.002; ui_max = 0.05; ui_step = 0.001;
> = 0.012;

uniform float Radius <
    ui_type = "slider";
    ui_label = "Radius";
    ui_tooltip = "How far the sampling reaches, in pixels. Wider gradients need a larger radius.";
    ui_min = 1.0; ui_max = 32.0; ui_step = 1.0;
> = 8.0;

uniform float Grain <
    ui_type = "slider";
    ui_label = "Dither";
    ui_tooltip = "Noise added to break up whatever banding survives the smoothing.";
    ui_min = 0.0; ui_max = 0.02; ui_step = 0.001;
> = 0.004;

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

float4 PS_Deband(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 centre = tex2D(BackBuffer, uv).rgb;
    float base = Hash(pos.xy) * 6.2831853;

    float3 total = float3(0.0, 0.0, 0.0);
    float3 deviation = float3(0.0, 0.0, 0.0);

    [unroll] for (int ring = 1; ring <= 2; ++ring)
    {
        float angle = base + float(ring) * 2.3999632;
        float reach = Radius * float(ring) * 0.5;
        float2 along = float2(cos(angle), sin(angle)) * reach;
        float2 across = float2(-along.y, along.x);

        float3 s0 = tex2D(BackBuffer, uv + along * texel).rgb;
        float3 s1 = tex2D(BackBuffer, uv - along * texel).rgb;
        float3 s2 = tex2D(BackBuffer, uv + across * texel).rgb;
        float3 s3 = tex2D(BackBuffer, uv - across * texel).rgb;

        total += s0 + s1 + s2 + s3;
        deviation = max(deviation, max(max(abs(s0 - centre), abs(s1 - centre)),
                                       max(abs(s2 - centre), abs(s3 - centre))));
    }

    float3 average = total * 0.125;
    float3 flatness = float3(1.0, 1.0, 1.0) -
                      smoothstep(Threshold * 0.5, Threshold, deviation);
    float3 result = lerp(centre, average, flatness);

    float dither = (Hash(pos.xy + float2(71.3, 41.7)) - 0.5) * Grain;

    return float4(saturate(result + dither), 1.0);
}

technique Deband <
    ui_label = "Deband";
    ui_tooltip = "Smooths the visible steps in gradients such as skies, then dithers whatever survives. Worth adding whenever large flat areas show rings.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Deband;
    }
}
