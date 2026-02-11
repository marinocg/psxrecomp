#include "psxrecomp/runtime/gpu.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
bool isOpcodeInRange(u8 opcode, u8 low, u8 high)
{
    return opcode >= low && opcode <= high;
}
} // namespace

size_t Gpu::expectedGp0Words(u8 opcode) const
{
    if (opcode == 0x00 || opcode == 0x01 || opcode == 0x1F)
    {
        return 1;
    }

    if (opcode == 0x02 || isOpcodeInRange(opcode, 0x60, 0x63))
    {
        return 3;
    }

    if (isOpcodeInRange(opcode, 0x20, 0x23) || isOpcodeInRange(opcode, 0x40, 0x47) ||
        isOpcodeInRange(opcode, 0x64, 0x67) || opcode == 0xA0 || opcode == 0xC0)
    {
        return 4;
    }

    if (isOpcodeInRange(opcode, 0x28, 0x2B) || isOpcodeInRange(opcode, 0x50, 0x57) ||
        opcode == 0x80)
    {
        return 5;
    }

    if (isOpcodeInRange(opcode, 0x30, 0x33))
    {
        return 6;
    }

    if (isOpcodeInRange(opcode, 0x24, 0x27) || isOpcodeInRange(opcode, 0x34, 0x37))
    {
        return 7;
    }

    if (isOpcodeInRange(opcode, 0x38, 0x3B))
    {
        return 8;
    }

    if (isOpcodeInRange(opcode, 0x2C, 0x2F) || isOpcodeInRange(opcode, 0x3C, 0x3F))
    {
        return 9;
    }

    if (opcode >= 0xE1 && opcode <= 0xE6)
    {
        return 1;
    }

    return 1;
}

size_t Gpu::expectedGp1Words(u8 opcode) const
{
    (void)opcode;
    return 1;
}

void Gpu::applyRegisterEffects(const GpuCommand& command, Registers& registers)
{
    if (!command.fromGp1)
    {
        switch (command.kind)
        {
        case GpuCommandKind::DrawMode:
            if (!command.words.empty())
            {
                registers.texturePage = static_cast<u16>(command.words[0] & 0x7FF);
            }
            break;
        case GpuCommandKind::DrawSprite:
            if (command.words.size() > 3 && isOpcodeInRange(command.opcode, 0x64, 0x67))
            {
                registers.clut = static_cast<u16>((command.words[2] >> 16) & 0x7FFF);
            }
            break;
        case GpuCommandKind::DrawingAreaTopLeft:
            if (!command.words.empty())
            {
                registers.drawAreaTopLeft = static_cast<u16>(command.words[0] & 0xFFFF);
            }
            break;
        case GpuCommandKind::DrawingAreaBottomRight:
            if (!command.words.empty())
            {
                registers.drawAreaBottomRight = static_cast<u16>(command.words[0] & 0xFFFF);
            }
            break;
        case GpuCommandKind::DrawingOffset:
            if (!command.words.empty())
            {
                registers.drawingOffset = static_cast<u16>(command.words[0] & 0xFFFF);
            }
            break;
        default:
            break;
        }
        return;
    }

    switch (command.kind)
    {
    case GpuCommandKind::DisplayEnable:
        if (!command.words.empty())
        {
            registers.displayEnabled = (command.words[0] & 0x1) == 0;
        }
        break;
    case GpuCommandKind::DisplayMode:
        if (!command.words.empty())
        {
            registers.interlaced = (command.words[0] & 0x20) != 0;
        }
        break;
    case GpuCommandKind::DmaDirection:
        if (!command.words.empty())
        {
            registers.dmaDirection = static_cast<Registers::DmaDirection>(command.words[0] & 0x3);
        }
        break;
    case GpuCommandKind::AcknowledgeIrq:
        registers.irqPending = false;
        break;
    case GpuCommandKind::InterruptRequest:
        registers.irqPending = true;
        break;
    case GpuCommandKind::DisplayVramStart:
        if (!command.words.empty())
        {
            registers.displayXStart = static_cast<u16>(command.words[0] & 0x3FE);
            registers.displayYStart = static_cast<u16>((command.words[0] >> 10) & 0x1FF);
        }
        break;
    case GpuCommandKind::DisplayHorizontalRange:
        if (!command.words.empty())
        {
            registers.displayXRangeStart = static_cast<u16>(command.words[0] & 0xFFF);
            registers.displayXRangeEnd = static_cast<u16>((command.words[0] >> 12) & 0xFFF);
        }
        break;
    case GpuCommandKind::DisplayVerticalRange:
        if (!command.words.empty())
        {
            registers.displayYRangeStart = static_cast<u16>(command.words[0] & 0x3FF);
            registers.displayYRangeEnd = static_cast<u16>((command.words[0] >> 10) & 0x3FF);
        }
        break;
    case GpuCommandKind::Reset:
        registers = {};
        break;
    case GpuCommandKind::ResetCommandBuffer:
        break;
    default:
        break;
    }
}

