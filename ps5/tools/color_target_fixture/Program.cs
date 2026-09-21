using SharpProspero.Graphics.Agc;
using var output=new BinaryWriter(File.Create(args[0]));
ushort[] offsets=[0x318,0x31b,0x31c,0x31d,0x31e,0x31f,0x321,0x323,0x324,0x325,0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8];
output.Write(64u);
for(uint i=0;i<64;++i){
    uint width=new uint[]{1,257,1920,16384}[i%4],height=new uint[]{1,129,1080,16384}[(i/4)%4];
    ulong address=(0x12340000ul+i*0x200000ul)|((ulong)(i%8)<<40);
    output.Write(width);output.Write(height);output.Write(address);
    var defaults=new CxRegister[16];
    for(uint r=0;r<16;++r){uint v=unchecked(0x9e3779b9u*(i*17+r+1));defaults[r]=new CxRegister(offsets[r],v);output.Write(v);}
    var block=new CxRenderTarget().Init(defaults);
    AgcRenderTargetSetup.Initialize(block,new RenderTargetSpec(CxRenderTarget.Format.k8_8_8_8,
        CxRenderTarget.ChannelType.kUNorm,CxRenderTarget.ChannelOrder.kAlt,width,height,address));
    foreach(var r in block.Registers)output.Write(r.Value);
}
