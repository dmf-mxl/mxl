// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{ptr::NonNull, sync::Arc};

use crate::{
    DataFormat, Error, FlowConfigInfo, FlowRuntimeInfo, GrainReader, Result, SamplesReader,
    flow::{FlowInfo, is_discrete_data_format},
    instance::InstanceContext,
};

/// A wrapper around the MXL FlowReader instance to manage its lifetime and ensure proper cleanup.
pub(crate) struct FlowReaderInstance {
    context: Arc<InstanceContext>,
    inner: NonNull<mxl_sys::FlowReader_t>,
}
impl FlowReaderInstance {
    pub(crate) fn new(context: Arc<InstanceContext>, flow_id: &str) -> Result<Self> {
        let flow_id = std::ffi::CString::new(flow_id)?;
        let options = std::ffi::CString::new("")?;
        let mut reader: mxl_sys::FlowReader = std::ptr::null_mut();

        Error::from_status(unsafe {
            context.api.create_flow_reader(
                context.instance,
                flow_id.as_ptr(),
                options.as_ptr(),
                &mut reader,
            )
        })?;

        Ok(Self {
            context,
            inner: NonNull::new(reader)
                .ok_or_else(|| Error::Other("Failed to create flow reader.".to_string()))?,
        })
    }
    pub(crate) fn as_ptr(&self) -> mxl_sys::FlowReader {
        self.inner.as_ptr()
    }
}
impl Drop for FlowReaderInstance {
    fn drop(&mut self) {
        if let Err(err) = Error::from_status(unsafe {
            self.context
                .api
                .release_flow_reader(self.context.instance, self.inner.as_ptr())
        }) {
            tracing::error!("Failed to release MXL flow reader: {:?}", err);
        }
    }
}

// SAFETY: The native reader may be moved between threads, but must not be accessed
// concurrently. Do not implement Sync for this type.
unsafe impl Send for FlowReaderInstance {}

pub struct FlowReader {
    context: Arc<InstanceContext>,
    reader: Arc<FlowReaderInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for FlowReader {}

pub(crate) fn get_flow_info(
    context: &InstanceContext,
    reader: mxl_sys::FlowReader,
) -> Result<FlowInfo> {
    let mut flow_info: mxl_sys::FlowInfo = unsafe { std::mem::zeroed() };
    Error::from_status(unsafe { context.api.flow_reader_get_info(reader, &mut flow_info) })?;
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
    context: &InstanceContext,
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
    context: &InstanceContext,
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
    pub(crate) fn new(context: Arc<InstanceContext>, reader: FlowReaderInstance) -> Self {
        Self {
            context,
            // Arc provides shared ownership for conversions without making the native
            // reader safe for concurrent access.
            #[allow(clippy::arc_with_non_send_sync)]
            reader: Arc::new(reader),
        }
    }

    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader.as_ptr())
    }

    pub fn to_grain_reader(self) -> Result<GrainReader> {
        let config_info = self.get_info()?.config;
        if !config_info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to GrainReader for continuous flow of type \"{:?}\".",
                config_info.common().data_format()
            )));
        }
        let result = GrainReader::new(self.context.clone(), self.reader);
        Ok(result)
    }

    pub fn to_samples_reader(self) -> Result<SamplesReader> {
        let flow_type = self.get_info()?.config.value.common.format;
        if is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to SamplesReader for discrete flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = SamplesReader::new(self.context.clone(), self.reader);
        Ok(result)
    }
}
