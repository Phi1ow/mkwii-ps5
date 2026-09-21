// Hardware encoding oracle: use the requested SharpProspero setters directly.
using SharpProspero.Graphics.Agc;
using var output=new BinaryWriter(File.Create(args[0]));
uint[] colorSource=[0,1,8,9,4,5,6,7],colorDest=[0,1,2,3,4,5,6,7];
uint[] alphaSource=[0,1,6,7,4,5,6,7],alphaDest=[0,1,4,5,4,5,6,7];
uint count=0;
foreach(uint src in Enumerable.Range(0,8)) foreach(uint dst in Enumerable.Range(0,8)) {
    uint seed=0xa5a50000u^(count*0x9e3779b9u);
    var control=new CxBlendControl().Init([new CxRegister(0x1e0,seed)]);
    control.SetBlend(CxBlendControl.Blend.kEnable).SetSeparateAlphaBlend(CxBlendControl.SeparateAlphaBlend.kEnable)
        .SetColorSourceMultiplier((CxBlendControl.ColorSourceMultiplier)colorSource[src])
        .SetColorDestMultiplier((CxBlendControl.ColorDestMultiplier)(colorDest[dst]<<8))
        .SetAlphaSourceMultiplier((CxBlendControl.AlphaSourceMultiplier)(alphaSource[src]<<16))
        .SetAlphaDestMultiplier((CxBlendControl.AlphaDestMultiplier)(alphaDest[dst]<<24))
        .SetColorBlendFunc(CxBlendControl.ColorBlendFunc.kAdd).SetAlphaBlendFunc(CxBlendControl.AlphaBlendFunc.kAdd);
    output.Write(src);output.Write(dst);output.Write(seed);output.Write(control[0].Value);++count;
}
Console.WriteLine($"Generated {count} SharpProspero blend references");
