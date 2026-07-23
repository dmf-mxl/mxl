// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! MXL index ↔ GStreamer PTS mapping for `mxlsrc` discrete (video, data) flows.
//!
//! `mxlsink` commits each grain at an **absolute** MXL grain index derived from
//! its PTS via the MXL media clock (`timestamp_to_index`). The same source
//! frame therefore lands at the *same* absolute index on every flow it is
//! written to (e.g. the video flow and its companion ancillary data flow).
//!
//! Readers exploit this: PTS is computed purely from the absolute index of the
//! grain actually read (`pts_for_index`), minus the MXL-time →
//! running-time subtrahend `pts_subtrahend` (the pipeline-shared offset `D`
//! plus the live pipeline `base_time`). Because every MXL element in the pipeline
//! shares one `D` (see `crate::clock::SharedClockOffset`), two `mxlsrc`
//! instances reading the same absolute grain expose identical PTS regardless of
//! when each attached or how far either fell behind — which is what
//! `st2038combiner` needs to re-pair video with its ancillary data, for example
//! — while the PTS still lands in the pipeline's running-time base so live
//! elements can synchronise the flows against the pipeline clock.

use gstreamer as gst;
use mxl::{GrainReader, MxlInstance, Rational};

/// Oldest absolute grain index still retained in a ring of `grain_count` grains
/// whose newest committed grain is `head`.
pub(crate) fn oldest_retained_index(head: u64, grain_count: u32) -> u64 {
    head.saturating_sub((grain_count as u64).saturating_sub(1))
}

/// What a discrete reader should do for its next grain.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum ReadStep {
    /// The producer has not committed `index` yet (or has committed nothing);
    /// the caller should wait and retry.
    WaitForProducer,
    /// Read absolute grain `index`. `discont` is set when the reader had to skip
    /// forward because it fell behind the ring.
    Read { index: u64, discont: bool },
}

/// Resolve the next grain to read on a discrete flow.
///
/// `index` is the reader's next desired absolute grain index, `head` the flow's
/// newest committed grain (`0` before the first commit), `grain_count` the ring
/// capacity. The returned index is always the absolute index of the grain to
/// read, so PTS stays tied to the payload across readers and across catch-ups.
pub(crate) fn resolve_read_step(index: u64, head: u64, grain_count: u32) -> ReadStep {
    if head == 0 || index > head {
        return ReadStep::WaitForProducer;
    }
    let oldest = oldest_retained_index(head, grain_count);
    if index < oldest {
        // The writer lapped us; resume at the oldest grain still in the ring.
        ReadStep::Read {
            index: oldest,
            discont: true,
        }
    } else {
        ReadStep::Read {
            index,
            discont: false,
        }
    }
}

/// Per-flow newest committed grain index (`0` before the first commit).
pub(crate) fn flow_head_index(reader: &GrainReader) -> Result<u64, gst::FlowError> {
    reader
        .get_runtime_info()
        .map(|r| r.headIndex)
        .map_err(|_| gst::FlowError::Error)
}

/// Ring capacity of a discrete flow (video or data).
pub(crate) fn discrete_grain_count(reader: &GrainReader) -> Result<u32, gst::FlowError> {
    reader
        .get_config_info()
        .map_err(|_| gst::FlowError::Error)?
        .discrete()
        .map_err(|_| gst::FlowError::Error)
        .map(|d| d.grainCount)
}

