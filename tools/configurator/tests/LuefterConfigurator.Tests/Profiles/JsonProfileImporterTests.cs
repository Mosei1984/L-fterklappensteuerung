using LuefterConfigurator.Profiles.Json;

namespace LuefterConfigurator.Tests.Profiles;

public sealed class JsonProfileImporterTests
{
    [Fact]
    public void ImportsValidProfile()
    {
        const string json = """
        {
          "id": "demo",
          "displayName": "Demo Controller",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [{"key":"device.id","displayName":"Device ID","minimum":1,"maximum":247,"unit":"id"}],
          "registers": [{"kind":"Holding","address":0,"name":"state","valueType":"UInt16","access":"ReadOnly"}]
        }
        """;

        var profile = JsonProfileImporter.Import(json);

        Assert.Equal("demo", profile.Id);
        Assert.True(profile.Validate().IsValid);
    }

    [Fact]
    public void RejectsDuplicateRegisters()
    {
        const string json = """
        {
          "id": "bad",
          "displayName": "Bad",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [],
          "registers": [
            {"kind":"Holding","address":0,"name":"a","valueType":"UInt16","access":"ReadOnly"},
            {"kind":"Holding","address":0,"name":"b","valueType":"UInt16","access":"ReadOnly"}
          ]
        }
        """;

        var exception = Assert.Throws<InvalidDataException>(() => JsonProfileImporter.Import(json));
        Assert.Contains("Duplicate register address 0", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RejectsIncompleteProfileWithoutNullReference()
    {
        const string json = """{"id":"incomplete"}""";

        var exception = Assert.Throws<InvalidDataException>(() => JsonProfileImporter.Import(json));

        Assert.Contains("displayName is required", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RejectsRegisterAddressOutsideUInt16Range()
    {
        const string json = """
        {
          "id": "bad-address",
          "displayName": "Bad Address",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [],
          "registers": [{"kind":"Holding","address":65536,"name":"bad","valueType":"UInt16","access":"ReadOnly"}]
        }
        """;

        var exception = Assert.Throws<InvalidDataException>(() => JsonProfileImporter.Import(json));

        Assert.Contains("outside UInt16 range", exception.Message, StringComparison.Ordinal);
    }
}
