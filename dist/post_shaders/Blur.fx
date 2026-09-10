// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Radius <
    ui_type = "slider";
    ui_label = "Radius";
    ui_tooltip = "How far the blur reaches, in pixels.";
    ui_min = 1.0; ui_max = 12.0; ui_step = 0.5;
> = 4.0;

uniform float Strength <
    ui_type = "slider";
    ui_label = "Strength";
    ui_tooltip = "Blend between the original image and the blurred one.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 1.0;

uniform float FocusSize <
    ui_type = "slider";
    ui_label = "Sharp Centre";
    ui_tooltip = "Size of the region left in focus at the centre of the screen. At zero the whole image is blurred evenly.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.0;

uniform float FocusSoftness <
    ui_type = "slider";
    ui_label = "Focus Falloff";
    ui_tooltip = "How gradually the sharp centre gives way to the blur.";
    ui_min = 0.05; ui_max = 1.0; ui_step = 0.05;
> = 0.4;

static const int TAPS = 17;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_Blur(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    float3 original = tex2D(BackBuffer, uv).rgb;

    float2 centred = (uv - 0.5) * float2(BUFFER_WIDTH * BUFFER_RCP_HEIGHT, 1.0) * 2.0;
    float distance_from_centre = length(centred);
    float no_focus = 1.0 - step(0.001, FocusSize);
    float focus = max(smoothstep(FocusSize, FocusSize + FocusSoftness, distance_from_centre), no_focus);
    float amount = Strength * focus;

    float angle = frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float2 spoke = float2(cos(angle), sin(angle));

    const float2 turn = float2(cos(2.39996323), sin(2.39996323));
    const float decay = exp(-2.0 / float(TAPS));
    float weight = exp(-1.0 / float(TAPS));

    float3 sum = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    [unroll] for (int i = 0; i < TAPS; ++i)
    {
        float reach = sqrt((float(i) + 0.5) / float(TAPS));
        sum += tex2D(BackBuffer, uv + spoke * reach * texel * Radius).rgb * weight;
        total += weight;
        weight *= decay;
        spoke = float2(spoke.x * turn.x - spoke.y * turn.y,
                       spoke.x * turn.y + spoke.y * turn.x);
    }

    float3 blurred = sum / total;
    return float4(lerp(original, blurred, amount), 1.0);
}

technique Blur <
    ui_label = "Blur";
    ui_tooltip = "Gaussian blur with an optional sharp centre, for softening the picture or faking depth of field.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Blur;
    }
}
