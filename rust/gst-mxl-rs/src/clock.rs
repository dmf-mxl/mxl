// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! The MXL-time ↔ pipeline-clock offset shared by `mxlsrc` (reading) and
//! `mxlsink` (writing).
//!
//! The MXL elements do **not** provide a pipeline clock. MXL time is
//! `clock_gettime(CLOCK_TAI)`, a wall clock that can step (NTP, leap seconds,
//! or a hypervisor time sync), so pacing `GstBaseSink` on it stalls the
//! pipeline. Instead the pipeline runs on whatever clock it selects (the
//! default monotonic system clock) and each element tracks the constant offset
//! `D = mxl_now - pipeline_clock_now`, sampled once and shared between the
//! pipeline's MXL elements.

use gst::glib;
use gst::prelude::*;
use gstreamer as gst;

use std::sync::Arc;
use std::sync::Mutex;

/// Internal clock-offset failure (mutex poison, context setup, etc.). Distinct
/// from `Ok(None)` in [`ClockOffsetExt::resolve_clock_offset`], which means the
/// pipeline clock or MXL instance is not ready yet.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub(crate) enum ClockOffsetError {
    Failed,
}

impl ClockOffsetError {
    pub(crate) fn into_error_message(self) -> gst::ErrorMessage {
        let _ = self;
        gst::error_msg!(gst::CoreError::Failed, ["Internal clock offset error"])
    }
}

/// MXL-time → pipeline-clock offset `D = mxl_now - pipeline_clock_now`.
///
/// The constant offset between MXL (TAI) time and the pipeline clock, sampled
/// once. `None` when the element has no clock yet.
///
/// A reader subtracts `D + base_time` from a grain's absolute MXL timestamp to
/// get its PTS; a writer adds the same to a buffer's PTS to get the MXL
/// timestamp it maps to a grain index. Sampling `D` once (and reusing it) is
/// what keeps two flows mapping the same frame to the same absolute index.
pub(crate) fn clock_offset(clock: Option<gst::Clock>, mxl_now: u64) -> Option<u64> {
    let clock = clock?;
    Some(mxl_now.saturating_sub(clock.time().nseconds()))
}

/// `GstContext` type shared by every MXL element (source and sink) in a pipeline
/// so they agree on a single [`SharedClockOffset`].
pub(crate) const CLOCK_OFFSET_CONTEXT: &str = "application/x-mxl-clock-offset";

/// The pipeline-wide `D`, sampled at most once (see [`clock_offset`]) and shared
/// between the MXL elements of one pipeline through [`CLOCK_OFFSET_CONTEXT`].
///
/// Sampling `D` per element would let a boundary-aligned frame round to a
/// different grain index on each flow (writers) or expose a different PTS for the
/// same index (readers), which would mispair flows in muxers like `st2038combiner`.
/// A single shared `D` maps a given PTS to the same index on every flow — even on a
/// foreign clock, where each element would otherwise sample a slightly different
/// offset.
#[derive(Clone, Debug, glib::Boxed)]
#[boxed_type(name = "GstMxlSharedClockOffset")]
pub(crate) struct SharedClockOffset(Arc<Mutex<Option<u64>>>);

impl Default for SharedClockOffset {
    fn default() -> Self {
        SharedClockOffset(Arc::new(Mutex::new(None)))
    }
}

impl SharedClockOffset {
    /// The shared `D`, sampling it once against `clock`/`mxl_now` if unset. The
    /// mutex serialises the first sample so concurrent elements reuse one value.
    /// `None` only when the element has no clock yet and nothing was sampled.
    pub(crate) fn get_or_sample(
        &self,
        clock: Option<gst::Clock>,
        mxl_now: u64,
    ) -> Result<Option<u64>, ClockOffsetError> {
        let mut slot = self.0.lock().map_err(|_| ClockOffsetError::Failed)?;
        if let Some(offset) = *slot {
            return Ok(Some(offset));
        }
        let Some(offset) = clock_offset(clock, mxl_now) else {
            return Ok(None);
        };
        *slot = Some(offset);
        Ok(Some(offset))
    }