/// GStreamer PTS for `read_index`, in the pipeline's running-time base, or
/// `None` when the grain predates running-time 0 (its timestamp is before
/// `offset`, so its PTS would be negative).
///
/// `offset` is the MXL-time → running-time subtrahend `pts_subtrahend`
/// (the pipeline-shared `D` plus the pipeline `base_time`). Subtracting it from
/// the grain's absolute MXL timestamp keeps the PTS identical for every
/// reader in the pipeline (all share one `D`) while landing it in the
/// running-time base the pipeline clock uses.
///
/// A grain committed before `base_time` maps to a negative running time. A live
/// source must not emit it: clamping such a PTS to 0 would shorten the first
/// inter-frame interval and let a muxer (e.g. `st2038combiner`) mis-window the
/// next frame's data onto it. `None` signals the caller to skip the grain
/// instead; because every reader shares one `offset`, they skip the same grains
/// and stay index-aligned.
pub(crate) fn pts_for_index(
    instance: &MxlInstance,
    read_index: u64,
    rate: &Rational,
    offset: u64,
) -> Result<Option<gst::ClockTime>, gst::FlowError> {
    let read_ts = instance
        .index_to_timestamp(read_index, rate)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(read_ts
        .checked_sub(offset)
        .map(gst::ClockTime::from_nseconds))
}

/// One grain/sample period at `rate`.
pub(crate) fn index_period(rate: &Rational) -> gst::ClockTime {
    let period_ns = (1_000_000_000u128 * rate.denominator as u128 / rate.numerator as u128) as u64;
    gst::ClockTime::from_nseconds(period_ns)
}

#[cfg(test)]
mod tests {
    use super::{ReadStep, oldest_retained_index, resolve_read_step};

    const GRAIN_COUNT: u32 = 300;

    #[test]
    fn oldest_retained_when_head_below_capacity() {
        // Fewer grains committed than the ring holds: nothing has been evicted.
        assert_eq!(oldest_retained_index(50, GRAIN_COUNT), 0);
    }

    #[test]
    fn oldest_retained_when_ring_full() {
        assert_eq!(oldest_retained_index(400, GRAIN_COUNT), 101);
    }

    #[test]
    fn waits_before_first_commit() {
        // head == 0 means the producer has not committed anything yet.
        assert_eq!(
            resolve_read_step(0, 0, GRAIN_COUNT),
            ReadStep::WaitForProducer
        );
    }

    #[test]
    fn reads_grain_at_head() {
        let head = 53_429_297_473;
        assert_eq!(
            resolve_read_step(head, head, GRAIN_COUNT),
            ReadStep::Read {
                index: head,
                discont: false,
            }
        );
    }

    #[test]
    fn reads_sequentially_within_ring() {
        let head = 53_429_297_500;
        assert_eq!(
            resolve_read_step(53_429_297_473, head, GRAIN_COUNT),
            ReadStep::Read {
                index: 53_429_297_473,
                discont: false,
            }
        );
    }

    #[test]
    fn waits_when_ahead_of_head() {
        let head = 53_429_297_473;
        assert_eq!(
            resolve_read_step(head + 1, head, GRAIN_COUNT),
            ReadStep::WaitForProducer
        );
    }

    #[test]
    fn catches_up_with_discont_when_lapped() {
        let head = 53_429_298_000;
        let oldest = oldest_retained_index(head, GRAIN_COUNT);
        // Reader fell far behind the ring.
        assert_eq!(
            resolve_read_step(head - 5_000, head, GRAIN_COUNT),
            ReadStep::Read {
                index: oldest,
                discont: true,
            }
        );
    }

    #[test]
    fn oldest_retained_grain_is_read_without_discont() {
        let head = 53_429_298_000;
        let oldest = oldest_retained_index(head, GRAIN_COUNT);
        assert_eq!(
            resolve_read_step(oldest, head, GRAIN_COUNT),
            ReadStep::Read {
                index: oldest,
                discont: false,
            }
        );
    }

    /// The same absolute grain resolves identically no matter which reader asks,
    /// so two readers that reach it expose the same PTS (combiner alignment).
    #[test]
    fn same_grain_resolves_identically_across_readers() {
        let head = 53_429_298_000;
        let grain = 53_429_297_900;
        assert_eq!(
            resolve_read_step(grain, head, GRAIN_COUNT),
            resolve_read_step(grain, head, GRAIN_COUNT)
        );
    }
}
