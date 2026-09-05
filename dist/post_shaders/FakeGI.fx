// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

texture BrightTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA16F; };
sampler Bright { Texture = BrightTex; };

texture BounceTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA16F; };
sampler BounceMap { Texture = BounceTex; };

uniform float Threshold <
    ui_type = "slider";
    ui_label = "Light Threshold";
    ui_tooltip = "Luminance above which a pixel is treated as a light source.";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.45;

uniform float Radius <
    ui_type = "slider";
    ui_label = "Bounce Radius";
    ui_tooltip = "How far light spreads onto neighbouring surfaces.";
    ui_min = 0.5; ui_max = 8.0; ui_step = 0.1;
> = 3.0;

uniform float Bounce <
    ui_type = "slider";
    ui_label = "Bounce Strength";
    ui_tooltip = "Amount of coloured light bleeding onto the image.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 0.7;

uniform float Occlusion <
    ui_type = "slider";
    ui_label = "Contact Shadows";
    ui_tooltip = "Darkening applied where a pixel is dimmer than its surroundings.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 0.6;

uniform float Saturation <
    ui_type = "slider";
    ui_label = "Bounce Saturation";
    ui_tooltip = "How strongly the bounced light keeps the colour of its source.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
> = 1.3;

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

float Luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float4 PS_Extract(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 sum = tex2D(BackBuffer, uv).rgb;
    sum += tex2D(BackBuffer, uv + texel * float2(-1.0, -1.0)).rgb;
    sum += tex2D(BackBuffer, uv + texel * float2( 1.0, -1.0)).rgb;
    sum += tex2D(BackBuffer, uv + texel * float2(-1.0,  1.0)).rgb;
    sum += tex2D(BackBuffer, uv + texel * float2( 1.0,  1.0)).rgb;
    sum *= 0.2;

    float luma = Luma(sum);
    float mask = saturate((luma - Threshold) / max(1.0 - Threshold, 0.0001));

    float3 tint = lerp(float3(luma, luma, luma), sum, Saturation);
    return float4(tint * mask * mask, 1.0);
}

float4 PS_Spread(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 unit = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT) * Radius * 8.0;

    const float2 dirs[8] = {
        float2( 1.0,  0.0), float2(-1.0,  0.0),
        float2( 0.0,  1.0), float2( 0.0, -1.0),
        float2( 0.7071,  0.7071), float2(-0.7071,  0.7071),
        float2( 0.7071, -0.7071), float2(-0.7071, -0.7071)
    };

    float3 sum = tex2D(Bright, uv).rgb;
    float total = 1.0;

    for (int i = 0; i < 8; ++i)
    {
        sum += tex2D(Bright, uv + dirs[i] * unit * 0.45).rgb * 0.8;
        sum += tex2D(Bright, uv + dirs[i] * unit).rgb * 0.5;
        total += 1.3;
    }

    return float4(sum / total, 1.0);
}

float4 PS_Composite(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT) * 2.0;

    float3 color = tex2D(BackBuffer, uv).rgb;
    float centre = Luma(color);

    float around = Luma(tex2D(BackBuffer, uv + texel * float2(-1.0,  0.0)).rgb);
    around += Luma(tex2D(BackBuffer, uv + texel * float2( 1.0,  0.0)).rgb);
    around += Luma(tex2D(BackBuffer, uv + texel * float2( 0.0, -1.0)).rgb);
    around += Luma(tex2D(BackBuffer, uv + texel * float2( 0.0,  1.0)).rgb);
    around *= 0.25;

    float cavity = saturate(around - centre);
    float shadow = 1.0 - cavity * Occlusion;

    float3 bounce = tex2D(BounceMap, uv).rgb;
    float3 lit = color * shadow + bounce * Bounce * (1.0 - centre);

    return float4(saturate(lit), 1.0);
}

technique FakeGI
{
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Extract;
        RenderTarget = BrightTex;
    }
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Spread;
        RenderTarget = BounceTex;
    }
    pass
    {
        VertexShader = VS_PostProcess;
        PixelShader = PS_Composite;
    }
}
