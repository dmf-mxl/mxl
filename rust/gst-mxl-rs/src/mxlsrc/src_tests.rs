// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[cfg(test)]
mod tests {
    use glib::{object::ObjectExt, subclass::types::ObjectSubclassType};
    use gst::CoreError;
    use gst::prelude::*;
    use gstreamer as gst;
    use std::{thread, time::Duration};

    use crate::mxlsrc::imp::*;

    #[test]
    fn set_properties() -> Result<(), glib::Error> {
        gst::init()?;
        gst::Element::register(None, "mxlsrc", gst::Rank::NONE, MxlSrc::type_())
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let element = gst::ElementFactory::make("mxlsrc")
            .property("video-flow", "test_flow")
            .property("domain", "mydomain")
            .build()
            .map_err(|e| glib::Error::new(CoreError::Failed, &e.message))?;

        let flow_id: String = element.property("video-flow");
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
            .property("video-flow", "8fbec3b1-1b0f-417d-9059-8b94a47197ed")
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

        pipeline_producer
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Producer state change failed"))?;
        pipeline_consumer
            .set_state(gst::State::Playing)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Consumer state change failed"))?;

        thread::sleep(Duration::from_millis(600));

        pipeline_producer
            .set_state(gst::State::Null)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Producer state change failed"))?;
        pipeline_consumer
            .set_state(gst::State::Null)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Consumer state change failed"))?;

        Ok(())
    }
}
