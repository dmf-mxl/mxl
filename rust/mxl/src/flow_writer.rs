use std::sync::Arc;

use crate::flow::is_discrete_data_format;
use crate::grain_writer::GrainWriter;
use crate::instance::create_flow_reader;
use crate::samples_writer::SamplesWriter;
use crate::{DataFormat, Error, Result, instance::InstanceContext};

/// Generic MXL Flow Writer, which can be further used to build either the "discrete" (grain-based
/// data like video frames or meta) or "continuous" (audio samples) flow writers in MXL terminology.
pub struct MxlFlowWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::mxlFlowWriter,
    id: uuid::Uuid,
}

impl MxlFlowWriter {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::mxlFlowWriter,
        id: uuid::Uuid,
    ) -> Self {
        Self {
            context,
            writer,
            id,
        }
    }

    pub fn to_grain_writer(mut self) -> Result<GrainWriter> {
        let flow_type = self.get_flow_type()?;
        if !is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert MxlFlowWriter to GrainWriter for continuous flow of type \"{:?}\".",
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
                "Cannot convert MxlFlowWriter to SamplesWriter for discrete flow of type \"{:?}\".",
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
        Ok(flow_info.value.common.format)
    }
}

impl Drop for MxlFlowWriter {
    fn drop(&mut self) {
        if !self.writer.is_null() {
            if let Err(err) = Error::from_status(unsafe {
                self.context
                    .api
                    .mxl_release_flow_writer(self.context.instance, self.writer)
            }) {
                tracing::error!("Failed to release MXL flow writer: {:?}", err);
            }
        }
    }
}
