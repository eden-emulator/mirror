// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float Distortion <
    ui_type = "slider";
    ui_label = "Distortion";
    ui_tooltip = "Positive bulges the picture outwards, negative pinches it inwards.";
    ui_min = -0.5; ui_max = 0.5; ui_step = 0.01;
> = 0.1;

uniform float Zoom <
    ui_type = "slider";
    ui_label = "Zoom";
    ui_tooltip = "Scales the picture to hide the edges the warp pulls in.";
    ui_min = 0.5; ui_max = 1.5; ui_step = 0.01;
> = 1.0;

void VS_PostProcess(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2(float(id & 2), float((id & 1) << 1));
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PS_LensDistortion(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float aspect = BUFFER_WIDTH * BUFFER_RCP_HEIGHT;
    float2 half_size = float2(aspect, 1.0);
    float2 unit = half_size / length(half_size);

    float2 centred = (uv - 0.5) * 2.0 * unit;
    float r2 = dot(centred, centred);
    centred *= 1.0 + Distortion * r2;
    centred /= max(Zoom, 0.001);

    float2 source = centred / (2.0 * unit) + 0.5;

    return float4(tex2D(BackBuffer, source).rgb, 1.0);
}

technique LensDistortion <
    ui_label = "Lens Distortion";
    ui_tooltip = "Barrel or pincushion warp, like looking through a wide angle lens.";
>
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_LensDistortion;
    }
}
