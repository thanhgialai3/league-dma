#pragma once

/**
 * \file entity_example.hpp
 * \brief Example showing how to use memory_holder<T> for entity caching.
 *
 * This file is provided as a reference / documentation for future engine integration.
 * It is NOT compiled as part of the project — include it only when adapting the
 * memory_holder system to a new engine (e.g. Unreal Engine actors).
 */

#include "memory_holder.hpp"
#include "memory_reader_i.hpp"
#include "update_policy.hpp"

namespace utils::memory::example {

    // -----------------------------------------------------------------------
    // Minimal vector type for illustration
    // -----------------------------------------------------------------------
    struct vec3 {
        float x{ }, y{ }, z{ };
    };

    // -----------------------------------------------------------------------
    // Example entity struct using memory_holder for each cached field.
    //
    // Each field is a separate memory_holder so it can:
    //   - be updated independently at its own cadence
    //   - respect the shared update_policy (render-thread bypass, throttle otherwise)
    //   - be read from a different remote address
    // -----------------------------------------------------------------------
    struct entity_t {
        memory_holder< vec3 >     position;
        memory_holder< int >      health;
        memory_holder< uintptr_t > mesh;

        /**
         * \brief Convenience: call update() on every holder.
         */
        auto update_all( ) -> void
        {
            position.update( );
            health.update( );
            mesh.update( );
        }

        /**
         * \brief Convenience: call force_update() on every holder.
         */
        auto force_update_all( ) -> void
        {
            position.force_update( );
            health.force_update( );
            mesh.force_update( );
        }
    };

    // -----------------------------------------------------------------------
    // Example construction helper.
    //
    // In real usage the reader and policy would be managed by your engine layer.
    // -----------------------------------------------------------------------
    inline auto make_entity(
        memory_reader_i* reader,
        uintptr_t        base_address,
        update_policy_t  policy
    ) -> entity_t
    {
        constexpr uintptr_t offset_position = 0x0280;
        constexpr uintptr_t offset_health   = 0x11E0;
        constexpr uintptr_t offset_mesh     = 0x42E8;

        return entity_t{
            .position = memory_holder< vec3 >( reader, base_address + offset_position, policy ),
            .health   = memory_holder< int >( reader, base_address + offset_health, policy ),
            .mesh     = memory_holder< uintptr_t >( reader, base_address + offset_mesh, policy ),
        };
    }

    // -----------------------------------------------------------------------
    // How multi-thread caching works and why it matters
    // -----------------------------------------------------------------------
    //
    // memory_holder uses a hybrid update policy (update_policy_t):
    //
    //   1. PREFERRED (render) THREAD — always gets fresh data.
    //      The render thread calls update() every frame and should_update() returns
    //      true unconditionally.  This keeps on-screen data responsive.
    //
    //   2. OTHER (feature / logic) THREADS — throttled.
    //      should_update() measures the time since the last read.  If less than
    //      min_interval (default 150 µs) has elapsed, it returns false and the
    //      caller reuses the cached value.
    //
    // Why this matters for DMA / external reads:
    //
    //   - Every memory read goes through DMA or a kernel boundary (e.g. ROP gadgets,
    //     FPGA-based DMA).  Each read has non-trivial latency (1–10+ µs).
    //   - Without throttling, multiple threads hammering the same addresses would:
    //       • saturate the DMA channel / bus
    //       • introduce contention and head-of-line blocking
    //       • cause cache thrashing on the host side
    //   - The throttle window (150 µs by default) is short enough that game-logic
    //     threads still see data that is effectively "this frame", but long enough
    //     to collapse redundant reads.
    //
    //   Result: render stays smooth, logic stays fast, DMA bandwidth is conserved.
    //

} // namespace utils::memory::example
