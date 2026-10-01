// Three separate Y, Cb and Cr planes (PyroWave decoder output). The chroma
// planes are either half resolution (4:2:0) or full resolution (4:4:4); both
// are sampled with the same normalized texture coordinates.
Texture2D<min16float> luminancePlane : register(t0);
Texture2D<min16float> cbPlane : register(t1);
Texture2D<min16float> crPlane : register(t2);
SamplerState theSampler : register(s0);

struct ShaderInput
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

cbuffer CSC_CONST_BUF : register(b0)
{
    min16float3x3 cscMatrix;
    min16float3 offsets;
    min16float2 chromaOffset;
    min16float2 chromaTexMax;
};

min16float4 main(ShaderInput input) : SV_TARGET
{
    float2 chromaTex = min(input.tex + chromaOffset, chromaTexMax.rg);
    min16float3 yuv = min16float3(luminancePlane.Sample(theSampler, input.tex),
                                  cbPlane.Sample(theSampler, chromaTex),
                                  crPlane.Sample(theSampler, chromaTex));

    // Subtract the YUV offset for limited vs full range
    yuv -= offsets;

    // Multiply by the conversion matrix for this colorspace
    yuv = mul(yuv, cscMatrix);

    return min16float4(yuv, 1.0);
}
