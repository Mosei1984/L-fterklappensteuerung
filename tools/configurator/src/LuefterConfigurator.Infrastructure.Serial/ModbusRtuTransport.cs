namespace LuefterConfigurator.Infrastructure.Serial;

public static class ModbusRtuTransport
{
    public static byte[] BuildReadHoldingRegisters(byte slaveId, ushort startAddress, ushort count)
    {
        if (slaveId is < 1 or > 247)
        {
            throw new ArgumentOutOfRangeException(nameof(slaveId), "Modbus slave id must be 1..247.");
        }

        Span<byte> payload = stackalloc byte[6];
        payload[0] = slaveId;
        payload[1] = 0x03;
        payload[2] = (byte)(startAddress >> 8);
        payload[3] = (byte)startAddress;
        payload[4] = (byte)(count >> 8);
        payload[5] = (byte)count;
        return ModbusRtuCrc.Append(payload);
    }
}