    /// Drop the sampled value so the next [`get_or_sample`](Self::get_or_sample)
    /// re-samples, e.g. after the pipeline clock changes.
    pub(crate) fn invalidate(&self) -> Result<(), ClockOffsetError> {
        *self.0.lock().map_err(|_| ClockOffsetError::Failed)? = None;
        Ok(())
    }
}

/// The [`SharedClockOffset`] carried by `context`, if it is one of ours.
pub(crate) fn clock_offset_from_context(context: &gst::Context) -> Option<SharedClockOffset> {
    if context.context_type() != CLOCK_OFFSET_CONTEXT {
        return None;
    }
    context
        .structure()
        .get::<&SharedClockOffset>("offset")
        .ok()
        .cloned()
}

/// Implemented by `mxlsrc`/`mxlsink` so they share one [`SharedClockOffset`]
/// through [`CLOCK_OFFSET_CONTEXT`]. Only the small accessors are per-element;
/// the handshake and sampling are provided here so both sides behave identically.
pub(crate) trait ClockOffsetExt {
    /// The wrapped element (for its clock and for posting context messages).
    fn element(&self) -> gst::Element;
    /// Current MXL time, or `None` before the instance exists.
    fn mxl_now(&self) -> Option<u64>;
    /// The shared offset cell already adopted by this element, if any.
    fn cached_clock_offset(&self) -> Option<SharedClockOffset>;
    /// Adopt `cell` as this element's shared offset.
    fn store_clock_offset(&self, cell: SharedClockOffset);

    /// Return this pipeline's shared offset cell, adopting a sibling's via the
    /// context handshake or, failing that, creating and publishing our own.
    fn ensure_clock_offset(&self) -> Result<SharedClockOffset, ClockOffsetError> {
        if let Some(cell) = self.cached_clock_offset() {
            return Ok(cell);
        }
        let element = self.element();

        // A peer along our pads may already hold the context.
        for pad in element.pads() {
            let mut query = gst::query::Context::new(CLOCK_OFFSET_CONTEXT);
            if pad.peer_query(&mut query)
                && let Some(context) = query.context_owned()
                && let Some(cell) = clock_offset_from_context(&context)
            {
                self.store_clock_offset(cell.clone());
                return Ok(cell);
            }
        }

        // Ask the pipeline; a sibling may answer synchronously via set_context.
        let _ = element.post_message(
            gst::message::NeedContext::builder(CLOCK_OFFSET_CONTEXT)
                .src(&element)
                .build(),
        );
        if let Some(cell) = self.cached_clock_offset() {
            return Ok(cell);
        }

        // Nobody has one yet: create ours and share it with the pipeline.
        let cell = SharedClockOffset::default();
        self.store_clock_offset(cell.clone());
        let mut context = gst::Context::new(CLOCK_OFFSET_CONTEXT, true);
        let Some(s) = context.get_mut() else {
            return Err(ClockOffsetError::Failed);
        };
        s.structure_mut().set("offset", &cell);
        element.set_context(&context);
        let _ = element.post_message(
            gst::message::HaveContext::builder(context)
                .src(&element)
                .build(),
        );
        Ok(cell)
    }

    /// The pipeline's shared `D`, sampling it once if needed. `Ok(None)` before
    /// a clock or instance is available.
    fn resolve_clock_offset(&self) -> Result<Option<u64>, ClockOffsetError> {
        let Some(mxl_now) = self.mxl_now() else {
            return Ok(None);
        };
        let cell = self.ensure_clock_offset()?;
        cell.get_or_sample(self.element().clock(), mxl_now)
    }

    /// Drop the shared sample so it is re-taken against the current clock.
    fn invalidate_clock_offset(&self) -> Result<(), ClockOffsetError> {
        if let Some(cell) = self.cached_clock_offset() {
            cell.invalidate()?;
        }
        Ok(())
    }
}
