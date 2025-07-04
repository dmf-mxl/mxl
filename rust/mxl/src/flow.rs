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
            1 => DataFormat::Video,
            2 => DataFormat::Audio,
            3 => DataFormat::Data,
            4 => DataFormat::Mux,
            _ => DataFormat::Unspecified,
        }
    }
}

pub struct FlowInfo {
    pub(crate) value: mxl_sys::FlowInfo,
}

impl FlowInfo {
    pub fn discrete_flow_info(&self) -> Result<&mxl_sys::DiscreteFlowInfo> {
        // Check is based on mxlIsDiscreteDataFormat, which is inline, thus not accessible in
        // mxl_sys.
        if self.value.common.format != mxl_sys::MXL_DATA_FORMAT_VIDEO
            && self.value.common.format != mxl_sys::MXL_DATA_FORMAT_DATA
        {
            return Err(Error::Other(format!(
                "Flow format is {}, video or data required.",
                self.value.common.format
            )));
        }
        Ok(unsafe { &self.value.__bindgen_anon_1.discrete })
    }
}
