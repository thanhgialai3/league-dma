#pragma once
#include <chrono>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "memory_reader_i.hpp"
#include "update_policy.hpp"

namespace utils::memory {

    /**
     * \brief Error codes for memory_holder operations.
     */
    enum class memory_holder_error {
        unknown,
        could_not_create_copy,
        could_not_create_updated_copy,
        invalid_memory_address,
    };

    /**
     * \brief A thread-safe, cache-aware wrapper around a remote memory value.
     *
     * Reads a value of type T from a remote process address through a memory_reader_i,
     * caching the result.  Updates are governed by an update_policy_t that implements
     * a hybrid caching strategy:
     *
     *   - On the preferred thread (e.g. render): always read fresh data
     *   - On other threads: throttle reads to at most once per min_interval
     *
     * This minimises expensive DMA / ROP reads while keeping rendering responsive.
     *
     * Thread-safety: m_value and m_last_updated are protected by a lightweight mutex.
     *
     * \tparam T trivially-copyable type stored in the holder.
     */
    template < typename T >
    class memory_holder {
    public:
        // -------------------------------------------------------------------
        // Construction
        // -------------------------------------------------------------------

        memory_holder( ) : m_created_from( std::this_thread::get_id( ) ) { }

        memory_holder( memory_reader_i* reader, uintptr_t address, update_policy_t policy = { } )
            : m_reader( reader ), m_address( address ), m_policy( policy ), m_created_from( std::this_thread::get_id( ) )
        {
            if ( m_reader && m_address )
            {
                auto read = m_reader->read< T >( m_address );
                if ( read.has_value( ) ) m_value = std::make_shared< T >( *read );
            }
        }

        memory_holder( const T& value, uintptr_t address, memory_reader_i* reader = nullptr,
                       update_policy_t policy = { } )
            : m_value( std::make_shared< T >( value ) ),
              m_reader( reader ),
              m_address( address ),
              m_policy( policy ),
              m_created_from( std::this_thread::get_id( ) )
        {
        }

        // -------------------------------------------------------------------
        // Accessors
        // -------------------------------------------------------------------

        auto get( ) -> std::shared_ptr< T >
        {
            std::lock_guard lock( m_mutex );
            if ( !is_valid_unlocked( ) ) return nullptr;
            return m_value;
        }

        [[nodiscard]] auto get( ) const -> std::shared_ptr< T >
        {
            std::lock_guard lock( m_mutex );
            if ( !is_valid_unlocked( ) ) return nullptr;
            return m_value;
        }

        [[nodiscard]] auto is_valid( ) const -> bool
        {
            std::lock_guard lock( m_mutex );
            return is_valid_unlocked( );
        }

        auto operator->( ) -> std::shared_ptr< T > { return get( ); }
        auto operator*( ) -> T&
        {
            auto v = get( );
            return *v;
        }

        auto operator->( ) const -> std::shared_ptr< T > { return get( ); }
        auto operator*( ) const -> const T&
        {
            auto v = get( );
            return *v;
        }

        explicit operator bool( ) const { return is_valid( ); }

        [[nodiscard]] auto get_address( ) const -> uintptr_t { return m_address; }

        // -------------------------------------------------------------------
        // Mutation
        // -------------------------------------------------------------------

        auto reset( ) -> void
        {
            std::lock_guard lock( m_mutex );
            m_address = 0;
            m_value.reset( );
        }

        /**
         * \brief Update m_value from m_address, respecting the update policy.
         * \return whether update was successful or skipped (false = throttled / not needed)
         */
        auto update( ) -> std::expected< bool, memory_holder_error >
        {
            // Check preferred-thread bypass without the lock (thread id is immutable)
            const bool is_preferred =
                m_policy.bypass_on_preferred_thread && std::this_thread::get_id( ) == m_policy.preferred_thread;

            std::lock_guard lock( m_mutex );

            if ( !is_preferred )
            {
                const auto now = std::chrono::steady_clock::now( );
                const auto delta =
                    std::chrono::duration_cast< std::chrono::microseconds >( now - m_last_updated ).count( );

                if ( delta >= 0 && delta <= m_policy.min_interval.count( ) ) return false;

                m_last_updated = now;
            }

            return force_update_unlocked( );
        }

        /**
         * \brief Force-read m_value from m_address, bypassing the update policy.
         */
        auto force_update( ) -> std::expected< bool, memory_holder_error >
        {
            std::lock_guard lock( m_mutex );
            return force_update_unlocked( );
        }

        // -------------------------------------------------------------------
        // Copy helpers
        // -------------------------------------------------------------------

        [[nodiscard]] auto create_copy( ) const -> std::expected< memory_holder< T >, memory_holder_error >
        {
            try
            {
                std::lock_guard lock( m_mutex );
                if ( m_value ) return memory_holder( *m_value, m_address, m_reader, m_policy );
                return create_updated_copy_unlocked( );
            }
            catch ( ... )
            {
                return std::unexpected( memory_holder_error::could_not_create_copy );
            }
        }

        [[nodiscard]] auto create_updated_copy( ) const -> std::expected< memory_holder< T >, memory_holder_error >
        {
            try
            {
                std::lock_guard lock( m_mutex );
                return create_updated_copy_unlocked( );
            }
            catch ( ... )
            {
                return std::unexpected( memory_holder_error::could_not_create_updated_copy );
            }
        }

        // -------------------------------------------------------------------
        // Configuration
        // -------------------------------------------------------------------

        auto set_policy( const update_policy_t& policy ) -> void { m_policy = policy; }
        [[nodiscard]] auto get_policy( ) const -> const update_policy_t& { return m_policy; }

        auto set_reader( memory_reader_i* reader ) -> void { m_reader = reader; }

    private:
        // -------------------------------------------------------------------
        // Internal helpers (caller MUST hold m_mutex when touching m_value)
        // -------------------------------------------------------------------

        [[nodiscard]] auto is_valid_unlocked( ) const -> bool { return m_address != 0 && m_value != nullptr; }

        /**
         * \brief Perform the actual memory read. Caller MUST hold m_mutex.
         */
        auto force_update_unlocked( ) -> std::expected< bool, memory_holder_error >
        {
            if ( !m_value ) return std::unexpected( memory_holder_error::invalid_memory_address );
            if ( !m_reader ) return std::unexpected( memory_holder_error::unknown );
            try
            {
                return m_reader->read_into< T >( m_address, m_value.get( ) );
            }
            catch ( ... )
            {
                return std::unexpected( memory_holder_error::unknown );
            }
        }

        [[nodiscard]] auto create_updated_copy_unlocked( ) const
            -> std::expected< memory_holder< T >, memory_holder_error >
        {
            // must have a reader to create updated copy
            if ( !m_reader ) return std::unexpected( memory_holder_error::could_not_create_updated_copy );
            return memory_holder( m_reader, m_address, m_policy );
        }

        // -------------------------------------------------------------------
        // Members
        // -------------------------------------------------------------------

        std::shared_ptr< T >                                 m_value{ };
        memory_reader_i*                                     m_reader{ nullptr };
        uintptr_t                                            m_address{ 0 };
        update_policy_t                                      m_policy{ };
        std::chrono::time_point< std::chrono::steady_clock > m_last_updated{ };
        std::thread::id                                      m_created_from{ };
        mutable std::mutex                                   m_mutex{ };
    };

} // namespace utils::memory
