using SharpProspero.Graphics.Agc;
using var writer=new BinaryWriter(File.Create(args[0]));
const int count=96;
writer.Write(count);
for(int i=0;i<count;++i) {
    float x=(i%8)*37.25f-50,y=(i/8)*11.5f-30,w=1+(i%5)*320.5f,h=1+(i%7)*90.25f;
    float near=(i%3)*.125f,far=.75f+(i%2)*.25f;
    int left=(i%6)*71-90,top=(i%4)*83-110,width=100+(i%9)*120,height=80+(i%8)*180;
    foreach(float v in new[]{x,y,w,h,near,far})writer.Write(v);
    foreach(int v in new[]{left,top,width,height})writer.Write(v);
    var viewport=new AgcViewport();viewport.SetViewport(x,y,w,h,near,far);
    viewport.SetScissor(left,top,left+width,top+height);
    foreach(var r in viewport.ToRegisters()){writer.Write(r.Offset);writer.Write((ushort)0);writer.Write(r.Value);}
}
