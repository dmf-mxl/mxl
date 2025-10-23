// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
    Mux,
}

impl From<u32> for DataFormat {
    fn from(value: u32) -> Self {
        match value {
            0 => DataFormat::Unspecified,
            mxl_sys::MXL_DATA_FORMAT_VIDEO => DataFormat::Video,
            mxl_sys::MXL_DATA_FORMAT_AUDIO => DataFormat::Audio,
            mxl_sys::MXL_DATA_FORMAT_DATA => DataFormat::Data,
            mxl_sys::MXL_DATA_FORMAT_MUX => DataFormat::Mux,
            _ => DataFormat::Unspecified,
        }
    }
}

pub(crate) fn is_discrete_data_format(format: u32) -> bool {
    // Check is based on mxlIsDiscreteDataFormat, which is inline, thus not accessible in mxl_sys.
    format == mxl_sys::MXL_DATA_FORMAT_VIDEO || format == mxl_sys::MXL_DATA_FORMAT_DATA
}

pub struct FlowInfo {
    pub(crate) value: mxl_sys::mxlFlowInfo,
}

impl FlowInfo {
    pub fn discrete_flow_info(&self) -> Result<&mxl_sys::mxlDiscreteFlowInfo> {
        if !is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, video or data required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.discrete })
    }

    pub fn continuous_flow_info(&self) -> Result<&mxl_sys::mxlContinuousFlowInfo> {
        if is_discrete_data_format(self.value.common.format) {
            return Err(Error::Other(format!(
                "Flow format is {}, audio required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.continuous })
    }

    pub fn common_flow_info(&self) -> CommonFlowInfo<'_> {
        CommonFlowInfo(&self.value.common)
    }

    pub fn is_discrete_flow(&self) -> bool {
        is_discrete_data_format(self.value.common.format)
    }
}

pub struct CommonFlowInfo<'a>(&'a mxl_sys::mxlCommonFlowInfo);

impl CommonFlowInfo<'_> {
    pub fn id(&self) -> Uuid {
        Uuid::from_bytes(self.0.id)
    }

    pub fn max_commit_batch_size_hint(&self) -> u32 {
        self.0.maxCommitBatchSizeHint
    }
}
