texture EdenBackBufferTex : COLOR;
sampler EdenBackBuffer { Texture = EdenBackBufferTex; };

uniform float Amount <
    ui_type = "slider";
    ui_label = "Amount";
    ui_min = 0.0; ui_max = 3.0; ui_step = 0.01;
> = 0.6;

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

float4 PS_Sharpen(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 texel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);

    float3 centre = tex2D(EdenBackBuffer, uv).rgb;
    float3 blur = tex2D(EdenBackBuffer, uv + float2(-texel.x, 0.0)).rgb;
    blur += tex2D(EdenBackBuffer, uv + float2(texel.x, 0.0)).rgb;
    blur += tex2D(EdenBackBuffer, uv + float2(0.0, -texel.y)).rgb;
    blur += tex2D(EdenBackBuffer, uv + float2(0.0, texel.y)).rgb;
    blur *= 0.25;

    return float4(centre + (centre - blur) * Amount, 1.0);
}

technique EdenSharpen
{
    pass
    {
        VertexShader = VS_Eden;
        PixelShader = PS_Sharpen;
    }
}
