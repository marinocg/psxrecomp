#pragma once

#include "psxrecomp/iso/iso_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{

/**
 * @brief Aggregates multiple disc images for disc swap workflows.
 */
class MultiDiscSet
{
  public:
    struct DiscInfo
    {
        std::string path;
        std::string volumeLabel;
        bool isValid = false;
        std::vector<TrackInfo> tracks;
    };

    explicit MultiDiscSet(std::vector<std::string> discPaths);

    /**
     * @brief Open and parse all disc images.
     * @return true if all discs were opened successfully.
     */
    bool open();

    /**
     * @brief Get total disc count.
     */
    size_t getDiscCount() const;

    /**
     * @brief Get information for a disc index.
     */
    const DiscInfo& getDiscInfo(size_t index) const;

    /**
     * @brief Set active disc index for disc swaps.
     * @return true if index was valid.
     */
    bool setActiveDisc(size_t index);

    /**
     * @brief Get active disc index.
     */
    size_t getActiveDiscIndex() const;

    /**
     * @brief Access the active parser.
     */
    IsoParser* getActiveParser();

    /**
     * @brief Aggregate track tables across discs.
     */
    std::vector<TrackInfo> getCumulativeTracks() const;

  private:
    std::vector<std::unique_ptr<IsoParser>> m_parsers;
    std::vector<DiscInfo> m_discInfo;
    size_t m_activeDiscIndex;
};

} // namespace iso
} // namespace psxrecomp
