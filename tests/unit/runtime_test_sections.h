#pragma once

namespace psxrecomp
{
namespace runtime
{
class PsxSystem;
}
}

void runRuntimeStateSerializationChecks(psxrecomp::runtime::PsxSystem& system);
void runRuntimeLoggingAndDumpChecks(psxrecomp::runtime::PsxSystem& system);
void runRuntimeResourcePackChecks();
