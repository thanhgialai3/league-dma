#pragma once
#include <cstdint>
#include <optional>

namespace utils::memory {
    /**
     * \brief Abstract interface for memory reading operations.
     *
     * Implementations may use DMA, ROP, kernel reads, or any other mechanism.
     * This abstraction decouples memory_holder from any specific engine or read backend.
     */
    class memory_reader_i {
    public:
        virtual ~memory_reader_i( ) = default;

        /**
         * \brief Reads a value of type T from the given address.
         * \param address remote memory address to read from
         * \param out pointer to the output buffer
         * \return true if the read was successful
         */
        virtual auto read_raw( uintptr_t address, void* out, size_t size ) -> bool = 0;

        /**
         * \brief Convenience: read a typed value into an optional.
         */
        template < typename T >
        auto read( uintptr_t address ) -> std::optional< T >
        {
            T value{ };
            if ( read_raw( address, &value, sizeof( T ) ) ) return value;
            return std::nullopt;
        }

        /**
         * \brief Convenience: read a typed value into an existing buffer.
         */
        template < typename T >
        auto read_into( uintptr_t address, T* out ) -> bool
        {
            return read_raw( address, out, sizeof( T ) );
        }
    };
} // namespace utils::memory
