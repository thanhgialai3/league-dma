#pragma once
#include <chrono>
#include <thread>

namespace utils::memory {
    /**
     * \brief Configurable update policy for memory_holder.
     *
     * Implements a hybrid cache strategy:
     *   - On the preferred thread (e.g. render thread): always allow updates (no throttling)
     *   - On other threads: throttle updates to at most once per `min_interval`
     *
     * This prevents:
     *   - Excessive DMA / ROP memory reads from non-critical threads
     *   - Cache thrashing across threads
     * While keeping:
     *   - High responsiveness for the rendering pipeline
     *   - Overall high performance
     */
    struct update_policy_t {
        /// Minimum interval between updates on non-preferred threads.
        std::chrono::microseconds min_interval{ 150 };

        /// The thread that should always get fresh data (no throttling).
        std::thread::id preferred_thread{ };

        /// When true, the preferred thread bypasses time-based throttling entirely.
        bool bypass_on_preferred_thread{ true };

        /**
         * \brief Factory: create a policy that always allows updates (no throttling).
         */
        static auto always_update( ) -> update_policy_t
        {
            return update_policy_t{ std::chrono::microseconds{ 0 }, { }, false };
        }

        /**
         * \brief Factory: create a default policy with render-thread bypass.
         * \param render_thread_id the thread id of the render/preferred thread
         * \param interval throttle interval for other threads (default 150µs)
         */
        static auto with_preferred_thread(
            std::thread::id render_thread_id,
            std::chrono::microseconds interval = std::chrono::microseconds{ 150 }
        ) -> update_policy_t
        {
            return update_policy_t{ interval, render_thread_id, true };
        }
    };
} // namespace utils::memory
