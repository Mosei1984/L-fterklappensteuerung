namespace LuefterConfigurator.Infrastructure.Serial;

public static class ModbusRtuCrc
{
    public static ushort Compute(ReadOnlySpan<byte> payload)
    {
        ushort crc = 0xFFFF;
        foreach (var value in payload)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x0001) != 0
                    ? (ushort)((crc >> 1) ^ 0xA001)
                    : (ushort)(crc >> 1);
            }
        }

        return crc;
    }

    public static byte[] Append(ReadOnlySpan<byte> payload)
    {
        var crc = Compute(payload);
        var result = new byte[payload.Length + 2];
        payload.CopyTo(result);
        result[^2] = (byte)(crc & 0xFF);
        result[^1] = (byte)(crc >> 8);
        return result;
    }
}
