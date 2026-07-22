// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#![cfg(feature = "mxl-fabrics-ofi")]

mod common;

use std::time::{Duration, Instant};

use common::{load_fabrics_test_api, read_flow_def, setup_test};
use mxl::{
    Error, FlowReader, FlowWriter, GrainReader, GrainWriter, OwnedGrainData, OwnedSamplesData,
    SamplesReader, SamplesWriter,
    fabrics::{
        EndpointAddress, InterfaceConfig, Provider,
        initiator::{self, Initiator},
        target::{self, Target},
    },
};

const POLL_TIMEOUT: Duration = Duration::from_secs(5);
const BLOCKING_WAIT: Duration = Duration::from_millis(20);
const AUDIO_SAMPLE_COUNT: usize = 42;

fn tcp_endpoint() -> EndpointAddress<'static> {
    EndpointAddress {
        node: Some("127.0.0.1"),
        service: Some("0"),
    }
}

fn tcp_interface(provider: &Provider) -> InterfaceConfig<'static> {
    InterfaceConfig::builder(tcp_endpoint())
        .provider(provider.prov_type().clone())
        .build()
}

fn poll_until_success<F>(mut step: F, timeout_message: &str)
where
    F: FnMut() -> bool,
{
    let deadline = Instant::now() + POLL_TIMEOUT;
    while Instant::now() < deadline {
        if step() {
            return;
        }
    }

    panic!("{timeout_message}");
}

fn wait_for_grain_connection(
    target: &Target<target::states::Grain>,
    initiator: &Initiator<initiator::states::Grain>,
) {
    poll_until_success(
        || {
            match target.read_non_blocking() {
                Ok(_) | Err(Error::NotReady) => {}
                Err(error) => {
                    panic!("unexpected target status while waiting for connection: {error}")
                }
            }

            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) => true,
                Err(Error::NotReady) => false,
                Err(error) => panic!(
                    "unexpected initiator status while waiting for grain connection: {error}"
                ),
            }
        },
        "failed to connect grain initiator and target in 5 seconds",
    );
}

fn wait_for_samples_connection(
    target: &Target<target::states::Sample>,
    initiator: &Initiator<initiator::states::Samples>,
) {
    poll_until_success(
        || {
            match target.read_non_blocking() {
                Ok(_) | Err(Error::NotReady) => {}
                Err(error) => {
                    panic!("unexpected target status while waiting for sample connection: {error}")
                }
            }

            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) => true,
                Err(Error::NotReady) => false,
                Err(error) => panic!(
                    "unexpected initiator status while waiting for sample connection: {error}"
                ),
            }
        },
        "failed to connect sample initiator and target in 5 seconds",
    );
}

fn wait_for_grain_transfer_start(
    target: &Target<target::states::Grain>,
    initiator: &Initiator<initiator::states::Grain>,
    grain_index: u64,
    end_slice: u16,
) {
    poll_until_success(
        || {
            match target.read_non_blocking() {
                Ok(_) | Err(Error::NotReady) => {}
                Err(error) => panic!("unexpected target status before grain transfer: {error}"),
            }

            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) | Err(Error::NotReady) => {}
                Err(error) => panic!("unexpected initiator status before grain transfer: {error}"),
            }

            match initiator.transfer(grain_index, 0, end_slice) {
                Ok(()) => true,
                Err(Error::NotReady) => false,
                Err(error) => panic!("failed to start grain transfer: {error}"),
            }
        },
        "failed to start grain transfer in 5 seconds",
    );
}

fn wait_for_samples_transfer_start(
    target: &Target<target::states::Sample>,
    initiator: &Initiator<initiator::states::Samples>,
    head_index: u64,
    count: usize,
) {
    poll_until_success(
        || {
            match target.read_non_blocking() {
                Ok(_) | Err(Error::NotReady) => {}
                Err(error) => panic!("unexpected target status before sample transfer: {error}"),
            }

            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) | Err(Error::NotReady) => {}
                Err(error) => panic!("unexpected initiator status before sample transfer: {error}"),
            }

            match initiator.transfer(head_index, count) {
                Ok(()) => true,
                Err(Error::NotReady) => false,
                Err(error) => panic!("failed to start samples transfer: {error}"),
            }
        },
        "failed to start samples transfer in 5 seconds",
    );
}

