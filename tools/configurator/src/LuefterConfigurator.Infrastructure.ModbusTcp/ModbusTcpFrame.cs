namespace LuefterConfigurator.Infrastructure.ModbusTcp;

public sealed record ModbusTcpFrame(ushort TransactionId, byte UnitId, byte FunctionCode, byte[] Pdu)
{
    public static ModbusTcpFrame Parse(ReadOnlySpan<byte> buffer)
    {
        if (buffer.Length < 8)
        {
            throw new InvalidDataException("Modbus TCP frame too short.");
        }

        var transactionId = ReadUInt16(buffer[0..2]);
        var protocolId = ReadUInt16(buffer[2..4]);
        var length = ReadUInt16(buffer[4..6]);
        if (protocolId != 0)
        {
            throw new InvalidDataException("Unsupported Modbus TCP protocol id.");
        }

        if (length < 2 || buffer.Length != 6 + length)
        {
            throw new InvalidDataException("Invalid Modbus TCP length.");
        }

        var unitId = buffer[6];
        var pdu = buffer[7..].ToArray();
        return new ModbusTcpFrame(transactionId, unitId, pdu[0], pdu);
    }

    public static byte[] Exception(ushort transactionId, byte unitId, byte functionCode, byte exceptionCode)
        => Build(transactionId, unitId, [(byte)(functionCode | 0x80), exceptionCode]);

    public static byte[] Build(ushort transactionId, byte unitId, byte[] pdu)
    {
        var length = checked((ushort)(pdu.Length + 1));
        var result = new byte[7 + pdu.Length];
        WriteUInt16(result.AsSpan(0, 2), transactionId);
        WriteUInt16(result.AsSpan(2, 2), 0);
        WriteUInt16(result.AsSpan(4, 2), length);
        result[6] = unitId;
        pdu.CopyTo(result.AsSpan(7));
        return result;
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> bytes) => (ushort)((bytes[0] << 8) | bytes[1]);

    private static void WriteUInt16(Span<byte> target, ushort value)
    {
        target[0] = (byte)(value >> 8);
        target[1] = (byte)value;
    }
}
