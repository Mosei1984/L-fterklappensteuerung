using LuefterConfigurator.Infrastructure.Serial;

namespace LuefterConfigurator.Tests.Serial;

public sealed class UsbTextTransportTests
{
    [Fact]
    public async Task SendsCommandAndReturnsLine()
    {
        var fake = new FakeSerialConnection("ID: 17");
        var transport = new UsbTextTransport(fake);

        var response = await transport.SendCommandAsync("ID?", CancellationToken.None);

        Assert.Equal(["ID?"], fake.Written);
        Assert.Equal("ID: 17", response);
    }

    private sealed class FakeSerialConnection(params string[] responses) : ISerialConnection
    {
        private readonly Queue<string> responses = new(responses);

        public List<string> Written { get; } = [];

        public Task WriteLineAsync(string line, CancellationToken cancellationToken)
        {
            Written.Add(line);
            return Task.CompletedTask;
        }

        public Task<string> ReadLineAsync(TimeSpan timeout, CancellationToken cancellationToken)
            => Task.FromResult(responses.Dequeue());
    }
}
