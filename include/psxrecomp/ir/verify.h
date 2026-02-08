#pragma once

#include "psxrecomp/ir/ir.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief Result of IR verification.
 */
struct VerificationResult
{
    std::vector<std::string> errors;

    bool success() const { return errors.empty(); }
};

/**
 * @brief Verify CFG integrity and SSA correctness for a function.
 */
VerificationResult verifyFunction(const Function& function);

} // namespace ir
} // namespace psxrecomp
