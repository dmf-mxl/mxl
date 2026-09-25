// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, marker::PhantomData, ptr::NonNull, sync::Arc};

use crate::{
    Error, FlowConfigInfo, FlowRuntimeInfo, GrainReader, Result, SamplesReader, flow::FlowInfo,
    instance::InstanceContext,
};

/// A wrapper around the MXL FlowReader instance to manage its lifetime and ensure proper cleanup.
pub(crate) struct FlowReaderResource {
    pub(crate) context: Arc<InstanceContext>,
    inner: NonNull<mxl_sys::FlowReader_t>,
}
impl FlowReaderResource {
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
    /// # Safety
    ///
    /// This is only safe to be called by a single instance of FlowReader, GrainReader or SamplesReader at a time.
    /// The underlying MXL FlowReader is not thread-safe.
    pub(crate) unsafe fn as_ptr(&self) -> mxl_sys::FlowReader {
        self.inner.as_ptr()
    }
    pub(crate) fn keep_alive(self: &Arc<Self>) -> FlowReaderResourceKeepAlive {
        FlowReaderResourceKeepAlive {
            _instance: self.clone(),
        }
    }
}
impl Drop for FlowReaderResource {
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

// SAFETY:
// This type is only used to manage the lifetime of the underlying MXL FlowReader instance.
// No operation can be done directly, thus the type itself is thread-safe.
unsafe impl Send for FlowReaderResource {}
unsafe impl Sync for FlowReaderResource {}

/// A wrapper around FlowReaderResource to keep it alive. Users can't mutate the underlying pointer.
pub(crate) struct FlowReaderResourceKeepAlive {
    _instance: Arc<FlowReaderResource>,
}

/// Generic MXL Flow Reader, which can be further used to build either the "discrete" (grain-based
/// data like video frames or meta) or "continuous" (audio samples) flow writers in MXL terminology.
pub struct FlowReader {
    reader: Arc<FlowReaderResource>,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL reader
                                      // is not thread-safe.
}

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
    pub(crate) fn new(reader: FlowReaderResource) -> Self {
        Self {
            reader: Arc::new(reader),
            _not_sync: PhantomData,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowReaderResourceKeepAlive {
        self.reader.keep_alive()
    }

    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

    pub fn to_grain_reader(self) -> Result<GrainReader> {
        let config_info = self.get_info()?.config;
        if !config_info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to GrainReader for continuous flow of type \"{:?}\".",
                config_info.common().data_format()
            )));
        }
        let result = GrainReader::new(self.reader);
        Ok(result)
    }

    pub fn to_samples_reader(self) -> Result<SamplesReader> {
        let config_info = self.get_info()?.config;
        if config_info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to SamplesReader for discrete flow of type \"{:?}\".",
                config_info.common().data_format()
            )));
        }
        let result = SamplesReader::new(self.reader);
        Ok(result)
    }
}
