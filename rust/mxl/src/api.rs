// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{path::Path, sync::Arc};

use dlopen2::wrapper::{Container, WrapperApi};

use crate::Result;

#[derive(WrapperApi)]
pub struct MxlApi {
    #[dlopen2_name = "mxlGetVersion"]
    get_version:
        unsafe extern "C" fn(out_version: *mut mxl_sys::mxlVersionType) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlCreateInstance"]
    create_instance: unsafe extern "C" fn(
        in_mxl_domain: *const std::os::raw::c_char,
        in_options: *const std::os::raw::c_char,
    ) -> mxl_sys::mxlInstance,

    #[dlopen2_name = "mxlGarbageCollectFlows"]
    garbage_collect_flows:
        unsafe extern "C" fn(in_instance: mxl_sys::mxlInstance) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlDestroyInstance"]
    destroy_instance: unsafe extern "C" fn(in_instance: mxl_sys::mxlInstance) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlCreateFlow"]
    create_flow: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flow_def: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        info: *mut mxl_sys::mxlFlowInfo,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlDestroyFlow"]
    destroy_flow: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flow_id: *const std::os::raw::c_char,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlGetFlowDef"]
    get_flow_def: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flow_id: *const ::std::os::raw::c_char,
        buffer: *mut ::std::os::raw::c_char,
        buffer_size: *mut usize,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlCreateFlowReader"]
    create_flow_reader: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flow_id: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        reader: *mut mxl_sys::mxlFlowReader,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlReleaseFlowReader"]
    release_flow_reader: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        reader: mxl_sys::mxlFlowReader,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlCreateFlowWriter"]
    create_flow_writer: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flow_id: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        writer: *mut mxl_sys::mxlFlowWriter,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlReleaseFlowWriter"]
    release_flow_writer: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        writer: mxl_sys::mxlFlowWriter,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowReaderGetInfo"]
    flow_reader_get_info: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        info: *mut mxl_sys::mxlFlowInfo,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowReaderGetGrain"]
    flow_reader_get_grain: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        timeout_ns: u64,
        grain: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowReaderGetGrainNonBlocking"]
    flow_reader_get_grain_non_blocking: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        grain: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterOpenGrain"]
    flow_writer_open_grain: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        index: u64,
        grain_info: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterCancelGrain"]
    flow_writer_cancel_grain:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterCommitGrain"]
    flow_writer_commit_grain: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        grain: *const mxl_sys::mxlGrainInfo,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowReaderGetSamples"]
    flow_reader_get_samples: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        count: usize,
        payload_buffers_slices: *mut mxl_sys::mxlWrappedMultiBufferSlice,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterOpenSamples"]
    flow_writer_open_samples: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        index: u64,
        count: usize,
        payload_buffers_slices: *mut mxl_sys::mxlMutableWrappedMultiBufferSlice,
    ) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterCancelSamples"]
    flow_writer_cancel_samples:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlFlowWriterCommitSamples"]
    flow_writer_commit_samples:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,

    #[dlopen2_name = "mxlGetCurrentIndex"]
    get_current_index: unsafe extern "C" fn(edit_rate: *const mxl_sys::mxlRational) -> u64,

    #[dlopen2_name = "mxlGetNsUntilIndex"]
    get_ns_until_index:
        unsafe extern "C" fn(index: u64, edit_rate: *const mxl_sys::mxlRational) -> u64,

    #[dlopen2_name = "mxlTimestampToIndex"]
    timestamp_to_index:
        unsafe extern "C" fn(edit_rate: *const mxl_sys::mxlRational, timestamp: u64) -> u64,

    #[dlopen2_name = "mxlIndexToTimestamp"]
    index_to_timestamp:
        unsafe extern "C" fn(edit_rate: *const mxl_sys::mxlRational, index: u64) -> u64,

    #[dlopen2_name = "mxlSleepForNs"]
    sleep_for_ns: unsafe extern "C" fn(ns: u64),

    #[dlopen2_name = "mxlGetTime"]
    get_time: unsafe extern "C" fn() -> u64,
}

pub type MxlApiHandle = Arc<Container<MxlApi>>;

pub fn load_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlApiHandle> {
    Ok(Arc::new(unsafe {
        Container::load(path_to_so_file.as_ref().as_os_str())
    }?))
}
