// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::borrow::Cow;

use crate::format;
use crate::mxlsink::{
    self,
    imp::CAT,
    state::{DiscreteFormat, DiscreteState, FlowState},
};

use gstreamer as gst;
use mxl::{MxlInstance, Rational};
use tracing::trace;

/// Converts between MXL timestamps in nanoseconds and grain indices at one
/// grain rate.
pub(crate) trait GrainClock {
    fn timestamp_to_index(&self, timestamp: u64) -> Result<u64, gst::FlowError>;
    fn index_to_timestamp(&self, index: u64) -> Result<u64, gst::FlowError>;
}

/// `GrainClock` backed by the MXL library.
struct InstanceClock<'a> {
    instance: &'a MxlInstance,
    grain_rate: Rational,
}

impl GrainClock for InstanceClock<'_> {
    fn timestamp_to_index(&self, timestamp: u64) -> Result<u64, gst::FlowError> {
        self.instance
            .timestamp_to_index(timestamp, &self.grain_rate)
            .map_err(|_| gst::FlowError::Error)
    }

    fn index_to_timestamp(&self, index: u64) -> Result<u64, gst::FlowError> {
        self.instance
            .index_to_timestamp(index, &self.grain_rate)
            .map_err(|_| gst::FlowError::Error)
    }
}

/// Write position of a discrete stream, updated as grains are placed and
/// written.
#[derive(Debug, Default)]
pub(crate) struct GrainAlignment {
    /// Index one past the last committed grain; `None` before the first commit.
    pub next_index: Option<u64>,
    /// Offset in nanoseconds of the first grain's MXL timestamp from the grain
    /// it rounds to, subtracted before rounding later grains so that timestamp
    /// jitter of up to half a grain maps to consecutive indices.
    pub phase: Option<i64>,
}

impl GrainAlignment {
    /// Map `mxl_ts` to a grain index. The first call records the phase that
    /// later calls subtract before rounding.
    pub(crate) fn grain_index(
        &mut self,
        mxl_ts: u64,
        clock: &impl GrainClock,
    ) -> Result<u64, gst::FlowError> {
        let phase = match self.phase {
            Some(phase) => phase,
            None => {
                let grain_ts = clock.index_to_timestamp(clock.timestamp_to_index(mxl_ts)?)?;
                let phase = mxl_ts as i64 - grain_ts as i64;
                self.phase = Some(phase);
                phase
            }
        };
        clock.timestamp_to_index((mxl_ts as i64 - phase) as u64)
    }

    /// Decide which flow index a grain is written to, given the index its
    /// timestamp maps to. Returns `None` when that index was already written,
    /// because the writer only accepts indices after the last committed one.
    pub(crate) fn place(&self, ts_index: u64) -> Option<u64> {
        match self.next_index {
            Some(next) if ts_index < next => None,
            _ => Some(ts_index),
        }
    }
}

pub(crate) fn discrete(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    mxl_ts: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let flow_config = state.flow_config.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow_config
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let discrete_state = match state.flow_state.as_mut() {
        Some(FlowState::Discrete(discrete)) => discrete,
        _ => return Err(gst::FlowError::Error),
    };
    let media = match discrete_state.format {
        DiscreteFormat::Video => "VIDEO",
        DiscreteFormat::Data => "DATA",
    };
    trace!("{media} mapped mxl timestamp: {:#?}", mxl_ts);
    let clock = InstanceClock {
        instance: &state.instance,
        grain_rate,
    };
    let mxl_index = discrete_state.alignment.grain_index(mxl_ts, &clock)?;
    trace!(
        "{media} mapped mxl_index from running time: {:#?}",
        mxl_index
    );
    let Some(mxl_index) = discrete_state.alignment.place(mxl_index) else {
        gst::warning!(
            CAT,
            obj = element,
            "Dropping {} grain at index {}, which was already written (next index {:?})",
            media,
            mxl_index,
            discrete_state.alignment.next_index,
        );
        return Ok(gst::FlowSuccess::Ok);
    };

    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let payload = match discrete_state.format {
        DiscreteFormat::Video => Cow::Borrowed(map.as_slice()),
        DiscreteFormat::Data => Cow::Owned(
            format::data::mxl_smpte291_grain_from_gst_st2038(map.as_slice())
                .map_err(|_| gst::FlowError::Error)?,
        ),
    };
    // GstBaseSink (sync=true) has already waited for this buffer's running time,
    // so commit straight to the ring here: no separate pacing.
    commit_grain(element, payload.as_ref(), discrete_state, mxl_index)?;

    Ok(gst::FlowSuccess::Ok)
}

fn commit_grain(
    element: &gst::Element,
    payload: &[u8],
    discrete_state: &mut DiscreteState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let mut access = discrete_state.writer.open_grain(index).map_err(|e| {
        gst::error!(
            CAT,
            obj = element,
            "Failed to open grain at index {} (next index {:?}): {}",
            index,
            discrete_state.alignment.next_index,
            e
        );
        gst::FlowError::Error
    })?;
    let destination = access.payload_mut();
    let copy_len = std::cmp::min(destination.len(), payload.len());
    destination[..copy_len].copy_from_slice(&payload[..copy_len]);
    let total_slices = access.total_slices();
    access.commit(total_slices).map_err(|e| {
        gst::error!(
            CAT,
            obj = element,
            "Failed to commit grain at index {}: {}",
            index,
            e
        );
        gst::FlowError::Error
    })?;
    discrete_state.alignment.next_index = Some(index + 1);
    Ok(())
}
