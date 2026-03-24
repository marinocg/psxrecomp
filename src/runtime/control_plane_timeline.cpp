#include "psxrecomp/runtime/control_plane_timeline.h"

namespace psxrecomp
{
namespace runtime
{

const char* cpEventKindLabel(CpEventKind kind)
{
    switch (kind)
    {
    case CpEventKind::IStatSet:
        return "i_stat_set";
    case CpEventKind::IStatClear:
        return "i_stat_clear";
    case CpEventKind::IMaskWrite:
        return "i_mask_write";
    case CpEventKind::DicrWrite:
        return "dicr_write";
    case CpEventKind::DicrFlagSet:
        return "dicr_flag_set";
    case CpEventKind::DpcrWrite:
        return "dpcr_write";
    case CpEventKind::ChcrWrite:
        return "chcr_write";
    case CpEventKind::ChcrTriggerClear:
        return "chcr_trigger_clear";
    case CpEventKind::ChcrBusyClear:
        return "chcr_busy_clear";
    case CpEventKind::DmaTransferStart:
        return "dma_transfer_start";
    case CpEventKind::DmaTransferDeferred:
        return "dma_transfer_deferred";
    case CpEventKind::DmaTransferDone:
        return "dma_transfer_done";
    case CpEventKind::DeviceIrqRise:
        return "device_irq_rise";
    case CpEventKind::DeviceIrqFall:
        return "device_irq_fall";
    case CpEventKind::DispatcherEnter:
        return "dispatcher_enter";
    case CpEventKind::DispatcherAck:
        return "dispatcher_ack";
    case CpEventKind::DispatcherExit:
        return "dispatcher_exit";
    default:
        return "unknown";
    }
}

} // namespace runtime
} // namespace psxrecomp
