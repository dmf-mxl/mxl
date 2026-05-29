// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::sync::Arc;

use crate::{
    DataFormat, Error, GrainWriter, Result, SamplesWriter,
    flow::is_discrete_data_format,
    instance::{InstanceContext, create_flow_reader},
};

/// Generic MXL Flow Writer, which can be further used to build either the "discrete" (grain-based
/// data like video frames or meta) or "continuous" (audio samples) flow writers in MXL terminology.
pub struct FlowWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
    id: uuid::Uuid,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for FlowWriter {}

impl FlowWriter {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::FlowWriter,
        id: uuid::Uuid,
    ) -> Self {
        Self {
            context,
            writer,
            id,
        }
    }

    pub(crate) fn inner(&self) -> mxl_sys::FlowWriter {
        self.writer
    }

    pub fn to_grain_writer(mut self) -> Result<GrainWriter> {
        let flow_type = self.get_flow_type()?;
        if !is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to GrainWriter for continuous flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = GrainWriter::new(self.context.clone(), self.writer);
        self.writer = std::ptr::null_mut();
        Ok(result)
    }

    pub fn to_samples_writer(mut self) -> Result<SamplesWriter> {
        let flow_type = self.get_flow_type()?;
        if is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to SamplesWriter for discrete flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = SamplesWriter::new(self.context.clone(), self.writer);
        self.writer = std::ptr::null_mut();
        Ok(result)
    }

    fn get_flow_type(&self) -> Result<u32> {
        // This feels pretty ugly, but currently, the only way how to get a flow type in MXL is to
        // use a reader.
        let reader = create_flow_reader(&self.context, &self.id.to_string()).map_err(|error| {
            Error::Other(format!(
                "Error while creating flow reader to get the flow type: {error}"
            ))
        })?;
        let flow_info = reader.get_info().map_err(|error| {
            Error::Other(format!(
                "Error while getting flow type from temporary reader: {error}"
            ))
        })?;
        Ok(flow_info.config.value.common.format)
    }
}

impl Drop for FlowWriter {
    fn drop(&mut self) {
        if !self.writer.is_null()
            && let Err(err) = Error::from_status(unsafe {
                self.context
                    .api
                    .release_flow_writer(self.context.instance, self.writer)
            })
        {
            tracing::error!("Failed to release MXL flow writer: {:?}", err);
        }
    }
}
