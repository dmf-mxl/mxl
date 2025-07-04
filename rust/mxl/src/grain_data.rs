pub struct GrainData<'a> {
    pub user_data: &'a [u8],
    pub payload: &'a [u8],
}

impl<'a> GrainData<'a> {
    pub fn to_owned(&self) -> OwnedGrainData {
        self.into()
    }
}

impl<'a> AsRef<GrainData<'a>> for GrainData<'a> {
    fn as_ref(&self) -> &GrainData<'a> {
        self
    }
}

pub struct OwnedGrainData {
    pub user_data: Vec<u8>,
    pub payload: Vec<u8>,
}

impl<'a> From<&GrainData<'a>> for OwnedGrainData {
    fn from(value: &GrainData<'a>) -> Self {
        Self {
            user_data: value.user_data.to_vec(),
            payload: value.payload.to_vec(),
        }
    }
}

impl<'a> From<GrainData<'a>> for OwnedGrainData {
    fn from(value: GrainData<'a>) -> Self {
        value.as_ref().into()
    }
}
