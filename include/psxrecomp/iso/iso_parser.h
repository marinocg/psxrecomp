#pragma once

#include "psxrecomp/types.h"
#include <string>
#include <vector>

namespace psxrecomp {
namespace iso {

/**
 * @brief ISO 9660 Primary Volume Descriptor
 */
struct PrimaryVolumeDescriptor {
    u8 type;
    char identifier[5];
    u8 version;
    char systemId[32];
    char volumeId[32];
    u32 volumeSpaceSize;
    u32 volumeSetSize;
    u32 volumeSequenceNumber;
    u16 logicalBlockSize;
    u32 pathTableSize;
};

/**
 * @brief ISO 9660 Directory Record
 */
struct DirectoryRecord {
    u8 length;
    u8 extendedLength;
    u32 extentLocation;
    u32 dataLength;
    u8 recordingDateTime[7];
    u8 flags;
    u8 fileUnitSize;
    u8 interleaveGapSize;
    u16 volumeSequenceNumber;
    u8 nameLength;
    std::string name;
};

/**
 * @brief Parses PSX ISO/BIN files
 * 
 * This class handles reading and parsing PlayStation ISO 9660
 * CD-ROM images, including Mode 2 sectors and XA extensions.
 */
class IsoParser {
public:
    /**
     * @brief Construct a new Iso Parser object
     * @param filename Path to ISO/BIN file
     */
    explicit IsoParser(const std::string& filename);
    
    /**
     * @brief Destroy the Iso Parser object
     */
    ~IsoParser();
    
    /**
     * @brief Open and parse the ISO file
     * @return true if successful, false otherwise
     */
    bool open();
    
    /**
     * @brief Extract a file from the ISO
     * @param path Path to file within ISO
     * @return File contents, or empty vector on failure
     */
    std::vector<u8> extractFile(const std::string& path);
    
    /**
     * @brief Find the PSX executable (PSX-EXE)
     * @return Path to executable within ISO, or empty string if not found
     */
    std::string findExecutable();
    
    /**
     * @brief Get volume label
     * @return Volume label string
     */
    std::string getVolumeLabel() const;
    
    /**
     * @brief Check if ISO is valid PSX format
     * @return true if valid PSX ISO
     */
    bool isValid() const;

private:
    std::string m_filename;
    bool m_isOpen;
    PrimaryVolumeDescriptor m_pvd;
    std::vector<DirectoryRecord> m_rootDirectory;
    
    bool readPVD();
    bool readDirectory(u32 sector, std::vector<DirectoryRecord>& records);
    std::vector<u8> readSector(u32 sector);
};

} // namespace iso
} // namespace psxrecomp
