// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/rational.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * A data structure with which information about a set of flows can be accumulated and then waited upon.
     */
    typedef struct mxlSyncObject_t
    {
        /**
         * The discrete rate at which the reader intends to consume flow data.
         */
        mxlRational tickRate;

        /**
         * The maximum source delay across all flow.
         */
        uint64_t sourceDelay;
    } mxlSyncObject;

    /**
     * Initialize the synchronization object with a specified target rate.
     *
     * \param[in] rate The target rate to initialize the object with.
     * \param[out] syncObject The synchronization object that should be initialized.
     * \return MXL_STATUS_OK if the rate was valid and the object was successfully initialized.
     */
    MXL_EXPORT
    mxlStatus mxlSyncObjectInitWithTickRate(mxlRational const* rate, mxlSyncObject* syncObject);

    /**
     * Initialize the synchronization object with the edit rate and source delay
     * of the discrete flow referred to by the passed flow reader.
     *
     * \param[in] reader The flow reader referring to the flow from which the synchronization object should be initialized.
     * \param[out] syncObject The synchronization object that should be initialized.
     * \return MXL_STATUS_OK if the rate was valid and the object was successfully initialized.
     */
    MXL_EXPORT
    mxlStatus mxlSyncObjectInitFromDiscreteFlow(mxlFlowReader reader, mxlSyncObject* syncObject);

    /**
     * Initialize the synchronization object with the edit rate and source delay of the continuous flow referred to by the passed flow reader, given
     * the batch size, in which the caller intends to consume samples from the flow.
     *
     * \param[in] reader The flow reader referring to the flow from which the synchronization object should be initialized.
     * \param[in] batchSize The batch size, in which the caller intends to consume samples from the flow.
     * \param[out] syncObject The synchronization object that should be initialized.
     * \return MXL_STATUS_OK if the rate was valid and the object was successfully initialized.
     */
    MXL_EXPORT
    mxlStatus mxlSyncObjectInitFromContinuousFLow(mxlFlowReader reader, int64_t batchSize, mxlSyncObject* syncObject);

    /**
     * Add the flow referred to by the passed flow reader to the synchronization object, effectively making sure that its source delay is accounted
     * for when waiting.
     *
     * \param[in] reader The flow reader referring to the flow that should be added to the synchronization object.
     * \param[out] syncObject The synchronization object that should account for the flows source delay.
     * \return MXL_STATUS_OK if the rate was valid and the object was successfully initialized.
     */
    MXL_EXPORT
    mxlStatus mxlSyncObjectAddFlow(mxlFlowReader reader, mxlSyncObject* syncObject);

    /**
     * Wait for the specified tickIndex to be available.
     *
     * \param[in] syncObject The synchronization object to wait on.
     * \param[in] tickIndex The tick index to wait for.
     * \return MXL_STATUS_OK if the index was successfully awaited, MXL_ERR_INTERRUPTED if the wait ended prematurely or MXL_ERR_INVALID_ARG if any of the arguments provided was invalid.
     */
    MXL_EXPORT
    mxlStatus mxlSyncObjectWaitFor(mxlSyncObject const* syncObject, uint64_t tickIndex);

#ifdef __cplusplus
}
#endif
