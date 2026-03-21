#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <type_traits>

#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
#include <span>
#else
namespace std
{

template <typename T, size_t Extent> class Span
{
  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using pointer = T*;
    using iterator = T*;

    Span(pointer ptr, size_t count) : m_data(ptr)
    {
        (void)count;
    }

    template <typename U> explicit Span(std::array<U, Extent>& source) : m_data(source.data()) {}

    template <typename U> explicit Span(const std::array<U, Extent>& source) : m_data(source.data())
    {
    }

    pointer data() const
    {
        return m_data;
    }
    constexpr size_t size() const
    {
        return Extent;
    }
    iterator begin() const
    {
        return m_data;
    }
    iterator end() const
    {
        return m_data + Extent;
    }
    T& operator[](size_t index) const
    {
        return m_data[index];
    }

  private:
    pointer m_data = nullptr;
};

template <typename T, size_t Extent> using span = Span<T, Extent>;

} // namespace std
#endif

namespace psxrecomp
{
namespace runtime
{

class Disc
{
  public:
    enum class Region
    {
        Unknown,
        Japan,
        NorthAmerica,
        Europe,
    };

    virtual ~Disc() = default;

    virtual bool readUserSector(u32 lba, std::span<u8, 2048> out) = 0;

    virtual u32 userSectorCount() const
    {
        return 0;
    }

    virtual bool readRawSector2352(u32 lba, std::span<u8, 2352> out)
    {
        (void)lba;
        (void)out;
        return false;
    }

    virtual Region region() const
    {
        return Region::Unknown;
    }
};

} // namespace runtime
} // namespace psxrecomp
