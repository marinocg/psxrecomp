#pragma once

#include "psxrecomp/types.h"

#include <string>
#include <vector>

bool hasEmptyBoundaryWarning(const std::vector<std::string>& warnings);
void writeLe32(std::vector<psxrecomp::u8>& buffer, size_t offset, psxrecomp::u32 value);
std::vector<psxrecomp::u8> buildMinimalExe(psxrecomp::u32 loadSize);
std::vector<psxrecomp::u8> buildExeWithCodeAndAsciiData();
std::vector<psxrecomp::u8> buildExeWithSeparateEntryAndBaseVector();
std::vector<psxrecomp::u8> buildExeWithCopLoadBeforeIndirectJump();
std::vector<psxrecomp::u8> buildExeWithStoredDataPointer();
std::vector<psxrecomp::u8> buildExeWithCallbackPointerPassedToJal();
std::vector<psxrecomp::u8> buildExeWithLiteralDataPointer();
std::vector<psxrecomp::u8> buildExeWithLiteralFunctionPointerInData();
std::vector<psxrecomp::u8> buildExeWithDetachedSingletonFunctionPointer();
std::vector<psxrecomp::u8> buildExeWithDetachedSingletonResumeLabelPointer();
std::vector<psxrecomp::u8> buildExeWithClusteredCodePointersInData();
std::vector<psxrecomp::u8> buildExeWithMixedCodeAndStringPointerTable();
std::vector<psxrecomp::u8> buildExeWithClusteredPointersToDataTables();
std::vector<psxrecomp::u8> buildExeWithLocalJumpTableTargets();
std::vector<psxrecomp::u8> buildExeWithCodeBuiltCallbackTargetAfterPrefixLoads();
std::vector<psxrecomp::u8> buildExeWithGapAdjacentRegisterCallTarget();
std::vector<psxrecomp::u8> buildExeWithGapAdjacentPointerCellTarget();
std::vector<psxrecomp::u8> buildExeWithStoredGapAdjacentDispatchTarget();
std::vector<psxrecomp::u8> buildExeWithDelaySlotStoredDispatchTarget();
std::vector<psxrecomp::u8> buildExeWithReturnedDispatchTargetStore();
std::vector<psxrecomp::u8> buildExeWithPointerTableToTrapData();
std::vector<psxrecomp::u8> buildExeWithHarvestedDelaySlotSeed();
std::vector<psxrecomp::u8> buildExeWithBgezalDelaySlotBoundary();
