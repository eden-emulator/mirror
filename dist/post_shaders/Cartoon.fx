// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2012 PPSSPP Project
// SPDX-FileCopyrightText: guest(r)
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from cartoon.fsh in PPSSPP, Advanced Cartoon shader I by guest(r).

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform float EdgeStrength <
    ui_type = "slider";
    ui_label = "Edge Strength";
    ui_tooltip = "Darkness of the ink outline drawn around detected edges.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 0.5;

uniform float Levels <
    ui_type = "slider";
    ui_label = "Colour Levels";
    ui_tooltip = "How many bands the colours are quantised into.";
    ui_min = 2.0; ui_max = 16.0; ui_step = 1.0;
> = 4.0;

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

float4 PS_Cartoon(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 c00 = tex2D(BackBuffer, uv + texel * float2(-1.0, -1.0)).rgb;
    float3 c10 = tex2D(BackBuffer, uv + texel * float2( 0.0, -1.0)).rgb;
    float3 c20 = tex2D(BackBuffer, uv + texel * float2( 1.0, -1.0)).rgb;
    float3 c01 = tex2D(BackBuffer, uv + texel * float2(-1.0,  0.0)).rgb;
    float3 c11 = tex2D(BackBuffer, uv).rgb;
    float3 c21 = tex2D(BackBuffer, uv + texel * float2( 1.0,  0.0)).rgb;
    float3 c02 = tex2D(BackBuffer, uv + texel * float2(-1.0,  1.0)).rgb;
    float3 c12 = tex2D(BackBuffer, uv + texel * float2( 0.0,  1.0)).rgb;
    float3 c22 = tex2D(BackBuffer, uv + texel * float2( 1.0,  1.0)).rgb;

    const float3 dt = float3(1.0, 1.0, 1.0);

    float d1 = dot(abs(c00 - c22), dt);
    float d2 = dot(abs(c20 - c02), dt);
    float hl = dot(abs(c01 - c21), dt);
    float vl = dot(abs(c10 - c12), dt);
    float edge = EdgeStrength * (d1 + d2 + hl + vl) / (dot(c11, dt) + 0.15);

    float lc = Levels * length(c11);
    float f = frac(lc);
    f *= f;
    lc = (floor(lc) + f * f) / Levels + 0.05;

    float3 unit = normalize(max(c11, 0.0001));
    float3 quant = Levels * unit;
    float3 frct = frac(quant);
    frct *= frct;
    quant = floor(quant) + 0.05 * dt + frct * frct;

    float3 color = lc * (1.1 - edge * sqrt(edge)) * quant / Levels;
    return float4(saturate(color), 1.0);
}

technique Cartoon
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Cartoon;
    }
}
