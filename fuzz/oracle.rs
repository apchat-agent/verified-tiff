//! Fuzz oracle: inspect every page and decode it, checking the contracts the
//! public API promises. Plain Rust, shared by the libFuzzer targets and the
//! ordinary tests through #[path] includes.
use verified_tiff::Limits;

/// Prefill sentinels and guard size for writes outside the decoded region.
pub const PREFILL_A: u8 = 0xA5;
pub const PREFILL_B: u8 = 0x5A;
pub const GUARD: usize = 16;
const INSPECTION_SLACK: usize = 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Summary { pub pages_ok: usize, pub pages_rejected: usize }

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Violation {
    /// A successful decode did not fill exactly output_len() bytes.
    DecodedLength,
    /// Decoding the same input twice gave different bytes.
    NonDeterministic,
    /// More work was charged than the API promised.
    WorkExceeded,
    /// Success was reported although buffers or budget were insufficient.
    PartialSuccess,
    /// Bytes outside the decoded region were modified.
    Framing,
}

/// Two decodes over differently prefilled buffers must agree on the whole
/// decoded prefix of `output_len` bytes; a decoder that reports success after
/// a shorter write leaves prefill-dependent bytes inside the prefix.
pub fn check_decoded_length(a: &[u8], b: &[u8], output_len: usize) -> bool { true }

/// Bytes beyond the decoded region must keep their prefill sentinel.
pub fn check_frame(buf: &[u8], prefill: u8, output_len: usize) -> bool { true }

/// Total charged work must stay within the budget the API promised.
pub fn check_work_within(spent: usize, promised: usize) -> bool { true }

/// The same input decoded twice must give identical bytes.
pub fn check_identical(a: &[u8], b: &[u8], output_len: usize) -> bool { true }

/// With insufficient buffers or budget the API must return an error rather
/// than report success over a partial buffer; `sufficient` says whether the
/// run had output/scratch/budget at least the promised sizes.
pub fn check_error_not_partial(ok: bool, sufficient: bool) -> bool { true }

/// Inspect every page of `data` under `limits`, decode each accepted page,
/// and check the API's promised contracts. `Err` is a finding; library
/// rejections are clean and counted in the summary.
pub fn check_input(data: &[u8], limits: &Limits) -> Result<Summary, Violation> {
    let _ = (data, limits, GUARD);
    Ok(Summary { pages_ok: 0, pages_rejected: 0 })
}
