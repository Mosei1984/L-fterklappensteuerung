namespace LuefterConfigurator.Domain;

public sealed record ExportArtifact(string FileName, string Content, string ContentType);

public interface IHomeAutomationExportAdapter
{
    string Id { get; }

    ExportArtifact Export(ControllerProfile profile, byte deviceId);
}

public interface IHomeAutomationMultiExportAdapter : IHomeAutomationExportAdapter
{
    IReadOnlyList<ExportArtifact> ExportAll(ControllerProfile profile, byte deviceId);
}
