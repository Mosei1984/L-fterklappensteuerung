namespace LuefterConfigurator.Infrastructure.Firmware;

public sealed class Uf2DriveDetector
{
    private readonly IReadOnlyList<string> roots;

    public Uf2DriveDetector(IReadOnlyList<string>? roots = null)
        => this.roots = roots ?? DriveInfo.GetDrives().Where(drive => drive.IsReady).Select(drive => drive.RootDirectory.FullName).ToArray();

    public string Detect()
        => roots.FirstOrDefault(root => File.Exists(Path.Combine(root, "INFO_UF2.TXT")))
            ?? throw new DriveNotFoundException("UF2 bootloader drive not found.");
}