fn wait_for_grain_transfer_completion(
    target: &Target<target::states::Grain>,
    initiator: &Initiator<initiator::states::Grain>,
    expected_grain_index: u64,
) -> u64 {
    let mut completed_index = None;
    poll_until_success(
        || {
            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) | Err(Error::NotReady) => {}
                Err(error) => {
                    panic!("unexpected initiator status while completing grain transfer: {error}")
                }
            }

            match target.read(BLOCKING_WAIT) {
                Ok(result) => {
                    assert_eq!(result.grain_index, expected_grain_index);
                    completed_index = Some(result.grain_index);
                    true
                }
                Err(Error::NotReady) => false,
                Err(Error::Interrupted) => {
                    panic!("grain target disconnected before transfer completed")
                }
                Err(error) => panic!("unexpected grain completion status: {error}"),
            }
        },
        "grain transfer did not complete in 5 seconds",
    );
    completed_index.unwrap()
}

fn wait_for_samples_transfer_completion(
    target: &Target<target::states::Sample>,
    initiator: &Initiator<initiator::states::Samples>,
    expected_head_index: u64,
    expected_count: usize,
) -> (u64, usize) {
    let mut completed = None;
    poll_until_success(
        || {
            match initiator.make_progress(BLOCKING_WAIT) {
                Ok(()) | Err(Error::NotReady) => {}
                Err(error) => {
                    panic!("unexpected initiator status while completing samples transfer: {error}")
                }
            }

            match target.read(BLOCKING_WAIT) {
                Ok(result) => {
                    assert_eq!(result.head_index, expected_head_index);
                    assert_eq!(result.count, expected_count);
                    completed = Some((result.head_index, result.count));
                    true
                }
                Err(Error::NotReady) => false,
                Err(Error::Interrupted) => {
                    panic!("samples target disconnected before transfer completed")
                }
                Err(error) => panic!("unexpected samples completion status: {error}"),
            }
        },
        "samples transfer did not complete in 5 seconds",
    );
    completed.unwrap()
}

fn wait_for_target_grain(reader: &GrainReader, grain_index: u64) -> OwnedGrainData {
    let mut result = None;
    poll_until_success(
        || match reader.get_grain_non_blocking(grain_index) {
            Ok(grain) => {
                result = Some(grain.into());
                true
            }
            Err(Error::OutOfRangeTooEarly) | Err(Error::NotReady) => false,
            Err(error) => panic!("unexpected target grain read status: {error}"),
        },
        "target grain did not become visible in 5 seconds",
    );
    result.unwrap()
}

fn wait_for_target_samples(
    reader: &SamplesReader,
    head_index: u64,
    count: usize,
) -> OwnedSamplesData {
    let mut result = None;
    poll_until_success(
        || match reader.get_samples_non_blocking(head_index, count) {
            Ok(samples) => {
                result = Some(samples.into());
                true
            }
            Err(Error::OutOfRangeTooEarly) | Err(Error::NotReady) => false,
            Err(error) => panic!("unexpected target samples read status: {error}"),
        },
        "target samples did not become visible in 5 seconds",
    );
    result.unwrap()
}

fn create_video_flow(
    mxl_instance: &mxl::MxlInstance,
) -> (FlowWriter, FlowReader, mxl::FlowConfigInfo) {
    create_flow_with_def(mxl_instance, read_flow_def("lib/tests/data/v210_flow.json"))
}

fn create_video_flow_with_unique_id(
    mxl_instance: &mxl::MxlInstance,
) -> (FlowWriter, FlowReader, mxl::FlowConfigInfo) {
    create_flow_with_def(
        mxl_instance,
        flow_def_with_fresh_id(&read_flow_def("lib/tests/data/v210_flow.json")),
    )
}

fn create_audio_flow(
    mxl_instance: &mxl::MxlInstance,
) -> (FlowWriter, FlowReader, mxl::FlowConfigInfo) {
    create_flow_with_def(
        mxl_instance,
        read_flow_def("lib/tests/data/audio_flow.json"),
    )
}

fn create_audio_flow_with_unique_id(
    mxl_instance: &mxl::MxlInstance,
) -> (FlowWriter, FlowReader, mxl::FlowConfigInfo) {
    create_flow_with_def(
        mxl_instance,
        flow_def_with_fresh_id(&read_flow_def("lib/tests/data/audio_flow.json")),
    )
}

fn flow_def_with_fresh_id(flow_def: &str) -> String {
    let mut value: serde_json::Value = serde_json::from_str(flow_def).unwrap();
    value["id"] = serde_json::Value::String(uuid::Uuid::new_v4().to_string());
    serde_json::to_string(&value).unwrap()
}

fn create_flow_with_def(
    mxl_instance: &mxl::MxlInstance,
    flow_def: String,
) -> (FlowWriter, FlowReader, mxl::FlowConfigInfo) {
    let (flow_writer, flow_config, was_created) = mxl_instance
        .create_flow_writer(flow_def.as_str(), None)
        .unwrap();
    assert!(was_created);

    let flow_id = flow_config.common().id().to_string();
    let flow_reader = mxl_instance.create_flow_reader(flow_id.as_str()).unwrap();

    (flow_writer, flow_reader, flow_config)
}

