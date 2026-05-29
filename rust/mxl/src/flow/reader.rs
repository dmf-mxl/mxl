// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::sync::Arc;

use crate::{
    DataFormat, Error, FlowConfigInfo, FlowRuntimeInfo, GrainReader, Result, SamplesReader,
    flow::{FlowInfo, is_discrete_data_format},
    instance::InstanceContext,
};

pub struct FlowReader {
    context: Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for FlowReader {}

pub(crate) fn get_flow_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<FlowInfo> {
    let mut flow_info: mxl_sys::FlowInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(context.api.flow_reader_get_info(reader, &mut flow_info))?;
    }
    Ok(FlowInfo {
        config: FlowConfigInfo {
            value: flow_info.config,
        },
        runtime: FlowRuntimeInfo {
            value: flow_info.runtime,
        },
    })
}

pub(crate) fn get_config_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<FlowConfigInfo> {
    let mut config_info: mxl_sys::FlowConfigInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(
            context
                .api
                .flow_reader_get_config_info(reader, &mut config_info),
        )?;
    }
    Ok(FlowConfigInfo { value: config_info })
}

pub(crate) fn get_runtime_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<mxl_sys::FlowRuntimeInfo> {
    let mut runtime_info: mxl_sys::FlowRuntimeInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(
            context
                .api
                .flow_reader_get_runtime_info(reader, &mut runtime_info),
        )?;
    }
    Ok(runtime_info)
}

impl FlowReader {
    pub(crate) fn new(context: Arc<InstanceContext>, reader: mxl_sys::FlowReader) -> Self {
        Self { context, reader }
    }

    pub(crate) fn inner(&self) -> mxl_sys::FlowReader {
        self.reader
    }

    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader)
    }

    pub fn to_grain_reader(mut self) -> Result<GrainReader> {
        let flow_type = self.get_info()?.config.value.common.format;
        if !is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to GrainReader for continuous flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = GrainReader::new(self.context.clone(), self.reader);
        self.reader = std::ptr::null_mut();
        Ok(result)
    }

    pub fn to_samples_reader(mut self) -> Result<SamplesReader> {
        let flow_type = self.get_info()?.config.value.common.format;
        if is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to SamplesReader for discrete flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = SamplesReader::new(self.context.clone(), self.reader);
        self.reader = std::ptr::null_mut();
        Ok(result)
    }
}

impl Drop for FlowReader {
    fn drop(&mut self) {
        if !self.reader.is_null()
            && let Err(err) = Error::from_status(unsafe {
                self.context
                    .api
                    .release_flow_reader(self.context.instance, self.reader)
            })
        {
            tracing::error!("Failed to release MXL flow reader: {:?}", err);
        }
    }
}
