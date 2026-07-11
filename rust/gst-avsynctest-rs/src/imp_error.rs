// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Shared error helpers for the avsynctest source element implementations.

use gstreamer as gst;

// Constants are only for messages shared verbatim by both source elements.
pub const LOCK_SETTINGS: &str = "Failed to lock settings";
pub const LOCK_STATE: &str = "Failed to lock state";
pub const LOCK_CLOCK_WAIT: &str = "Failed to lock clock wait";
pub const CALC_LATENCY: &str = "Failed to calculate latency";
pub const CAPS_NO_STRUCTURE: &str = "Caps had no structure to fixate";
pub const GET_TIME_SEGMENT: &str = "Failed to get time segment";
pub const BUFFER_NOT_WRITABLE: &str = "Newly allocated buffer was not writable";

pub fn failed_msg(msg: &str) -> gst::ErrorMessage {
    gst::error_msg!(gst::CoreError::Failed, ["{}", msg])
}

/// Thin wrapper around [`gst::element_imp_error!`] for internal failures
/// ([`CoreError::Failed`](gst::CoreError::Failed)).
#[macro_export]
macro_rules! imp_failed {
    ($imp:expr, $msg:expr) => {
        gst::element_imp_error!($imp, gst::CoreError::Failed, ["{}", $msg])
    };
}