fn fill_grain_payload(payload: &mut [u8]) {
    for (index, byte) in payload.iter_mut().enumerate() {
        *byte = (index % 251) as u8;
    }
}

fn fill_samples_payload(writer: &mut mxl::SamplesWriteAccess<'_>) {
    for channel in 0..writer.channels() {
        let (first, second) = writer.channel_data_mut(channel).unwrap();
        for (index, byte) in first.iter_mut().enumerate() {
            *byte = (channel as u8).wrapping_mul(17).wrapping_add(index as u8);
        }
        for (index, byte) in second.iter_mut().enumerate() {
            *byte = (channel as u8)
                .wrapping_mul(29)
                .wrapping_add(index as u8)
                .wrapping_add(3);
        }
    }
}

#[test]
fn provider_tcp_roundtrip() {
    let (mxl_instance, _domain_guard) = setup_test("provider_tcp_roundtrip");

    {
        let fabrics_api = load_fabrics_test_api();
        let fabrics_instance = mxl_instance.create_fabrics_instance(&fabrics_api).unwrap();
        let provider = fabrics_instance.provider_from_str("tcp").unwrap();

        assert_eq!(provider.to_string().unwrap(), "tcp");
    }

    mxl_instance.destroy().unwrap();
}

#[test]
fn target_info_roundtrip() {
    let (mxl_instance, _domain_guard) = setup_test("target_info_roundtrip");

    {
        let fabrics_api = load_fabrics_test_api();
        let fabrics_instance = mxl_instance.create_fabrics_instance(&fabrics_api).unwrap();
        let (flow_writer, _flow_reader, _flow_config) = create_video_flow(&mxl_instance);

        let provider = fabrics_instance.provider_from_str("tcp").unwrap();
        let target = fabrics_instance.create_target().unwrap();
        let config = target::Config::new(tcp_interface(&provider), &flow_writer);
        let (_target, target_info) = target.setup(&config).unwrap();

        let serialized = target_info.to_string().unwrap();
        let deserialized = fabrics_instance.target_info_from_str(&serialized).unwrap();

        assert_eq!(serialized, deserialized.to_string().unwrap());
    }

    mxl_instance.destroy().unwrap();
}

#[test]
fn tcp_grain_transfer_delivers_payload_to_target_flow() {
    let (mxl_instance, _domain_guard) = setup_test("tcp_grain_transfer");

    {
        let fabrics_api = load_fabrics_test_api();
        let fabrics_instance = mxl_instance.create_fabrics_instance(&fabrics_api).unwrap();

        let (source_flow_writer, source_flow_reader, source_flow_config) =
            create_video_flow(&mxl_instance);
        let (target_flow_writer, target_flow_reader, _target_flow_config) =
            create_video_flow_with_unique_id(&mxl_instance);

        let source_grain_writer: GrainWriter = source_flow_writer.to_grain_writer().unwrap();
        let source_grain_reader: GrainReader = source_flow_reader.to_grain_reader().unwrap();
        let target_grain_reader: GrainReader = target_flow_reader.to_grain_reader().unwrap();

        let (target, target_info) = {
            let target_provider = fabrics_instance.provider_from_str("tcp").unwrap();
            let target = fabrics_instance.create_target().unwrap();
            let target_config =
                target::Config::new(tcp_interface(&target_provider), &target_flow_writer);
            target.setup(&target_config).unwrap()
        };
        let target_grain_writer: GrainWriter = target_flow_writer.to_grain_writer().unwrap();
        let target = match target.specialize(&source_flow_config) {
            target::Either::Grain(target) => target,
            target::Either::Sample(_) => panic!("expected grain target for video flow"),
        };

        let initiator_flow_reader = mxl_instance
            .create_flow_reader(source_flow_config.common().id().to_string().as_str())
            .unwrap();
        let initiator = {
            let initiator_provider = fabrics_instance.provider_from_str("tcp").unwrap();
            let initiator = fabrics_instance.create_initiator().unwrap();
            let initiator_config = initiator::Config::new(
                tcp_interface(&initiator_provider),
                &initiator_flow_reader,
            );
            initiator.setup(&initiator_config).unwrap()
        };
        let initiator = match initiator.specialize(&source_flow_config) {
            initiator::Either::Grain(initiator) => initiator,
            initiator::Either::Samples(_) => panic!("expected grain initiator for video flow"),
        };

        initiator.add_target(&target_info).unwrap();
        wait_for_grain_connection(&target, &initiator);

        let grain_index =
            mxl_instance.get_current_index(&source_flow_config.common().grain_rate().unwrap());
        let mut grain = source_grain_writer.open_grain(grain_index).unwrap();
        fill_grain_payload(grain.payload_mut());
        let total_slices = grain.total_slices();
        grain.commit(total_slices).unwrap();

        let expected: OwnedGrainData = source_grain_reader
            .get_complete_grain(grain_index, POLL_TIMEOUT)
            .unwrap()
            .into();

        wait_for_grain_transfer_start(&target, &initiator, grain_index, total_slices);
        let completed_index = wait_for_grain_transfer_completion(&target, &initiator, grain_index);

        let committed_grain = target_grain_writer.open_grain(completed_index).unwrap();
        let committed_slices = committed_grain.valid_slices();
        committed_grain.commit(committed_slices).unwrap();

        let actual = wait_for_target_grain(&target_grain_reader, grain_index);
        assert_eq!(actual.payload, expected.payload);

        initiator.remove_target(&target_info).unwrap();
    }

    mxl_instance.destroy().unwrap();
}

