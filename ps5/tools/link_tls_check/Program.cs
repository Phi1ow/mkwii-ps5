using System.Buffers.Binary;
using SharpProspero.Link;
using SharpProspero.Prx;
try {
if (args.Length != 3) throw new ArgumentException("Pass three compiled TLS fixture objects");
int checks = 0;
foreach (int[] order in new[] { new[] {0,1,2}, new[] {1,0,2}, new[] {2,1,0} })
{
    var options = new LinkOptions();
    foreach (var entry in StubCatalog.Core)
        options.ExtraStubs.Add(StubLibrary.Parse(PrxStubEmitter.BuildObject(entry.Library, entry.Exports,
            entry.ModuleVersion, entry.LibraryVersion, entry.ModuleName, entry.Soname), entry.Library + ".prx"));
    foreach (int i in order) options.Objects.Add(args[i]);
    var resolution = Linker.Resolve(options);
    if (resolution.Unresolved.Count != 0) throw new Exception("Fixture unresolved symbols");
    if (resolution.DroppedSections.Count < 2) throw new Exception("Fixture did not exercise duplicate TLS sections");
    var functions = new Dictionary<string, (ulong Address, ulong Size)>();
    byte[] elf = DynamicWriter.Write(resolution, "_start", functionMap:
        (_, name, address, size) => functions[name] = (address, size));
    ulong U64(int offset) => BinaryPrimitives.ReadUInt64LittleEndian(elf.AsSpan(offset));
    ushort U16(int offset) => BinaryPrimitives.ReadUInt16LittleEndian(elf.AsSpan(offset));
    uint U32(int offset) => BinaryPrimitives.ReadUInt32LittleEndian(elf.AsSpan(offset));
    var loads = new List<(ulong Offset, ulong Address, ulong Size)>();
    bool foundTls = false;
    for (int i = 0; i < U16(56); ++i)
    {
        int p = checked((int)U64(32) + i * U16(54));
        if (U32(p) == 1) loads.Add((U64(p+8), U64(p+16), U64(p+32)));
        if (U32(p) == 7)
        {
            if (U64(p+32) != 4 || U64(p+40) != 8) throw new Exception("TLS duplicates retained in template");
            if (U32(checked((int)U64(p+8))) != 17) throw new Exception("TLS initializer corrupted");
            foundTls = true; checks += 3;
        }
    }
    if (!foundTls) throw new Exception("No TLS segment");
    int Offset(string name)
    {
        var f = functions[name];
        var load = loads.Single(s => f.Address >= s.Address && f.Address+f.Size <= s.Address+s.Size);
        int start = checked((int)(load.Offset + f.Address-load.Address));
        // Clang's GD sequence relaxed to `mov fs:0,rax; lea displacement(rax),rax`.
        // Inspect the actual linked code, not the linker's internal offset table.
        var offsets = new List<int>();
        for (int i = start; i+7 <= start+(int)f.Size; ++i)
            if (elf[i]==0x48 && elf[i+1]==0x8d && elf[i+2]==0x80)
                offsets.Add(BinaryPrimitives.ReadInt32LittleEndian(elf.AsSpan(i+3)));
        if (offsets.Count != 1) throw new Exception($"Unexpected TLS getter encoding for {name}");
        return offsets[0];
    }
    foreach (string prefix in new[] { "zero", "initialized" })
    {
        int a = Offset(prefix+"A"), b = Offset(prefix+"B"), c = Offset(prefix+"C");
        if (a != b || a != c || a is not (-8 or -4)) throw new Exception($"Split TLS identity {prefix}: {a},{b},{c}");
        checks += 3;
    }
    if (Offset("zeroA") == Offset("initializedA")) throw new Exception("Distinct TLS symbols alias");
    ++checks;
}
Console.WriteLine($"PASS {checks} checks: TLS COMDAT identity, initialized/zero templates, undefined references and three link orders; host ELF inspection only.");
} catch (Exception error) { Console.Error.WriteLine(error); Environment.ExitCode = 1; }
