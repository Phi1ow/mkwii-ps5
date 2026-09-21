// SPDX-License-Identifier: GPL-3.0-only
using SharpProspero.Graphics.Agc;
using System.Numerics;
if(args.Length!=1)throw new ArgumentException("agc_layout_reference output.bin");
var dimensions=new List<(uint w,uint h)>();
foreach(uint w in new uint[]{1,3,31,64,65,127,128,129,257})
    foreach(uint h in new uint[]{1,5,64,65,128,259}) dimensions.Add((w,h));
dimensions.Add((1920,1080)); dimensions.Add((1024,256));
using var file=new BinaryWriter(File.Create(args[0]));
file.Write((uint)(dimensions.Count*2));
uint seed=0x12345678;
foreach(var(w,h) in dimensions)foreach(uint levels in new uint[]{1,(uint)BitOperations.Log2(Math.Max(w,h))+1}) {
    var description=new AgcSurfaceDescription(AgcTileMode.RenderTarget,AgcSurfaceDimension.TwoD,w,h,4,numMips:levels);
    var layout=AgcSurface.Compute(description);
    byte[] tiled=new byte[checked((int)layout.TotalSizeBytes)];
    using var linear=new MemoryStream();
    for(uint mip=0;mip<levels;++mip) {
        byte[] pixels=new byte[checked((int)AgcTiler.LinearSizeBytes(description,mip))];
        for(int i=0;i<pixels.Length;++i) { seed=unchecked(seed*1664525+1013904223);pixels[i]=(byte)(seed>>24); }
        linear.Write(pixels);AgcTiler.Tile(tiled,pixels,description,mip);
    }
    var t=new AgcTextureDescriptor();
    t.SetBaseAddress(0x1234560000);t.SetFormat(56);t.SetDimensions((int)w,(int)h);
    t.SetChannelOrder(AgcChannelSource.Red,AgcChannelSource.Green,AgcChannelSource.Blue,AgcChannelSource.Alpha);
    t.SetType(AgcImageType.Texture2D);t.SetTilingIndex(27);t.SetMipRange(0,(int)levels-1);t.SetMipLevelCount((int)levels);
    file.Write(w);file.Write(h);file.Write(levels);file.Write(layout.TotalSizeBytes);file.Write((ulong)linear.Length);
    for(int i=0;i<8;++i)file.Write(t[i]);
    file.Write(linear.ToArray());file.Write(tiled);
}
Console.WriteLine($"SharpProspero reference: {dimensions.Count*2} complete texture layouts, pixels and descriptors.");
