using System.Net;
using System.Net.Sockets;

namespace LuefterConfigurator.Infrastructure.ModbusTcp;

public sealed class ModbusTcpGateway : IAsyncDisposable
{
    private readonly IPAddress bindAddress;
    private readonly int port;
    private readonly Func<byte, byte[], Task<byte[]>> pduHandler;
    private TcpListener? listener;
    private CancellationTokenSource? stopSource;

    public ModbusTcpGateway(IPAddress bindAddress, int port, Func<byte, byte[], Task<byte[]>> pduHandler)
    {
        this.bindAddress = bindAddress;
        this.port = port;
        this.pduHandler = pduHandler;
    }

    public bool IsRunning => listener is not null;

    public Task StartAsync(CancellationToken cancellationToken)
    {
        if (listener is not null)
        {
            return Task.CompletedTask;
        }

        listener = new TcpListener(bindAddress, port);
        listener.Start();
        stopSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        _ = AcceptLoopAsync(stopSource.Token);
        return Task.CompletedTask;
    }

    public Task StopAsync()
    {
        stopSource?.Cancel();
        listener?.Stop();
        listener = null;
        stopSource?.Dispose();
        stopSource = null;
        return Task.CompletedTask;
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
    }

    private async Task AcceptLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested && listener is not null)
        {
            try
            {
                var client = await listener.AcceptTcpClientAsync(cancellationToken);
                _ = HandleClientAsync(client, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (SocketException)
            {
                return;
            }
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        using var ownedClient = client;
        var stream = ownedClient.GetStream();
        var buffer = new byte[260];
        var count = await stream.ReadAsync(buffer, cancellationToken);
        if (count == 0)
        {
            return;
        }

        var frame = ModbusTcpFrame.Parse(buffer.AsSpan(0, count));
        var pdu = await pduHandler(frame.UnitId, frame.Pdu);
        var response = ModbusTcpFrame.Build(frame.TransactionId, frame.UnitId, pdu);
        await stream.WriteAsync(response, cancellationToken);
    }
}
