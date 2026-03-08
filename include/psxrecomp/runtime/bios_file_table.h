#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Disc;

/**
 * @brief A single BIOS file descriptor.
 *
 * Tracks an open file backed by ISO 9660 disc data:
 * its starting LBA, total byte size, and current read position.
 */
struct BiosFileDescriptor
{
    bool open = false;
    u32 lba = 0;        ///< Starting LBA on disc.
    u32 size = 0;        ///< File size in bytes.
    u32 position = 0;    ///< Current seek position in bytes.
    std::string path;    ///< Normalised path for debugging.
};

/**
 * @brief Minimal ISO 9660 directory entry used for firstfile/nextfile.
 */
struct BiosDirEntry
{
    std::string name;
    u32 lba = 0;
    u32 size = 0;
    u8 flags = 0;
};

/**
 * @brief Read-only BIOS file table backed by an ISO 9660 disc.
 *
 * Provides the minimal file I/O contract required by PSX BIOS
 * functions B0:32 FileOpen, B0:33 FileSeek, B0:34 FileRead,
 * B0:36 FileClose, B0:42 firstfile, and B0:43 nextfile.
 *
 * The table operates directly on a Disc* backend using
 * readUserSector, parsing the ISO 9660 PVD and directory
 * records on demand.
 */
class BiosFileTable
{
  public:
    /// PSX BIOS supports 16 file descriptors.  FDs 0–1 are reserved.
    static constexpr size_t MAX_FDS = 16;
    static constexpr int FIRST_USER_FD = 2;

    void reset();

    /**
     * @brief Bind a disc backend for sector reads.
     */
    void setDisc(Disc* disc);

    /**
     * @brief Open a file by PSX path (e.g. "cdrom:\\FILE.DAT;1").
     * @return File descriptor index (≥ 2), or -1 on failure.
     */
    int fileOpen(const std::string& rawPath);

    /**
     * @brief Seek within an open file.
     * @param fd    File descriptor.
     * @param offset  Byte offset.
     * @param whence  0 = SEEK_SET, 1 = SEEK_CUR, 2 = SEEK_END.
     * @return New absolute position, or -1 on error.
     */
    int fileSeek(int fd, int offset, int whence);

    /**
     * @brief Read bytes from an open file into a buffer.
     * @param fd    File descriptor.
     * @param dst   Destination buffer in host memory.
     * @param count Bytes to read.
     * @return Number of bytes actually read, or -1 on error.
     */
    int fileRead(int fd, u8* dst, u32 count);

    /**
     * @brief Close a file descriptor.
     * @return true on success.
     */
    bool fileClose(int fd);

    /**
     * @brief Begin directory enumeration (firstfile).
     *
     * Populates the internal directory listing cache and returns
     * the first entry, or nullptr if the directory is empty.
     */
    const BiosDirEntry* firstFile(const std::string& pattern);

    /**
     * @brief Continue directory enumeration (nextfile).
     * @return Next entry, or nullptr when exhausted.
     */
    const BiosDirEntry* nextFile();

    /**
     * @brief Read-only access to a file descriptor for inspection.
     */
    const BiosFileDescriptor* getFd(int fd) const;

  private:
    Disc* m_disc = nullptr;

    std::array<BiosFileDescriptor, MAX_FDS> m_fds{};

    /// ISO 9660 PVD state (lazy-initialised).
    bool m_pvdLoaded = false;
    u32 m_rootLba = 0;
    u32 m_rootSize = 0;

    /// Directory enumeration state for firstfile/nextfile.
    std::vector<BiosDirEntry> m_dirCache;
    size_t m_dirIndex = 0;

    bool ensurePvd();
    bool findFileInDirectory(u32 dirLba, u32 dirSize, const std::string& name, u32& outLba,
                             u32& outSize);
    bool resolveIsoPath(const std::string& normalised, u32& outLba, u32& outSize);
    bool listDirectory(u32 dirLba, u32 dirSize, std::vector<BiosDirEntry>& entries);

    static std::string normalisePath(const std::string& rawPath);
    static std::string stripVersionSuffix(const std::string& name);
};

} // namespace runtime
} // namespace psxrecomp
