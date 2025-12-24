// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

pub mod flowdef;
pub mod reader;
pub mod writer;

use uuid::Uuid;

use crate::{Error, Result};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DataFormat {
    Unspecified,
    Video,
    Audio,
    Data,
}

impl From<u32> for DataFormat {
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

pub(crate) fn is_discrete_data_format(format: u32) -> bool {
    // Check is based on mxlIsDiscreteDataFormat, which is inline, thus not accessible in mxl_sys.
    format == mxl_sys::MXL_DATA_FORMAT_VIDEO || format == mxl_sys::MXL_DATA_FORMAT_DATA
}

pub struct FlowInfo {
    pub config: FlowConfigInfo,
    pub runtime: FlowRuntimeInfo,
}

pub struct FlowConfigInfo {
    pub(crate) value: mxl_sys::FlowConfigInfo,
}

impl FlowConfigInfo {
    pub fn discrete(&self) -> Result<&mxl_sys::DiscreteFlowConfigInfo> {
        if !is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, video or data required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.discrete })
    }

    pub fn continuous(&self) -> Result<&mxl_sys::ContinuousFlowConfigInfo> {
        if is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, audio required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.continuous })
    }

    pub fn common(&self) -> CommonFlowConfigInfo<'_> {
        CommonFlowConfigInfo(&self.value.common)
    }

    pub fn is_discrete_flow(&self) -> bool {
        is_discrete_data_format(self.value.common.format)
    }
}

pub struct CommonFlowConfigInfo<'a>(&'a mxl_sys::CommonFlowConfigInfo);

impl CommonFlowConfigInfo<'_> {
    pub fn id(&self) -> Uuid {
        Uuid::from_bytes(self.0.id)
    }

    pub fn data_format(&self) -> DataFormat {
        DataFormat::from(self.0.format)
    }

    pub fn is_discrete_flow(&self) -> bool {
        is_discrete_data_format(self.0.format)
    }

    pub fn grain_or_sample_rate(&self) -> mxl_sys::Rational {
        self.0.grainRate
    }

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

    pub fn max_commit_batch_size_hint(&self) -> u32 {
        self.0.maxCommitBatchSizeHint
    }

    pub fn max_sync_batch_size_hint(&self) -> u32 {
        self.0.maxSyncBatchSizeHint
    }

    pub fn payload_location(&self) -> u32 {
        self.0.payloadLocation
    }

    pub fn device_index(&self) -> i32 {
        self.0.deviceIndex
    }
}

pub struct FlowRuntimeInfo {
    pub(crate) value: mxl_sys::FlowRuntimeInfo,
}

impl FlowRuntimeInfo {
    pub fn head_index(&self) -> u64 {
        self.value.headIndex
    }

    pub fn last_write_time(&self) -> u64 {
        self.value.lastWriteTime
    }

    pub fn last_read_time(&self) -> u64 {
        self.value.lastReadTime
    }
}
