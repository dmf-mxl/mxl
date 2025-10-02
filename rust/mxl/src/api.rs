// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::path::Path;

use dlopen2::wrapper::{Container, WrapperApi};

use crate::Result;

#[derive(WrapperApi)]
pub struct MxlApi {
    #[dlopen2_name = "mxlGetVersion"]
    mxl_get_version:
        unsafe extern "C" fn(out_version: *mut mxl_sys::mxlVersionType) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlCreateInstance"]
    mxl_create_instance: unsafe extern "C" fn(
        in_mxlDomain: *const std::os::raw::c_char,
        in_options: *const std::os::raw::c_char,
    ) -> mxl_sys::mxlInstance,
    #[dlopen2_name = "mxlGarbageCollectFlows"]
    mxl_garbage_collect_flows:
        unsafe extern "C" fn(in_instance: mxl_sys::mxlInstance) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlDestroyInstance"]
    mxl_destroy_instance:
        unsafe extern "C" fn(in_instance: mxl_sys::mxlInstance) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlCreateFlow"]
    mxl_create_flow: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flowDef: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        info: *mut mxl_sys::mxlFlowInfo,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlDestroyFlow"]
    mxl_destroy_flow: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flowId: *const std::os::raw::c_char,
    ) -> mxl_sys::mxlStatus,

    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlGetFlowDef"]
    mxl_get_flow_def: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flowId: *const ::std::os::raw::c_char,
        buffer: *mut ::std::os::raw::c_char,
        bufferSize: *mut usize,
    ) -> mxl_sys::mxlStatus,

    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlCreateFlowReader"]
    mxl_create_flow_reader: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flowId: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        reader: *mut mxl_sys::mxlFlowReader,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlReleaseFlowReader"]
    mxl_release_flow_reader: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        reader: mxl_sys::mxlFlowReader,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlCreateFlowWriter"]
    mxl_create_flow_writer: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        flowId: *const std::os::raw::c_char,
        options: *const std::os::raw::c_char,
        writer: *mut mxl_sys::mxlFlowWriter,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlReleaseFlowWriter"]
    mxl_release_flow_writer: unsafe extern "C" fn(
        instance: mxl_sys::mxlInstance,
        writer: mxl_sys::mxlFlowWriter,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowReaderGetInfo"]
    mxl_flow_reader_get_info: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        info: *mut mxl_sys::mxlFlowInfo,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlFlowReaderGetGrain"]
    mxl_flow_reader_get_grain: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        timeoutNs: u64,
        grain: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowReaderGetGrainNonBlocking"]
    mxl_flow_reader_get_grain_non_blocking: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        grain: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlFlowWriterOpenGrain"]
    mxl_flow_writer_open_grain: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        index: u64,
        grainInfo: *mut mxl_sys::mxlGrainInfo,
        payload: *mut *mut u8,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowWriterCancelGrain"]
    mxl_flow_writer_cancel_grain:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowWriterCommitGrain"]
    mxl_flow_writer_commit_grain: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        grain: *const mxl_sys::mxlGrainInfo,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlFlowReaderGetSamples"]
    mxl_flow_reader_get_samples: unsafe extern "C" fn(
        reader: mxl_sys::mxlFlowReader,
        index: u64,
        count: usize,
        payloadBuffersSlices: *mut mxl_sys::mxlWrappedMultiBufferSlice,
    ) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlFlowWriterOpenSamples"]
    mxl_flow_writer_open_samples: unsafe extern "C" fn(
        writer: mxl_sys::mxlFlowWriter,
        index: u64,
        count: usize,
        payloadBuffersSlices: *mut mxl_sys::mxlMutableWrappedMultiBufferSlice,
    ) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowWriterCancelSamples"]
    mxl_flow_writer_cancel_samples:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,
    #[dlopen2_name = "mxlFlowWriterCommitSamples"]
    mxl_flow_writer_commit_samples:
        unsafe extern "C" fn(writer: mxl_sys::mxlFlowWriter) -> mxl_sys::mxlStatus,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlGetCurrentIndex"]
    mxl_get_current_index: unsafe extern "C" fn(editRate: *const mxl_sys::mxlRational) -> u64,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlGetNsUntilIndex"]
    mxl_get_ns_until_index:
        unsafe extern "C" fn(index: u64, editRate: *const mxl_sys::mxlRational) -> u64,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlTimestampToIndex"]
    mxl_timestamp_to_index:
        unsafe extern "C" fn(editRate: *const mxl_sys::mxlRational, timestamp: u64) -> u64,
    #[allow(non_snake_case)]
    #[dlopen2_name = "mxlIndexToTimestamp"]
    mxl_index_to_timestamp:
        unsafe extern "C" fn(editRate: *const mxl_sys::mxlRational, index: u64) -> u64,
    #[dlopen2_name = "mxlSleepForNs"]
    mxl_sleep_for_ns: unsafe extern "C" fn(ns: u64),
    #[dlopen2_name = "mxlGetTime"]
    mxl_get_time: unsafe extern "C" fn() -> u64,
}

pub fn load_api(path_to_so_file: impl AsRef<Path>) -> Result<Container<MxlApi>> {
    Ok(unsafe { Container::load(path_to_so_file.as_ref().as_os_str()) }?)
}
