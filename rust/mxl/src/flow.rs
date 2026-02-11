// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Flow types and metadata structures.
//!
//! This module defines data types for working with MXL flows, including:
//! - Flow readers and writers ([`reader`], [`writer`])
//! - Flow definitions and schema ([`flowdef`])
//! - Configuration and runtime metadata ([`FlowConfigInfo`], [`FlowRuntimeInfo`])
//! - Media format classification ([`DataFormat`])

pub mod flowdef;
pub mod reader;
pub mod writer;

use uuid::Uuid;

use crate::{Error, Result};

/// Media data format classification for MXL flows.
///
/// Flows are classified as either discrete (grain-based) or continuous
/// (sample-based) depending on the data format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DataFormat {
    /// Format not specified or unknown.
    Unspecified,
    /// Video data (discrete, grain-based).
    Video,
    /// Audio data (continuous, sample-based).
    Audio,
    /// Generic data packets (discrete, grain-based).
    Data,
}

impl From<u32> for DataFormat {
    /// Converts a raw MXL format constant to a [`DataFormat`] enum.
    fn from(value: u32) -> Self {
        match value {
            0 => DataFormat::Unspecified,
            mxl_sys::MXL_DATA_FORMAT_VIDEO => DataFormat::Video,
            mxl_sys::MXL_DATA_FORMAT_AUDIO => DataFormat::Audio,
            mxl_sys::MXL_DATA_FORMAT_DATA => DataFormat::Data,
            _ => DataFormat::Unspecified,
        }
    }
}

/// Determines whether a format uses discrete (grain-based) delivery.
///
/// Video and generic data flows use discrete delivery, while audio uses
/// continuous (sample-based) delivery.
///
/// This is an internal helper corresponding to the C API's inline
/// `mxlIsDiscreteDataFormat` function.
pub(crate) fn is_discrete_data_format(format: u32) -> bool {
    format == mxl_sys::MXL_DATA_FORMAT_VIDEO || format == mxl_sys::MXL_DATA_FORMAT_DATA
}

/// Complete flow information including configuration and runtime state.
///
/// Combines static configuration (format, rate, dimensions) with dynamic
/// runtime state (head index, last access times).
pub struct FlowInfo {
    /// Static flow configuration.
    pub config: FlowConfigInfo,
    /// Dynamic runtime state.
    pub runtime: FlowRuntimeInfo,
}

/// Flow configuration metadata.
///
/// Contains static information about a flow's format, rate, and buffer
/// configuration. This is set when the flow is created and does not change.
pub struct FlowConfigInfo {
    pub(crate) value: mxl_sys::FlowConfigInfo,
}

impl FlowConfigInfo {
    /// Returns discrete flow configuration (for video and data flows).
    ///
    /// # Returns
    ///
    /// A reference to the discrete-specific config fields.
    ///
    /// # Errors
    ///
    /// Returns an error if this flow is continuous (audio).
    pub fn discrete(&self) -> Result<&mxl_sys::DiscreteFlowConfigInfo> {
        if !is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, video or data required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.discrete })
    }

    /// Returns continuous flow configuration (for audio flows).
    ///
    /// # Returns
    ///
    /// A reference to the continuous-specific config fields.
    ///
    /// # Errors
    ///
    /// Returns an error if this flow is discrete (video/data).
    pub fn continuous(&self) -> Result<&mxl_sys::ContinuousFlowConfigInfo> {
        if is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, audio required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.continuous })
    }

    /// Returns the common configuration fields shared by all flow types.
    ///
    /// This provides access to format, ID, rate, and buffer hints that are
    /// common to both discrete and continuous flows.
    pub fn common(&self) -> CommonFlowConfigInfo<'_> {
        CommonFlowConfigInfo(&self.value.common)
    }

    /// Returns `true` if this is a discrete (grain-based) flow.
    ///
    /// Discrete flows include video and data flows, accessed via
    /// [`crate::GrainReader`]/[`crate::GrainWriter`].
    pub fn is_discrete_flow(&self) -> bool {
        is_discrete_data_format(self.value.common.format)
    }
}

