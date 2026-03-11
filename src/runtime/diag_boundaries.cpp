#include "psxrecomp/runtime/diag_boundaries.h"

#include "psxrecomp/runtime/diag_explainers.h"
#include "psxrecomp/runtime/diag_validators.h"
#include "psxrecomp/runtime/logger.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

const char* boundaryKindLabel(BoundaryKind kind)
{
    switch (kind)
    {
    case BoundaryKind::IrqCallbackReturn:
        return "irq_callback_return";
    case BoundaryKind::BiosCallReturn:
        return "bios_call_return";
    case BoundaryKind::BiosMemoryOp:
        return "bios_memory_op";
    case BoundaryKind::DmaCompletion:
        return "dma_completion";
    case BoundaryKind::MmioPollSuspicion:
        return "mmio_poll_suspicion";
    case BoundaryKind::FunctionReturn:
        return "function_return";
    case BoundaryKind::StallDetected:
        return "stall_detected";
    }
    return "unknown";
}

} // namespace

DiagBoundaryDispatcher::DiagBoundaryDispatcher() = default;

void DiagBoundaryDispatcher::configure(const std::vector<BoundaryConfig>& configs,
                                       DiagValidatorEngine* validators,
                                       DiagExplainerEngine* explainers)
{
    m_configs = configs;
    m_validators = validators;
    m_explainers = explainers;
}

bool DiagBoundaryDispatcher::onBoundary(BoundaryKind kind, const u8* ram, size_t ramSize,
                                        RuntimeLogger* logger) const
{
    bool allPassed = true;

    for (const auto& config : m_configs)
    {
        if (config.kind != kind)
        {
            continue;
        }

        if (m_validators != nullptr)
        {
            for (const auto& validatorName : config.runValidators)
            {
                auto result = m_validators->runValidator(validatorName, ram, ramSize);
                if (!result.passed)
                {
                    allPassed = false;
                    if (logger != nullptr)
                    {
                        std::ostringstream msg;
                        msg << "boundary=" << boundaryKindLabel(kind)
                            << " validator=" << validatorName << " result=FAILED\n"
                            << result.report;
                        logger->log(LogLevel::Warn, "boundary", msg.str());
                    }

                    if (config.trapOnFailure)
                    {
                        if (logger != nullptr)
                        {
                            logger->log(LogLevel::Error, "boundary",
                                        "trap on boundary failure: " + validatorName);
                        }
                    }
                }
                else if (logger != nullptr)
                {
                    std::ostringstream msg;
                    msg << "boundary=" << boundaryKindLabel(kind) << " validator=" << validatorName
                        << " result=passed";
                    if (!result.report.empty())
                    {
                        msg << " " << result.report;
                    }
                    logger->log(LogLevel::Info, "boundary", msg.str());
                }
            }
        }
    }

    return allPassed;
}

bool DiagBoundaryDispatcher::hasBoundary(BoundaryKind kind) const
{
    for (const auto& config : m_configs)
    {
        if (config.kind == kind)
        {
            return true;
        }
    }
    return false;
}

size_t DiagBoundaryDispatcher::boundaryCount() const
{
    return m_configs.size();
}

} // namespace runtime
} // namespace psxrecomp
