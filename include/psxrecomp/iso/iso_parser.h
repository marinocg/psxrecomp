#pragma once

#include "psxrecomp/iso/track_info.h"
#include "psxrecomp/types.h"
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace iso
{

/**
 * @brief ISO 9660 Primary Volume Descriptor
 */
struct PrimaryVolumeDescriptor
{
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
    u32 pathTableLba;
    u32 optionalPathTableLba;
};

/**
 * @brief ISO 9660 Directory Record
 */
struct DirectoryRecord
{
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
class IsoParser
{
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
     * @brief Get parsed track metadata.
     * @return Track list (empty if unavailable)
     */
    const std::vector<TrackInfo>& getTracks() const;

    /**
     * @brief Get the first data track, if available.
     * @return TrackInfo when available, std::nullopt otherwise
     */
    std::optional<TrackInfo> getDataTrack() const;

    /**
     * @brief Get accumulated parser errors.
     * @return Error messages
     */
    const std::vector<std::string>& getErrors() const;

    /**
     * @brief Get the most recent parser error.
     * @return Error message, or empty if none
     */
    std::string getLastError() const;

    /**
     * @brief Check if ISO is valid PSX format
     * @return true if valid PSX ISO
     */
    bool isValid() const;

    /**
     * @brief List PSX-EXE candidates found on disc.
     * @return Paths to executables
     */
    std::vector<std::string> listExecutables();

  private:
    struct DirectoryInfo
    {
        u32 extent = 0;
        u32 size = 0;
    };

    std::string m_filename;
    std::string m_inputFilename;
    bool m_isOpen;
    bool m_isValid;
    u32 m_rawSectorSize;
    u32 m_dataTrackStartLba;
    u32 m_logicalBlockSize;
    bool m_useJoliet;
    std::ifstream m_stream;
    std::vector<u8> m_rawSectorScratch;
    PrimaryVolumeDescriptor m_pvd;
    std::vector<DirectoryRecord> m_rootDirectory;
    u32 m_rootExtent;
    u32 m_rootSize;
    u32 m_totalSectors;
    std::vector<TrackInfo> m_tracks;
    std::vector<std::string> m_errors;
    std::unordered_map<std::string, u32> m_pathTable;
    std::unordered_map<std::string, DirectoryInfo> m_directoryCache;

    bool readPVD();
    bool readDirectory(u32 extent, u32 size, std::vector<DirectoryRecord>& records);
    std::vector<u8> readSector(u32 sector);
    bool readRawSector(u32 sector, std::vector<u8>& buffer);
    bool readSectorInto(u32 sector, u8* buffer, size_t size);
    bool openStream();
    void addError(const std::string& message);
    bool loadPathTable();
    bool validateVolumeMetadata(const PrimaryVolumeDescriptor& pvd);
    bool getDirectoryInfo(const std::string& path, DirectoryInfo& info);
    bool readDirectorySelfSize(u32 extent, u32& outSize);
};

} // namespace iso
} // namespace psxrecomp