GpuCommand Gpu::decodePacket(const PacketState& packet) const
{
    GpuCommand command;
    command.opcode = packet.opcode;
    command.fromGp1 = packet.fromGp1;
    command.words = packet.words;

    if (packet.fromGp1)
    {
        switch (packet.opcode)
        {
        case 0x00:
            command.kind = GpuCommandKind::Reset;
            break;
        case 0x01:
            command.kind = GpuCommandKind::ResetCommandBuffer;
            break;
        case 0x02:
            command.kind = GpuCommandKind::AcknowledgeIrq;
            break;
        case 0x03:
            command.kind = GpuCommandKind::DisplayEnable;
            break;
        case 0x04:
            command.kind = GpuCommandKind::DmaDirection;
            break;
        case 0x05:
            command.kind = GpuCommandKind::DisplayVramStart;
            break;
        case 0x06:
            command.kind = GpuCommandKind::DisplayHorizontalRange;
            break;
        case 0x07:
            command.kind = GpuCommandKind::DisplayVerticalRange;
            break;
        case 0x08:
            command.kind = GpuCommandKind::DisplayMode;
            break;
        default:
            command.kind = GpuCommandKind::Unknown;
            break;
        }
        return command;
    }

    if (packet.opcode == 0x00)
    {
        command.kind = GpuCommandKind::Nop;
    }
    else if (packet.opcode == 0x01)
    {
        command.kind = GpuCommandKind::InterruptRequest;
    }
    else if (packet.opcode == 0x02)
    {
        command.kind = GpuCommandKind::FillRectangle;
    }
    else if (isOpcodeInRange(packet.opcode, 0x20, 0x27) ||
             isOpcodeInRange(packet.opcode, 0x30, 0x37))
    {
        command.kind = GpuCommandKind::DrawTriangle;
    }
    else if (isOpcodeInRange(packet.opcode, 0x28, 0x2F) ||
             isOpcodeInRange(packet.opcode, 0x38, 0x3F))
    {
        command.kind = GpuCommandKind::DrawQuad;
    }
    else if (isOpcodeInRange(packet.opcode, 0x40, 0x57))
    {
        command.kind = GpuCommandKind::DrawLine;
    }
    else if (isOpcodeInRange(packet.opcode, 0x58, 0x5F))
    {
        command.kind = GpuCommandKind::DrawPolyline;
    }
    else if (isOpcodeInRange(packet.opcode, 0x60, 0x67))
    {
        command.kind = GpuCommandKind::DrawSprite;
    }
    else if (packet.opcode == 0x80)
    {
        command.kind = GpuCommandKind::VramToVramBlit;
    }
    else if (packet.opcode == 0xA0)
    {
        command.kind = GpuCommandKind::CpuToVramSetup;
    }
    else if (packet.opcode == 0xC0)
    {
        command.kind = GpuCommandKind::VramToCpuSetup;
    }
    else if (packet.opcode == 0xE1)
    {
        command.kind = GpuCommandKind::DrawMode;
    }
    else if (packet.opcode == 0xE2)
    {
        command.kind = GpuCommandKind::TextureWindow;
    }
    else if (packet.opcode == 0xE3)
    {
        command.kind = GpuCommandKind::DrawingAreaTopLeft;
    }
    else if (packet.opcode == 0xE4)
    {
        command.kind = GpuCommandKind::DrawingAreaBottomRight;
    }
    else if (packet.opcode == 0xE5)
    {
        command.kind = GpuCommandKind::DrawingOffset;
    }
    else if (packet.opcode == 0xE6)
    {
        command.kind = GpuCommandKind::MaskBitSetting;
    }
    else
    {
        command.kind = GpuCommandKind::Unknown;
    }

    return command;
}

} // namespace runtime
} // namespace psxrecomp
