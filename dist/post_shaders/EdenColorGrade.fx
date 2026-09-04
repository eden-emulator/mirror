texture EdenBackBufferTex : COLOR;
sampler EdenBackBuffer { Texture = EdenBackBufferTex; };

uniform float Saturation <
    ui_type = "slider";
    ui_label = "Saturation";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 1.0;

uniform float Brightness <
    ui_type = "slider";
    ui_label = "Brightness";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 1.0;

uniform float Contrast <
    ui_type = "slider";
    ui_label = "Contrast";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 1.0;

uniform float Gamma <
    ui_type = "slider";
    ui_label = "Gamma";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
> = 1.0;

void VS_Eden(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
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

float4 PS_ColorGrade(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 rgb = tex2D(EdenBackBuffer, uv).rgb;

    float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    rgb = lerp(float3(luma, luma, luma), rgb, Saturation);
    rgb *= Brightness;
    rgb = (rgb - 0.5) * Contrast + 0.5;
    rgb = pow(max(rgb, 0.0), 1.0 / max(Gamma, 0.0001));

    return float4(saturate(rgb), 1.0);
}

technique EdenColorGrade
{
    pass
    {
        VertexShader = VS_Eden;
        PixelShader = PS_ColorGrade;
    }
}
