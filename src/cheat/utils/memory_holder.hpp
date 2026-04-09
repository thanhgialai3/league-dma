#pragma once
#include "exceptions.hpp"
#include "../sdk/memory/memory.hpp"
#include "../features/threading.hpp"
#include "../runtime/app.hpp"

// New generic, engine-agnostic module
#include "memory/memory_holder.hpp"
#include "memory/memory_reader_i.hpp"
#include "memory/update_policy.hpp"

namespace utils {
    enum class EMemoryHolderError {
        unknown,
        could_not_create_copy,
        could_not_create_updated_copy,
        invalid_memory_address
    };

    // -----------------------------------------------------------------------
    // Bridge adapter: wraps sdk::memory::Memory behind the generic interface
    // -----------------------------------------------------------------------
    class AppMemoryReader final : public memory::memory_reader_i {
    public:
        auto read_raw( uintptr_t address, void* out, size_t size ) -> bool override{
            if ( !app || !app->memory ) return false;
            // The existing Memory class provides a pointer-based read<T> overload.
            // We call read_amount which handles arbitrary sizes.
            try {
                return app->memory->read_amount(
                    static_cast< intptr_t >( address ),
                    *static_cast< uint8_t* >( out ),
                    size
                );
            } catch ( ... ) { return false; }
        }
    };

    /**
     * \brief Returns a singleton AppMemoryReader instance.
     *
     * The reader is stateless so a single global instance is sufficient.
     */
    inline auto get_app_memory_reader( ) -> AppMemoryReader*{
        static AppMemoryReader instance;
        return &instance;
    }

    /**
     * \brief Builds an update_policy_t that mirrors the original MemoryHolder behaviour:
     *        render thread → always update, others → 150µs throttle.
     */
    inline auto make_default_policy( ) -> memory::update_policy_t{
        if ( g_threading )
            return memory::update_policy_t::with_preferred_thread( g_threading->render_thread );
        return { };
    }

    // -----------------------------------------------------------------------
    // MemoryHolder<T> — backwards-compatible wrapper
    //
    // Delegates to utils::memory::memory_holder<T> for the generic logic while
    // preserving the original API (throwing get(), intptr_t addresses, etc.)
    // so that no existing call-site needs to change.
    // -----------------------------------------------------------------------
    template <typename T>
    class MemoryHolder {
    public:
        MemoryHolder( ){ m_created_from = std::this_thread::get_id( ); }

        MemoryHolder( const T value, const intptr_t address ) :
            m_value( std::make_shared< T >( value ) ),
            m_address( address ){ m_created_from = std::this_thread::get_id( ); }

        explicit MemoryHolder( const intptr_t address ) :
            m_address( address ){
            auto read = app->memory->read< T >( m_address );
            if ( read.has_value( ) ) { m_value = std::make_shared< T >( *read ); }
        }

        auto get( ) -> std::shared_ptr< T >{
            if ( !is_valid( ) ) throw InvalidMemoryAddressException( );

            return m_value;
        }

        [[nodiscard]] auto get( ) const -> std::shared_ptr< T >{
            if ( !is_valid( ) ) throw InvalidMemoryAddressException( );

            return m_value;
        }

        [[nodiscard]] auto is_valid( ) const -> bool{ return !!m_address && !!m_value; }

        auto operator->( ) -> std::shared_ptr< T >{ return get( ); }

        auto operator*( ) -> T&{ return *get( ); }

        auto operator->( ) const -> std::shared_ptr< T >{ return get( ); }

        auto operator*( ) const -> const T&{ return *get( ); }

        auto reset( ) -> void{
            m_address = 0;
            m_value   = { };
        }

        /**
         * \brief Updates m_value from m_address.
         * \return whether update was successful or not
         */
        auto update( ) -> std::expected< bool, EMemoryHolderError >{
            if ( !should_update( ) ) return false;
            if ( !m_value ) return std::unexpected( EMemoryHolderError::invalid_memory_address );
            try { return app->memory->read< T >( m_address, m_value.get( ) ); } catch ( ... ) {
                return std::unexpected( EMemoryHolderError::unknown );
            }
        }

        auto force_update( ) -> std::expected< bool, EMemoryHolderError >{
            if ( !m_value ) return std::unexpected( EMemoryHolderError::invalid_memory_address );
            try { return app->memory->read< T >( m_address, m_value.get( ) ); } catch ( ... ) {
                return std::unexpected( EMemoryHolderError::unknown );
            }
        }

        [[nodiscard]] auto get_address( ) const -> intptr_t{ return m_address; }

        explicit operator bool( ) const{ return is_valid( ); }

        auto create_copy( ) const -> std::expected< MemoryHolder< T >, EMemoryHolderError >{
            try {
                auto& v = m_value;
                if ( !v ) return create_updated_copy( );
                return MemoryHolder( *v, m_address );
            } catch ( ... ) { return std::unexpected( EMemoryHolderError::could_not_create_copy ); }
        }

        auto create_updated_copy( ) const -> std::expected< MemoryHolder< T >, EMemoryHolderError >{
            try {
                if ( !should_update( ) ) return *this;
                return MemoryHolder( m_address );
            } catch ( ... ) { return std::unexpected( EMemoryHolderError::could_not_create_updated_copy ); }
        }

    private:
        auto should_update( ) -> bool{
            if ( g_threading->is_render_thread( ) ) return true;
            const auto now   = std::chrono::steady_clock::now( );
            const auto delta = std::chrono::duration_cast< std::chrono::microseconds >( now - m_last_updated ).count( );
            if ( delta <= 150 && delta >= 0 ) return false;

            m_last_updated = now;
            return true;
        }

    private:
        std::shared_ptr< T >                                 m_value{ };
        intptr_t                                             m_address{ };
        std::chrono::time_point< std::chrono::steady_clock > m_last_updated{ };
        std::thread::id                                      m_created_from{ };
    };
}
