using SharpProspero.Link;
using SharpProspero.Prx;
using System.Text.Json;
if (args.Length is < 2 or > 3) throw new ArgumentException("game_link manifest.json result.json [game.elf]");
if (args.Length == 3 && File.Exists(args[2])) throw new IOException("Refusing to overwrite an existing game ELF");
using var document = JsonDocument.Parse(File.ReadAllText(args[0]));
var options = new LinkOptions();
foreach (var item in document.RootElement.GetProperty("objects").EnumerateArray()) options.Objects.Add(item.GetString()!);
foreach (var item in document.RootElement.GetProperty("archives").EnumerateArray()) options.Archives.Add(item.GetString()!);
foreach (var item in document.RootElement.GetProperty("stubs").EnumerateArray()) options.Stubs.Add(item.GetString()!);
options.ExtraObjects.Add(ElfObjectReader.Read(CrtEmitter.BuildStartObject(), "sharpprospero_crt.o"));
options.ExtraObjects.Add(ElfObjectReader.Read(CompatEmitter.BuildObject(), "sharpprospero_compat.o"));
foreach (var entry in StubCatalog.Core)
    options.ExtraStubs.Add(StubLibrary.Parse(PrxStubEmitter.BuildObject(entry.Library, entry.Exports,
        entry.ModuleVersion, entry.LibraryVersion, entry.ModuleName, entry.Soname), entry.Library + ".prx"));
var result = Linker.Resolve(options);
var unresolvedUsers = result.Unresolved.ToDictionary(name => name, name => result.Included
    .Where(obj => obj.Symbols.Any(symbol => symbol.IsUndefined && symbol.Name == name))
    .Select(obj => obj.Origin).ToArray());
bool linked = false;
if (args.Length == 3)
{
    if (result.Unresolved.Count != 0 || result.SkippedMembers.Count != 0)
        throw new Exception("The full game must resolve without skipped archive members before writing an executable");
    string path = Path.GetFullPath(args[2]);
    var functions = new List<object>();
    byte[] module = DynamicWriter.Write(result, CrtEmitter.StartSymbol, ModuleKind.Executable,
        null, Path.GetFileName(path), functionMap: (origin, name, address, size) =>
            functions.Add(new { origin, name, address, size }));
    using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write);
    output.Write(module);
    File.WriteAllText(path + ".functions.json", JsonSerializer.Serialize(functions));
    linked = true;
    Console.WriteLine($"Wrote actual game executable {path} ({module.Length} bytes); native execution unverified.");
}
var report = new { includedObjects = result.Included.Count, definedSymbols = result.Defined.Count,
    includedOrigins = result.Included.Select(obj => obj.Origin).ToArray(),
    imports = result.Imports, unresolved = result.Unresolved, unresolvedUsers, skippedMembers = result.SkippedMembers,
    linkedGame = linked };
File.WriteAllText(args[1], JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"Game closure: {result.Included.Count} objects, {result.Defined.Count} definitions, {result.Imports.Count} imports, {result.Unresolved.Count} unresolved, {result.SkippedMembers.Count} skipped archive members.");
if (result.SkippedMembers.Count > 0) throw new Exception("Archive parsing incomplete; inspect skippedMembers");