#[test]
fn tcp_samples_transfer_delivers_payload_to_target_flow() {
    let (mxl_instance, _domain_guard) = setup_test("tcp_samples_transfer");

    {
        let fabrics_api = load_fabrics_test_api();
        let fabrics_instance = mxl_instance.create_fabrics_instance(&fabrics_api).unwrap();

        let (source_flow_writer, source_flow_reader, source_flow_config) =
            create_audio_flow(&mxl_instance);
        let (target_flow_writer, target_flow_reader, _target_flow_config) =
            create_audio_flow_with_unique_id(&mxl_instance);

        let source_samples_writer: SamplesWriter = source_flow_writer.to_samples_writer().unwrap();
        let source_samples_reader: SamplesReader = source_flow_reader.to_samples_reader().unwrap();
        let target_samples_reader: SamplesReader = target_flow_reader.to_samples_reader().unwrap();

        let (target, target_info) = {
            let target_provider = fabrics_instance.provider_from_str("tcp").unwrap();
            let target = fabrics_instance.create_target().unwrap();
            let target_config =
                target::Config::new(tcp_interface(&target_provider), &target_flow_writer);
            target.setup(&target_config).unwrap()
        };
        let target_samples_writer: SamplesWriter = target_flow_writer.to_samples_writer().unwrap();
        let target = match target.specialize(&source_flow_config) {
            target::Either::Sample(target) => target,
            target::Either::Grain(_) => panic!("expected samples target for audio flow"),
        };

        let initiator_flow_reader = mxl_instance
            .create_flow_reader(source_flow_config.common().id().to_string().as_str())
            .unwrap();
        let initiator = {
            let initiator_provider = fabrics_instance.provider_from_str("tcp").unwrap();
            let initiator = fabrics_instance.create_initiator().unwrap();
            let initiator_config = initiator::Config::new(
                tcp_interface(&initiator_provider),
                &initiator_flow_reader,
            );
            initiator.setup(&initiator_config).unwrap()
        };
        let initiator = match initiator.specialize(&source_flow_config) {
            initiator::Either::Samples(initiator) => initiator,
            initiator::Either::Grain(_) => panic!("expected samples initiator for audio flow"),
        };

        initiator.add_target(&target_info).unwrap();
        wait_for_samples_connection(&target, &initiator);

        let head_index =
            mxl_instance.get_current_index(&source_flow_config.common().sample_rate().unwrap());
        let mut samples = source_samples_writer
            .open_samples(head_index, AUDIO_SAMPLE_COUNT)
            .unwrap();
        fill_samples_payload(&mut samples);
        samples.commit().unwrap();

        let expected: OwnedSamplesData = source_samples_reader
            .get_samples(head_index, AUDIO_SAMPLE_COUNT, POLL_TIMEOUT)
            .unwrap()
            .into();

        wait_for_samples_transfer_start(&target, &initiator, head_index, AUDIO_SAMPLE_COUNT);
        let (completed_head_index, completed_count) = wait_for_samples_transfer_completion(
            &target,
            &initiator,
            head_index,
            AUDIO_SAMPLE_COUNT,
        );

        let committed_samples = target_samples_writer
            .open_samples(completed_head_index, completed_count)
            .unwrap();
        committed_samples.commit().unwrap();

        let actual =
            wait_for_target_samples(&target_samples_reader, head_index, AUDIO_SAMPLE_COUNT);
        assert_eq!(actual.payload, expected.payload);

        initiator.remove_target(&target_info).unwrap();
    }

    mxl_instance.destroy().unwrap();
}
