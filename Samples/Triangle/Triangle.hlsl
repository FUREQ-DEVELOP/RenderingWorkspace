struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D PairTexture : register(t1);
SamplerState SceneSampler : register(s0);
SamplerState PairSampler : register(s1);

cbuffer Tint : register(b0) {
    float4 tint;
};

Texture2D AlternateTexture : register(t2);
SamplerState AlternateSampler : register(s3);
cbuffer AlternateTint : register(b4) {
    float4 alternate_tint;
};

VertexOut VSMain(uint vertex_id : SV_VertexID) {
    float2 positions[3] = {
        float2( 0.0,  0.65),
        float2( 0.65, -0.55),
        float2(-0.65, -0.55)
    };
    float3 colors[3] = {
        float3(0.88, 0.45, 0.30),
        float3(0.28, 0.70, 0.76),
        float3(0.91, 0.78, 0.48)
    };
    VertexOut output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    float2 uvs[3] = {float2(0.5, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};
    output.uv = uvs[vertex_id];
    return output;
}

struct VertexInput {
    float2 position : POSITION;
    float3 color : COLOR;
};

VertexOut VSBuffer(VertexInput input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    output.uv = float2(0.0, 0.0);
    return output;
}

struct UvVertexInput {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

VertexOut VSUv(UvVertexInput input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = float3(1.0, 1.0, 1.0);
    output.uv = input.uv;
    return output;
}

struct ColorUvVertexInput {
    float2 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

VertexOut VSColorUv(ColorUvVertexInput input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color.rgb;
    output.uv = input.uv;
    return output;
}

struct ScalarVertexInput {
    float2 position : POSITION;
    float3 color : COLOR;
    float intensity : TEXCOORD0;
};

VertexOut VSScalar(ScalarVertexInput input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color * input.intensity;
    output.uv = float2(0.0, 0.0);
    return output;
}

float4 PSMain(VertexOut input) : SV_Target {
    return float4(input.color, 1.0);
}

float4 TexturedPS(VertexOut input) : SV_Target {
    return SceneTexture.Sample(SceneSampler, input.uv);
}

float4 TexturedColorPS(VertexOut input) : SV_Target {
    return SceneTexture.Sample(SceneSampler, input.uv) * float4(input.color, 1.0);
}

float4 PairPS(VertexOut input) : SV_Target {
    return 0.5 * (SceneTexture.Sample(SceneSampler, input.uv) +
        PairTexture.Sample(SceneSampler, input.uv));
}

float4 PairSamplerPS(VertexOut input) : SV_Target {
    const float2 center = float2(0.5, 0.5);
    return 0.5 * (SceneTexture.Sample(SceneSampler, center) +
        PairTexture.Sample(PairSampler, center));
}

float4 TintedPS(VertexOut input) : SV_Target {
    return float4(input.color, 1.0) * tint;
}

float4 AlternatePS(VertexOut input) : SV_Target {
    return AlternateTexture.Sample(AlternateSampler, input.uv) * alternate_tint;
}
