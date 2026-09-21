using SharpProspero.Link;
var options = new LinkOptions();
options.Objects.AddRange(args);
var resolution = Linker.Resolve(options);
int before = 0, after = 0, ignored = 0;
foreach (var obj in resolution.Included)
    for (int i = 0; i < obj.Sections.Count; ++i) {
        if (obj.Sections[i].Name != ".eh_frame") continue;
        var entries = new List<EhFrame.Entry>();
        if (!EhFrame.TryParse(obj.Sections[i].Data, 0, entries)) throw new Exception("Invalid input frames");
        before += entries.Count;
        var pruned = EhFrame.PruneDiscarded(obj, i, resolution.DroppedSections);
        entries.Clear();
        if (!EhFrame.TryParse(pruned.Data, 0, entries)) throw new Exception("Invalid output frames");
        after += entries.Count;
        ignored += pruned.IgnoredRelocations.Count;
        if (pruned.Data.Length != obj.Sections[i].Data.Length) throw new Exception("Offsets changed");
        var second = EhFrame.PruneDiscarded(obj, i, new HashSet<(ElfObject, int)>());
        if (!second.Data.SequenceEqual(obj.Sections[i].Data) || second.IgnoredRelocations.Count != 0)
            throw new Exception("Kept frames modified");
        foreach (var r in obj.Relocations[i])
            if (resolution.DroppedSections.Contains((obj, obj.Symbols[(int)r.SymbolIndex].SectionIndex)) &&
                !pruned.IgnoredRelocations.Contains(r.Offset)) throw new Exception("Dangling discarded relocation");
    }
if (before != 3 || after != 2 || ignored != 2) throw new Exception($"Unexpected counts {before}/{after}/{ignored}");
Console.WriteLine("PASS: remove discarded COMDAT FDE and LSDA relocations, preserve kept frames and byte offsets (3 -> 2 FDEs).");
