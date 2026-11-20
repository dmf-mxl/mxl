// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Payload in CUDA device memory: the storage type `cuda-device-pointer` of the cuda-linear
//! backend, which `mxl/storage/cuda_device_pointer.h` defines, installed with the backend. libmxl
//! passes the reserved bytes of the layout and of the slot description through without
//! interpreting them. The traits of this crate read them as that header defines them:
//!
//! ```ignore
//! use mxl_cuda::{CudaGrainStorageExt, CudaGrainStorageLayoutExt};
//! let device = slots.layout().cuda_device();
//! let pointer = slots.slots()[0].cuda_device_pointer(slots.layout());
//! ```

use mxl::{GrainStorage, GrainStorageLayout};

#[allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code
)]
mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

/// The storage type of payload in CUDA device memory, where each slot is described by a device
/// pointer.
pub const PAYLOAD_STORAGE_CUDA_DEVICE_POINTER: &str = "cuda-device-pointer";

/// The device that holds a payload in CUDA device memory: `deviceIndex`, its index in this
/// process, which differs between processes, and `deviceUuid`, which identifies it across them.
pub type CudaGrainStorageLayout = bindings::mxlCudaGrainStorageLayout;

/// Reads the device that holds a payload in CUDA device memory from a layout.
pub trait CudaGrainStorageLayoutExt {
    /// The device that holds the payload, if the storage type says that it is in CUDA device
    /// memory.
    fn cuda_device(&self) -> Option<CudaGrainStorageLayout>;
}

impl CudaGrainStorageLayoutExt for GrainStorageLayout {
    fn cuda_device(&self) -> Option<CudaGrainStorageLayout> {
        (self.storage_type() == PAYLOAD_STORAGE_CUDA_DEVICE_POINTER).then(|| {
            // SAFETY: the reserved bytes are as large as the structure, which is plain integers.
            unsafe { std::ptr::read_unaligned(self.reserved().as_ptr().cast()) }
        })
    }
}

/// Reads the device address of a slot in CUDA device memory from its description.
pub trait CudaGrainStorageExt {
    /// The CUDA device address of the first byte of the slot, valid in this process on the device
    /// of the layout, if `layout`, the layout of its flow, says that the payload is in CUDA device
    /// memory. The planes are at the offsets of the layout. Device work that writes a grain must be
    /// complete before the grain is committed.
    fn cuda_device_pointer(&self, layout: &GrainStorageLayout) -> Option<*mut std::ffi::c_void>;
}

impl CudaGrainStorageExt for GrainStorage {
    fn cuda_device_pointer(&self, layout: &GrainStorageLayout) -> Option<*mut std::ffi::c_void> {
        (layout.storage_type() == PAYLOAD_STORAGE_CUDA_DEVICE_POINTER).then(|| {
            // SAFETY: the reserved bytes are larger than the structure, which is one pointer.
            unsafe {
                std::ptr::read_unaligned::<bindings::mxlCudaGrainStorage>(
                    self.reserved().as_ptr().cast(),
                )
                .pointer
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use mxl::PAYLOAD_STORAGE_HOST_POINTER;

    #[test]
    fn cuda_device_pointer_name_matches_the_c_header() {
        assert_eq!(
            Some(PAYLOAD_STORAGE_CUDA_DEVICE_POINTER.as_bytes()),
            bindings::MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER.strip_suffix(b"\0")
        );
    }

    /// A layout whose storage type is `name`.
    fn layout_of_type(name: &str) -> GrainStorageLayout {
        let mut raw = mxl_sys::GrainStorageLayout::default();
        for (target, byte) in raw.storageType.iter_mut().zip(name.as_bytes()) {
            *target = *byte as std::ffi::c_char;
        }
        GrainStorageLayout::from_raw(raw)
    }

    #[test]
    fn the_cuda_device_follows_the_storage_type() {
        let mut cuda = layout_of_type(PAYLOAD_STORAGE_CUDA_DEVICE_POINTER);
        let device = CudaGrainStorageLayout {
            deviceIndex: 1,
            deviceUuid: [9; 16],
            ..Default::default()
        };
        // SAFETY: the reserved bytes are as large as the structure.
        unsafe { std::ptr::write_unaligned(cuda.reserved_mut().as_mut_ptr().cast(), device) };
        let read = cuda.cuda_device().unwrap();
        assert_eq!((read.deviceIndex, read.deviceUuid), (1, [9; 16]));
        // The device is where the C header puts it.
        assert_eq!(&cuda.reserved()[..4], &[1, 0, 0, 0]);
        assert_eq!(cuda.reserved()[8], 9);
        assert!(
            layout_of_type(PAYLOAD_STORAGE_HOST_POINTER)
                .cuda_device()
                .is_none()
        );
    }

    #[test]
    fn slot_pointers_follow_the_storage_type() {
        let cuda = layout_of_type(PAYLOAD_STORAGE_CUDA_DEVICE_POINTER);
        let mut storage = GrainStorage::new(3);
        let address = 0x7f00_0000_1000usize as *mut std::ffi::c_void;
        storage.reserved_mut()[..8].copy_from_slice(&(address as usize).to_ne_bytes());
        assert_eq!(storage.slot(), 3);
        assert_eq!(storage.cuda_device_pointer(&cuda), Some(address));
        assert_eq!(storage.host_pointer(&cuda), None);

        let host = layout_of_type(PAYLOAD_STORAGE_HOST_POINTER);
        assert!(storage.host_pointer(&host).is_some());
        assert_eq!(storage.cuda_device_pointer(&host), None);
    }
}
