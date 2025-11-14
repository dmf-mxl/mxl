// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <list>
#include <mxl-internal/Instance.hpp>
#include <mxl/fabrics.h>
#include "Initiator.hpp"
#include "Target.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief The underlying implementation of an mxlFabricsInstance API object.
     *
     * A FabricInstance manages the creation and destruction of Targets and Initiators.
     */
    class FabricsInstance
    {
    public:
        /** \brief Create a FabricsInstance associated with the given MXL Instance.
         *
         * \param instance The MXL Instance to associate with this FabricsInstance. It is required that the lifetime of instance is at least as
         * long as this FabricsInstance.
         */
        FabricsInstance(mxl::lib::Instance* instance);
        ~FabricsInstance() = default;

        // Delete copy and move constructors and assignment operators
        FabricsInstance(FabricsInstance&&) = delete;
        FabricsInstance(FabricsInstance const&) = delete;
        FabricsInstance& operator=(FabricsInstance&&) = delete;
        FabricsInstance& operator=(FabricsInstance const&) = delete;

        /** \brief Convert this FabricsInstance to its API representation.
         *
         * \return The mxlFabricsInstance representing this FabricsInstance.
         */
        mxlFabricsInstance toAPI() noexcept;

        /** \brief Convert an mxlFabricsInstance API object to its underlying FabricsInstance.
         *
         * \param instance The mxlFabricsInstance to convert.
         * \return The FabricsInstance underlying the given mxlFabricsInstance.
         */
        static FabricsInstance* fromAPI(mxlFabricsInstance) noexcept;

        /** \brief create an uninitialized Target associated with this instance.
         */
        TargetWrapper* createTarget();

        /** \brief destroy a Target associated with this instance.
         */
        void destroyTarget(TargetWrapper*);

        /** \brief create an uninitialized Initiator associated with this instance.
         */
        InitiatorWrapper* createInitiator();

        /** \brief destroy an Initiator associated with this instance.
         */
        void destroyInitiator(InitiatorWrapper*);

    private:
        mxl::lib::Instance* _mxlInstance;
        std::list<TargetWrapper> _targets;
        std::list<InitiatorWrapper> _initiators;
    };

}
