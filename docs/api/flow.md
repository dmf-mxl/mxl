# flow.h

Flow creation, reader and writer APIs, grain access, and synchronization primitives.

## Handle Types

- `mxlFlowReader`
- `mxlFlowWriter`
- `mxlFlowSynchronizationGroup`

## Writer Functions

### mxlCreateFlowWriter
Creates or opens a flow writer based on a flow definition.

### mxlFlowWriterOpenGrain / CommitGrain / CancelGrain
Open, publish, or cancel mutation of a grain.

### mxlFlowWriterOpenSamples / CommitSamples / CancelSamples
Open and commit sample ranges for continuous flows.

## Reader Functions

### mxlCreateFlowReader
Opens a flow reader for an existing flow.

### mxlFlowReaderGetGrain*
Blocking and non-blocking grain access functions.

### mxlFlowReaderGetSamples*
Sample-based accessors for continuous flows.

## Flow Metadata

### mxlFlowReaderGetInfo
Retrieves runtime flow metadata.

### mxlGetFlowDef
Retrieves the JSON flow definition used to create a flow.

## Synchronization Groups

Functions for synchronizing reads across multiple flows:

- `mxlCreateFlowSynchronizationGroup`
- `mxlFlowSynchronizationGroupAddReader`
- `mxlFlowSynchronizationGroupWaitForDataAt`
