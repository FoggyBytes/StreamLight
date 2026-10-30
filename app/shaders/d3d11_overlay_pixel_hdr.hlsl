Texture2D<float4> theTexture : register(t0);
SamplerState theSampler : register(s0);

struct ShaderInput
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

// The overlays are painted in sRGB (OverlayManager). On an HDR stream the swap chain is
// BT.2020 with the PQ curve, and the plain overlay shader handed it sRGB values as they
// were: colours read against the wider primaries (the Xbox Y went orange) and the PQ curve,
// far steeper than sRGB's at the top, turned the anti-aliased edges of the text into steps.
// So: sRGB to linear, BT.709 to BT.2020, SDR white at the reference level of BT.2408, PQ.
static const float kWhiteNits = 203.0;

static const float3x3 kBt709To2020 = {
    0.627404, 0.329283, 0.043313,
    0.069097, 0.919540, 0.011362,
    0.016391, 0.088013, 0.895595
};

float3 srgbToLinear(float3 c)
{
    return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float3 linearToPq(float3 nits)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 y = pow(saturate(nits / 10000.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float4 main(ShaderInput input) : SV_TARGET
{
    float4 c = theTexture.Sample(theSampler, input.tex);
    float3 lin = mul(kBt709To2020, srgbToLinear(c.rgb));
    return float4(linearToPq(lin * kWhiteNits), c.a);
}
