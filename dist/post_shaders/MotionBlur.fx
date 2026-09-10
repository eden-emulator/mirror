// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Length <
    ui_type = "slider";
    ui_label = "Length";
    ui_tooltip = "How far the streaks reach, as a percentage of screen height.";
    ui_min = 0.0; ui_max = 20.0; ui_step = 0.5;
> = 5.0;

uniform float Zoom <
    ui_type = "slider";
    ui_label = "Zoom";
    ui_tooltip = "Streaks running out from the centre of the screen, as if rushing forward. The centre stays sharp.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 1.0;

uniform float Pan <
    ui_type = "slider";
    ui_label = "Pan";
    ui_tooltip = "Streaks running in one fixed direction, as if the camera were sweeping across.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
> = 0.0;

uniform float Angle <
    ui_type = "slider";
    ui_label = "Pan Angle";
    ui_tooltip = "Direction of the sweep, in degrees.";
    ui_min = 0.0; ui_max = 360.0; ui_step = 5.0;
> = 0.0;

uniform float Spin <
    ui_type = "slider";
    ui_label = "Spin";
    ui_tooltip = "Streaks curling around the centre, as if the camera were rolling. Negative turns the other way.";
    ui_min = -1.0; ui_max = 1.0; ui_step = 0.05;
> = 0.0;

static const int TAPS = 24;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_MotionBlur(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float aspect = BUFFER_WIDTH * BUFFER_RCP_HEIGHT;
    float2 to_screen = float2(aspect, 1.0);

    float2 centred = (uv - 0.5) * to_screen * 2.0;
    float2 outward = centred;
    float2 around = float2(-centred.y, centred.x);

    float radians_angle = Angle * 0.01745329;
    float2 sweep = float2(cos(radians_angle), sin(radians_angle));

    float2 velocity = sweep * Pan + outward * Zoom + around * Spin;
    velocity *= Length * 0.01;
    velocity /= to_screen;

    float jitter = frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453);

    float3 sum = float3(0.0, 0.0, 0.0);
    [unroll] for (int i = 0; i < TAPS; ++i)
    {
        float t = (float(i) + jitter) / float(TAPS) - 0.5;
        sum += tex2D(BackBuffer, uv + velocity * t).rgb;
    }

    return float4(sum / float(TAPS), 1.0);
}

technique MotionBlur <
    ui_label = "Motion Blur";
    ui_tooltip = "Camera style motion blur: streaks the picture outwards, sideways or around, without touching still detail at the centre.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_MotionBlur;
    }
}
