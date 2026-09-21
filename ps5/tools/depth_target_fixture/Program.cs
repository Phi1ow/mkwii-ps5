// Use the supplied SharpProspero implementation as the independent reference.
using SharpProspero.Graphics.Agc;
using D = SharpProspero.Graphics.Agc.CxDepthRenderTarget;
using var output=new BinaryWriter(File.Create(args[0]));
output.Write(64u);
for(uint i=0;i<64;++i){
    uint width=new uint[]{1,257,1920,16384}[i%4],height=new uint[]{1,129,1080,16384}[(i/4)%4];
    ulong address=(0x12340000ul+i*0x200000ul)|((ulong)(i%8)<<40);
    output.Write(width);output.Write(height);output.Write(address);
    var defaults=new CxRegister[16];
    for(uint r=0;r<16;++r){uint v=unchecked(0x9e3779b9u*(i*17+r+1));defaults[r]=new CxRegister((ushort)r,v);output.Write(v);}
    var b=new D().Init(defaults);
    b.SetNumMipLevels(1).SetDepthFormat(D.DepthFormat.k32Float).SetNumFragments(D.NumFragments.k1)
     .SetHtileAcceleration(D.HtileAcceleration.kDisable).SetExpClearDepthAcceleration(D.ExpClearDepthAcceleration.kDisable)
     .SetZCompareBase(D.ZCompareBase.kZMin).SetEmbeddedSampleLocations(D.EmbeddedSampleLocations.kDisable)
     .SetPartiallyResidentDepth(D.PartiallyResidentDepth.kDisable)
     .SetTextureCompatiblePlaneCompression(D.TextureCompatiblePlaneCompression.kFullCompression)
     .SetStencilFormat(D.StencilFormat.kInvalid).SetTextureCompatibleStencil(D.TextureCompatibleStencil.kDisable)
     .SetHtileStencil(D.HtileStencil.kDisable).SetPartiallyResidentStencil(D.PartiallyResidentStencil.kDisable)
     .SetExpClearStencilAcceleration(D.ExpClearStencilAcceleration.kDisable)
     .SetBaseArraySliceIndex(0).SetLastArraySliceIndex(0).SetCurrentMipLevel(0)
     .SetDepthWrite(D.DepthWrite.kEnable).SetStencilWrite(D.StencilWrite.kDisable)
     .SetWidth(width).SetHeight(height).SetDepthClearValue(1).SetStencilClearValue(0)
     .SetDepthReadAddress(address).SetDepthWriteAddress(address)
     .SetStencilReadAddress(0).SetStencilWriteAddress(0).SetHtileAddress(0);
    foreach(var r in b.Registers)output.Write(r.Value);
}
var dims=new List<(uint w,uint h)>();
foreach(uint w in new uint[]{1,3,31,64,127,128,129,257,1024})
    foreach(uint h in new uint[]{1,5,65,128,259})dims.Add((w,h));
dims.Add((1920,1080));
output.Write((uint)dims.Count);
foreach(var(w,h) in dims){
    var description=new AgcSurfaceDescription(AgcTileMode.Depth,AgcSurfaceDimension.TwoD,w,h,4);
    var layout=AgcSurface.Compute(description);
    var linear=new byte[w*h*4];
    for(uint i=0;i<w*h;++i)BitConverter.TryWriteBytes(linear.AsSpan((int)i*4,4),i+1);
    var tiled=new byte[checked((int)layout.TotalSizeBytes)];
    AgcTiler.Tile(tiled,linear,description,0);
    output.Write(w);output.Write(h);output.Write(layout.TotalSizeBytes);output.Write(tiled);
}
Console.WriteLine($"SharpProspero: 64 depth blocks and {dims.Count} complete D32 tiled surfaces");
