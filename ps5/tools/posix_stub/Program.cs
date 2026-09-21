using System.Text.Json;
using SharpProspero.Prx;

if (args.Length != 2) throw new ArgumentException("posix_stub kernel-export-report.json output.o");
using var report = JsonDocument.Parse(File.ReadAllText(args[0]));
var root = report.RootElement;
if (root.GetProperty("module").GetString() != "libkernel.sprx" || root.GetProperty("processId").GetInt32() < 0)
    throw new Exception("Import evidence must come from an application, not the payload kernel variant");
foreach (var flag in new[] { "captureComplete", "positiveControlsPassed", "negativeControlPassed" })
    if (!root.GetProperty(flag).GetBoolean()) throw new Exception("Unverified export report: " + flag);
var symbols = root.GetProperty("symbols").EnumerateArray().ToDictionary(x => x.GetProperty("name").GetString()!);
HashSet<string> Scopes(string name) => symbols[name].GetProperty("symbolScopes").EnumerateArray()
    .Select(x => x.GetString()!).Select(x => x[x.IndexOf('#')..]).ToHashSet();
// stat and clock_gettime are established libScePosix imports in native probes.
// Every new symbol must occur in all of their scopes. Letters are module-local
// identifiers, not assumed library names. Application binding still needs a test.
var control = Scopes("stat");
if (control.Count == 0 || !control.SetEquals(Scopes("clock_gettime")))
    throw new Exception("POSIX control scopes disagree");
string[] names = ["pthread_getschedparam", "pthread_setschedparam", "pthread_key_delete",
    "pthread_rwlock_init", "pthread_rwlock_destroy", "pthread_rwlock_tryrdlock", "pthread_rwlock_trywrlock",
    "pthread_setcanceltype", "truncate", "sleep", "getpeername", "getsockname", "recvfrom", "sendto"];
foreach (var name in names)
    if (!symbols[name].GetProperty("present").GetBoolean() || !control.IsSubsetOf(Scopes(name)))
        throw new Exception("Missing shared POSIX export scopes for " + name);
var entry = StubCatalog.Core.Single(e => e.Library == "libScePosix");
PrxStubEmitter.WriteStub(entry.Library, names, args[1], entry.ModuleVersion,
    entry.LibraryVersion, entry.ModuleName, entry.Soname);
Console.WriteLine($"Prepared {names.Length} POSIX import candidates from verified export scopes; native binding must be tested.");
string[] kernelNames = ["chdir", "lstat", "access", "sceKernelStat", "dup", "execvp",
    "_exit", "__inet_ntop", "__inet_pton"];
var kernelControl = Scopes("sceKernelDlsym");
foreach (var name in kernelNames)
    if (!symbols[name].GetProperty("present").GetBoolean() || !kernelControl.IsSubsetOf(Scopes(name)))
        throw new Exception("Missing kernel export scopes for " + name);
var kernel = StubCatalog.Core.Single(e => e.Library == "libkernel");
PrxStubEmitter.WriteStub(kernel.Library, kernelNames, args[1] + ".kernel.o",
    kernel.ModuleVersion, kernel.LibraryVersion, kernel.ModuleName, kernel.Soname);