/// Common flow configuration fields shared across all flow types.
///
/// This wrapper provides safe access to the format, ID, rate, and buffer
/// configuration that applies to both discrete and continuous flows.
pub struct CommonFlowConfigInfo<'a>(&'a mxl_sys::CommonFlowConfigInfo);

impl CommonFlowConfigInfo<'_> {
    /// Returns the flow's unique identifier (UUID).
    pub fn id(&self) -> Uuid {
        Uuid::from_bytes(self.0.id)
    }

    /// Returns the media data format of this flow.
    pub fn data_format(&self) -> DataFormat {
        DataFormat::from(self.0.format)
    }

    /// Returns `true` if this is a discrete (grain-based) flow.
    pub fn is_discrete_flow(&self) -> bool {
        is_discrete_data_format(self.0.format)
    }

    /// Returns the rate as a rational number (grain rate or sample rate).
    ///
    /// For discrete flows, this is the grain rate (e.g., frame rate).
    /// For continuous flows, this is the sample rate (e.g., 48000/1 Hz).
    ///
    /// Use [`Self::grain_rate`] or [`Self::sample_rate`] for type-checked access.
    pub fn grain_or_sample_rate(&self) -> mxl_sys::Rational {
        self.0.grainRate
    }

    /// Returns the grain rate for discrete flows (video/data).
    ///
    /// # Errors
    ///
    /// Returns an error if this flow is continuous (audio).
    pub fn grain_rate(&self) -> Result<mxl_sys::Rational> {
        let data_format = self.data_format();
        if data_format != DataFormat::Video && data_format != DataFormat::Data {
            return Err(Error::Other(format!(
                "Flow format is {:?}, grain rate is only relevant for discrete flows.",
                data_format
            )));
        }
        Ok(self.0.grainRate)
    }

    /// Returns the sample rate for continuous flows (audio).
    ///
    /// # Errors
    ///
    /// Returns an error if this flow is discrete (video/data).
    pub fn sample_rate(&self) -> Result<mxl_sys::Rational> {
        let data_format = self.data_format();
        if data_format != DataFormat::Audio {
            return Err(Error::Other(format!(
                "Flow format is {:?}, sample rate is only relevant for continuous flows.",
                data_format
            )));
        }
        Ok(self.0.grainRate)
    }

    /// Returns the maximum commit batch size hint from the writer.
    ///
    /// For continuous flows, writers can indicate their preferred batch size.
    /// Readers can use this to match the writer's pacing. Returns 0 if not set.
    pub fn max_commit_batch_size_hint(&self) -> u32 {
        self.0.maxCommitBatchSizeHint
    }

    /// Returns the maximum synchronization batch size hint.
    ///
    /// Indicates the optimal batch size for synchronized read operations.
    pub fn max_sync_batch_size_hint(&self) -> u32 {
        self.0.maxSyncBatchSizeHint
    }

    /// Returns the payload storage location flags.
    ///
    /// Indicates where the media payload is stored (e.g., shared memory, GPU memory).
    pub fn payload_location(&self) -> u32 {
        self.0.payloadLocation
    }

    /// Returns the device index for GPU-backed flows.
    ///
    /// For flows using GPU memory, this indicates which device. Returns -1
    /// for system memory flows.
    pub fn device_index(&self) -> i32 {
        self.0.deviceIndex
    }
}

/// Dynamic runtime information about a flow.
///
/// Contains state that changes as data is written and read, such as the
/// current head index and last access timestamps.
pub struct FlowRuntimeInfo {
    pub(crate) value: mxl_sys::FlowRuntimeInfo,
}

impl FlowRuntimeInfo {
    /// Returns the current head index of the flow.
    ///
    /// For discrete flows, this is the index of the last complete grain.
    /// For continuous flows, this is the index of the last written sample.
    pub fn head_index(&self) -> u64 {
        self.value.headIndex
    }

    /// Returns the TAI timestamp of the last write operation (in nanoseconds).
    pub fn last_write_time(&self) -> u64 {
        self.value.lastWriteTime
    }

    /// Returns the TAI timestamp of the last read operation (in nanoseconds).
    pub fn last_read_time(&self) -> u64 {
        self.value.lastReadTime
    }
}
