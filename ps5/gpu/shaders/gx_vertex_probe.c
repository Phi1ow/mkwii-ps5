/* SPDX-License-Identifier: GPL-3.0-only
 * First compiled vertex body, matching the supplied mesh VS resource and
 * interpolation interface. This is an ABI diagnostic, not full GX texgen.
 * build_vertex_shader.py prepends the verified primitive-export entry sequence
 * and relocates the compiler's register names to the supplied AGC input slots.
 */
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef float f4 __attribute__((ext_vector_type(4)));
extern f4 load_vertex(u4,unsigned,unsigned,unsigned,unsigned) __asm("llvm.amdgcn.struct.buffer.load.v4f32");
extern unsigned load_color(u4,unsigned,unsigned,unsigned,unsigned) __asm("llvm.amdgcn.struct.buffer.load.i32");
extern f4 load_uniform(u4,unsigned,unsigned) __asm("llvm.amdgcn.s.buffer.load.v4f32");
extern void export_vertex(unsigned,unsigned,float,float,float,float,_Bool,_Bool) __asm("llvm.amdgcn.exp.f32");
void gx_vertex(u4 vertices,u4 uniforms,unsigned vertex_id) {
    f4 p=load_vertex(vertices,vertex_id,0,0,0);
    f4 n=load_vertex(vertices,vertex_id,12,0,0);
    unsigned c=load_color(vertices,vertex_id,32,0,0);
    f4 pos=load_uniform(uniforms,0,0)*p.x+load_uniform(uniforms,16,0)*p.y+
        load_uniform(uniforms,32,0)*p.z+load_uniform(uniforms,48,0);
    f4 normal=load_uniform(uniforms,64,0)*n.x+load_uniform(uniforms,80,0)*n.y+load_uniform(uniforms,96,0)*n.z;
    export_vertex(12,15,pos.x,pos.y,pos.z,pos.w,1,0);
    export_vertex(32,15,normal.x,normal.y,normal.z,0,0,0);
    const float unorm=0x1.010102p-8f;
    export_vertex(33,15,((c>>16)&255)*unorm,((c>>8)&255)*unorm,(c&255)*unorm,(c>>24)*unorm,0,0);
}
