texture EdenBackBufferTex : COLOR;
sampler EdenBackBuffer { Texture = EdenBackBufferTex; };

uniform float Strength <
    ui_type = "slider";
    ui_label = "Strength";
    ui_tooltip = "How dark the corners become.";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 0.6;

uniform float Aspect <
    ui_type = "slider";
    ui_label = "Aspect";
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

float4 PS_Vignette(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 diff = uv - 0.5;
    diff.x *= Aspect;
    diff.y /= max(Aspect, 0.0001);

    float falloff = 1.0 - min(1.0, Strength * dot(diff, diff) * 2.0);
    float3 rgb = tex2D(EdenBackBuffer, uv).rgb;

    return float4(rgb * falloff, 1.0);
}

technique EdenVignette
{
    pass
    {
        VertexShader = VS_Eden;
        PixelShader = PS_Vignette;
    }
}
