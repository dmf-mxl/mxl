// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Tests of the grain storage API: layouts, mapped slots, and grains addressed by slot.
mod common;

use std::time::Duration;

use common::{read_flow_def, setup_test};
use mxl::{Error, GRAIN_VALID_SLICES_ALL, PAYLOAD_STORAGE_HOST_POINTER};

#[test]
fn host_flow_slots_hold_the_grains() {
    let (mxl_instance, _domain_guard) = setup_test("host_slots");
    let (flow_writer, flow_config_info, _) = mxl_instance
        .create_flow_writer(
            read_flow_def("lib/tests/data/v210_flow.json").as_str(),
            None,
        )
        .unwrap();
    let discrete = *flow_config_info.discrete().unwrap();
    let flow_id = flow_config_info.common().id().to_string();
    let grain_writer = flow_writer.to_grain_writer().unwrap();
    let grain_reader = mxl_instance
        .create_flow_reader(flow_id.as_str())
        .unwrap()
        .to_grain_reader()
        .unwrap();

    let layout = grain_writer.storage_layout().unwrap();
    assert!(layout.is_host_storage());
    assert_eq!(layout.storage_type(), PAYLOAD_STORAGE_HOST_POINTER);
    assert_eq!(layout.slot_count(), discrete.grainCount);
    assert_eq!(layout.planes().len(), 1);
    assert_eq!(layout.planes()[0].pitch, u64::from(discrete.sliceSizes[0]));
    let reader_layout = grain_reader.storage_layout().unwrap();
    assert_eq!(reader_layout.slot_count(), layout.slot_count());
    assert_eq!(reader_layout.planes()[0].pitch, layout.planes()[0].pitch);

    let writer_slots = grain_writer.map_slots().unwrap();
    let reader_slots = grain_reader.map_slots().unwrap();
    assert_eq!(writer_slots.len(), layout.slot_count() as usize);
    assert_eq!(reader_slots.len(), layout.slot_count() as usize);
    for (slot, storage) in reader_slots.slots().iter().enumerate() {
        assert_eq!(storage.slot(), slot as u32);
        assert!(storage.host_pointer(reader_slots.layout()).is_some());
    }

    // A grain written through the writer's slot is read through the reader's slot and through the
    // payload pointer.
    let rate = flow_config_info.common().grain_rate().unwrap();
    let index = mxl_instance.get_current_index(&rate);
    let access = grain_writer.open_grain_slot(index).unwrap();
    let slot = access.slot();
    assert_eq!(u64::from(slot), index % u64::from(layout.slot_count()));
    assert_eq!(access.index(), index);
    let plane_offset = layout.planes()[0].offset as usize;
    let written = writer_slots
        .get(slot)
        .unwrap()
        .host_pointer(writer_slots.layout())
        .unwrap();
    // SAFETY: the slot holds a complete grain from the offset of its plane.
    unsafe {
        *written.add(plane_offset) = 0x5A;
        *written.add(plane_offset + layout.planes()[0].pitch as usize) = 0xA5;
    }
    let total_slices = access.total_slices();
    access.commit(total_slices).unwrap();

    let grain_slot = grain_reader
        .get_grain_slot(index, GRAIN_VALID_SLICES_ALL, Duration::from_secs(5))
        .unwrap();
    assert_eq!(grain_slot.slot, slot);
    assert_eq!(grain_slot.index, index);
    assert_eq!(grain_slot.valid_slices, grain_slot.total_slices);
    let read = reader_slots
        .get(slot)
        .unwrap()
        .host_pointer(reader_slots.layout())
        .unwrap();
    // SAFETY: as above, in the reader's mapping of the same slot.
    let (first, second) = unsafe {
        (
            *read.add(plane_offset),
            *read.add(plane_offset + layout.planes()[0].pitch as usize),
        )
    };
    assert_eq!((first, second), (0x5A, 0xA5));
    let grain_data = grain_reader
        .get_complete_grain(index, Duration::from_secs(5))
        .unwrap();
    assert_eq!(grain_data.payload[0], 0x5A);

    // A grain that is not written yet is not available, and a dropped write is cancelled.
    assert!(matches!(
        grain_reader.get_grain_slot(index + 1, GRAIN_VALID_SLICES_ALL, Duration::ZERO),
        Err(Error::OutOfRangeTooEarly)
    ));
    drop(grain_writer.open_grain_slot(index + 1).unwrap());
    assert!(matches!(
        grain_reader.get_grain_slot(index + 1, 1, Duration::ZERO),
        Err(Error::OutOfRangeTooEarly)
    ));
}
