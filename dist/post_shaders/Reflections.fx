// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Timer < source = "timer"; > = 0.0;

uniform float Horizon <
    ui_type = "slider";
    ui_label = "Reflection Line";
    ui_tooltip = "Height on screen where the reflective floor begins. Everything above it is left untouched.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.55;

uniform float Amount <
    ui_type = "slider";
    ui_label = "Amount";
    ui_tooltip = "How strongly the reflection shows through the floor.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.35;

uniform float Falloff <
    ui_type = "slider";
    ui_label = "Falloff";
    ui_tooltip = "How quickly the reflection fades as the floor comes towards the viewer. At zero it stays even.";
    ui_min = 0.0; ui_max = 4.0; ui_step = 0.1;
> = 1.2;

uniform float Perspective <
    ui_type = "slider";
    ui_label = "Perspective";
    ui_tooltip = "Stretches or squashes the mirrored image. One is a true mirror, higher values pull it towards the line.";
    ui_min = 0.2; ui_max = 2.0; ui_step = 0.05;
> = 1.0;

uniform float Ripple <
    ui_type = "slider";
    ui_label = "Ripple";
    ui_tooltip = "Amplitude of the waves disturbing the reflection. At zero the mirror is perfectly still.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.0;

uniform float RippleSpeed <
    ui_type = "slider";
    ui_label = "Ripple Speed";
    ui_tooltip = "How fast the waves travel.";
    ui_min = 0.0; ui_max = 4.0; ui_step = 0.1;
> = 1.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_Reflections(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 color = tex2D(BackBuffer, uv).rgb;

    float depth = uv.y - Horizon;
    float on_floor = step(0.0, depth);
    float span = max(1.0 - Horizon, 0.001);
    float distance_down = saturate(depth / span);

    float seconds = Timer * 0.001;
    float wave = sin(uv.x * 38.0 + seconds * RippleSpeed * 2.0) *
                 sin(uv.y * 21.0 - seconds * RippleSpeed * 1.3);
    float2 disturbance = float2(wave * 0.004, wave * 0.002) * Ripple * distance_down;

    float2 mirrored = float2(uv.x, Horizon - depth * Perspective) + disturbance;
    float3 reflection = tex2D(BackBuffer, saturate(mirrored)).rgb;

    float fade = pow(max(1.0 - distance_down, 0.0001), Falloff);
    float strength = Amount * fade * on_floor;

    return float4(lerp(color, reflection, strength), 1.0);
}

technique Reflections <
    ui_label = "Reflections";
    ui_tooltip = "Mirrors the picture into a reflective floor below a line you choose, with optional water ripples.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Reflections;
    }
}
