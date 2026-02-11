// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowData.cpp
 * @brief Base class for managing shared-memory flow metadata
 *
 * FlowData wraps the memory-mapped 'data' file containing mxlFlowInfo structures.
 * This file serves as the coordination point between readers and writers, containing:
 * - Flow configuration (format, rates, buffer sizes)
 * - Runtime state (head index, timestamps)
 * - Synchronization primitives (futex counter)
 *
 * The static_asserts in this file are CRITICAL for correctness. They ensure that:
 * 1. Structures have no padding bytes (required for safe mmap sharing)
 * 2. Structures have consistent size across compilations
 * 3. Memory layout matches between different processes
 *
 * If any assert fails, it indicates that the structure definition was modified
 * in a way that introduces padding or changes alignment, which would break
 * inter-process communication via mmap.
 *
 * Memory layout invariants:
 * - All structures must be trivially copyable
 * - No virtual functions (would add vtable pointer)
 * - Explicit padding to achieve natural alignment
 * - Fixed sizes verified by static_assert
 */

#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    /**
     * @brief Construct FlowData by opening or creating a flow metadata file
     *
     * This constructor memory-maps the flow's 'data' file and validates that
     * all the mxlFlowInfo structures have the expected memory layout. The
     * static_asserts ensure cross-process compatibility.
     *
     * @param flowFilePath Path to the flow's data file
     * @param mode Access mode (create, read-only, read-write)
     * @param lockMode Advisory lock mode (exclusive, shared, or none)
     *
     * @throws std::system_error if file operations fail
     * @throws Compilation error if structure layout is incorrect
     */
    FlowData::FlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : _flow{flowFilePath, mode, 0U, lockMode}
    {
        //
        // Validate structure memory layout for safe shared-memory usage
        // See: https://en.cppreference.com/w/cpp/types/has_unique_object_representations.html
        //
        // has_unique_object_representations ensures that:
        // - The structure has no padding bytes
        // - The structure is trivially copyable
        // - All bits contribute to the object's value
        //
        // This is essential because these structures are shared via mmap between processes
        // that may be compiled with different compilers or options. Any padding or
        // non-deterministic layout would cause data corruption.
        //

        static_assert(std::has_unique_object_representations_v<::mxlFlowInfo>,
            "mxlFlowInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlFlowConfigInfo>,
            "mxlFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlDiscreteFlowConfigInfo>,
            "mxlDiscreteFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from how "
            "it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlContinuousFlowConfigInfo>,
            "mxlContinuousFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from "
            "how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlFlowRuntimeInfo>,
            "mxlFlowRuntimeInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlGrainInfo>,
            "mxlGrainInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");

        // Verify fixed structure sizes for ABI stability
        // These sizes must NEVER change as they're part of the shared-memory protocol
        static_assert(sizeof(::mxlCommonFlowConfigInfo) == 128, "mxlCommonFlowConfigInfo does not have a size of 128 bytes");
        static_assert(sizeof(::mxlContinuousFlowConfigInfo) == 64, "mxlContinuousFlowConfigInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlDiscreteFlowConfigInfo) == 64, "mxlDiscreteFlowConfigInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlFlowConfigInfo) == 192, "mxlFlowConfigInfo does not have a size of 192 bytes");
        static_assert(sizeof(::mxlFlowRuntimeInfo) == 64, "mxlFlowRuntimeInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlFlowInfo) == 2048, "mxlFlowInfo does not have a size of 2048 bytes");
        static_assert(sizeof(::mxlGrainInfo) == 4096, "mxlGrainInfo does not have a size of 4096 bytes");
    }

    /**
     * @brief Virtual destructor for proper derived class cleanup
     */
    FlowData::~FlowData() = default;

    /**
     * @brief Check if this instance holds an exclusive lock on the flow data file
     *
     * An exclusive lock indicates that this process is the only one with write
     * access to the flow. This is used during garbage collection to determine
     * if a flow can be safely deleted.
     *
     * @return true if holding an exclusive lock, false if shared or no lock
     */
    bool FlowData::isExclusive() const
    {
        return _flow.isExclusive();
    }

    /**
     * @brief Attempt to upgrade from shared to exclusive lock
     *
     * This non-blocking operation tries to upgrade the advisory lock from shared
     * to exclusive. It succeeds only if no other processes hold locks on the file.
     *
     * Used during flow cleanup to detect if other processes are still using the flow.
     *
     * @return true if upgrade succeeded, false if other processes still have locks
     */
    bool FlowData::makeExclusive()
    {
        return _flow.makeExclusive();
    }
}
