// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[cfg(test)]
mod tests {
    use glib::{object::ObjectExt, subclass::types::ObjectSubclassType};
    use gst::CoreError;
    use gst::prelude::*;
    use gstreamer as gst;
    use std::{thread, time::Duration};

    use crate::mxlsrc::imp::*;
    use crate::mxlsrc::mxl_helper;
    use std::path::PathBuf;

    const SHM_MXL_HINT: &str = "MXL uses mkdtemp(3) under /dev/shm; run integration tests \
        on Linux with tmpfs (/dev/shm)";

    /// Per-test MXL domain under `/dev/shm`, removed on drop.
    struct TestDomainGuard {
        dir: PathBuf,
    }

    impl TestDomainGuard {
        fn new(test: &str) -> Self {
            let dir = PathBuf::from(format!(
                "/dev/shm/mxl_gst_test_domain_{test}_{}",
                uuid::Uuid::new_v4()
            ));
            std::fs::create_dir_all(&dir).unwrap_or_else(|e| {
                panic!("create test domain {}: {e}\n{SHM_MXL_HINT}", dir.display())
            });
            Self { dir }
        }

        fn domain(&self) -> String {
            self.dir.to_string_lossy().into_owned()
        }
    }

    impl Drop for TestDomainGuard {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.dir);
        }
    }

    /// Fail if the pipeline posts an ERROR within `wait`.
    fn assert_no_bus_error(bus: &gst::Bus, wait: gst::ClockTime, what: &str) {
        if let Some(msg) = bus.timed_pop_filtered(wait, &[gst::MessageType::Error]) {
            panic!("{what}: {msg:?}");
        }
    }

    /// Block until the source's streaming thread has entered its task, so that
    /// `create()` is running.
    fn wait_for_streaming_thread(bus: &gst::Bus) {
        let timeout = gst::ClockTime::from_seconds(5);
        while let Some(msg) = bus.timed_pop_filtered(
            timeout,
            &[gst::MessageType::StreamStatus, gst::MessageType::Error],
        ) {
            match msg.view() {
                gst::MessageView::StreamStatus(s) if s.get().0 == gst::StreamStatusType::Enter => {
                    return;
                }
                gst::MessageView::Error(_) => {
                    panic!("ERROR while starting the streaming thread: {msg:?}")
                }
                _ => {}
            }
        }
        panic!("streaming thread did not start within {timeout}");
    }

    #[test]
    fn set_properties() -> Result<(), glib::Error> {
        gst::init()?;
        gst::Element::register(None, "mxlsrc", gst::Rank::NONE, MxlSrc::type_())
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let element = gst::ElementFactory::make("mxlsrc")
            .property("video-flow-id", "test_flow")
            .property("domain", "mydomain")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let flow_id: String = element.property("video-flow-id");
        let domain: String = element.property("domain");

        assert_eq!(flow_id, "test_flow");
        assert_eq!(domain, "mydomain");
        Ok(())
    }

    #[test]
    #[cfg_attr(feature = "tracing", tracing_test::traced_test)]
    fn start_valid_pipeline() -> Result<(), glib::Error> {
        gst::init()?;
        gst::Element::register(None, "mxlsrc", gst::Rank::NONE, MxlSrc::type_())
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        gst::Element::register(
            None,
            "mxlsink",
            gst::Rank::NONE,
            crate::mxlsink::MxlSink::static_type(),
        )
        .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        // -------------------------
        // Pipeline 1: Producer
        // -------------------------
        let src0 = gst::ElementFactory::make("videotestsrc")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        let queue1 = gst::ElementFactory::make("queue")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        let convert1 = gst::ElementFactory::make("videoconvert")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        let queue2 = gst::ElementFactory::make("queue")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let mxlsink = gst::ElementFactory::make("mxlsink")
            .property("flow-id", "8fbec3b1-1b0f-417d-9059-8b94a47197ed")
            .property("domain", "/dev/shm")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let pipeline_producer = gst::Pipeline::new();
        pipeline_producer
            .add_many([&src0, &queue1, &convert1, &queue2, &mxlsink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        gst::Element::link_many([&src0, &queue1, &convert1, &queue2, &mxlsink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        // -------------------------
        // Pipeline 2: Consumer
        // -------------------------
        let src1 = gst::ElementFactory::make("mxlsrc")
            .property("video-flow-id", "8fbec3b1-1b0f-417d-9059-8b94a47197ed")
            .property("domain", "/dev/shm")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let sink = gst::ElementFactory::make("fakesink")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let pipeline_consumer = gst::Pipeline::new();
        pipeline_consumer
            .add_many([&src1, &sink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        gst::Element::link_many([&src1, &sink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        // Consumer first to demonstrate that mxlsrc does not block the state change
        // if the MXL flow does not exist yet.
        pipeline_consumer
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Consumer state change failed"))?;
        pipeline_producer
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Producer state change failed"))?;

        thread::sleep(Duration::from_millis(600));

        pipeline_producer
            .set_state(gst::State::Null)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Producer state change failed"))?;
        pipeline_consumer
            .set_state(gst::State::Null)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Consumer state change failed"))?;

        Ok(())
    }

    #[test]
    fn pause_while_waiting_for_missing_flow_does_not_error() -> Result<(), glib::Error> {
        gst::init()?;
        gst::Element::register(None, "mxlsrc", gst::Rank::NONE, MxlSrc::type_())
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let domain = TestDomainGuard::new("pause_wait");
        let src = gst::ElementFactory::make("mxlsrc")
            .property("video-flow-id", "55555555-aaaa-5555-a555-555555555555")
            .property("domain", domain.domain())
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        let sink = gst::ElementFactory::make("fakesink")
            .property("sync", false)
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let pipeline = gst::Pipeline::new();
        pipeline
            .add_many([&src, &sink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;
        gst::Element::link_many([&src, &sink])
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let bus = pipeline
            .bus()
            .ok_or_else(|| glib::Error::new(CoreError::Failed, "pipeline has no bus"))?;
        pipeline
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Playing state change failed"))?;
        // Live source: PLAYING returns before create() reaches the wait, and
        // ENTER is posted in the same moment the task starts, so wait across
        // a couple of FlowNotFound retries before interrupting.
        wait_for_streaming_thread(&bus);
        thread::sleep(mxl_helper::FLOW_NOT_FOUND_RETRY * 2);

        // PAUSED calls unlock() on the source, interrupting the wait.
        pipeline
            .set_state(gst::State::Paused)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Paused state change failed"))?;
        pipeline
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Playing state change failed"))?;

        // An interrupted wait posts not-negotiated within a few milliseconds.
        assert_no_bus_error(
            &bus,
            gst::ClockTime::from_mseconds(mxl_helper::FLOW_NOT_FOUND_RETRY.as_millis() as u64),
            "pausing while waiting for a missing flow posted ERROR",
        );
        pipeline
            .set_state(gst::State::Null)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Null state change failed"))?;
        Ok(())
    }
}
