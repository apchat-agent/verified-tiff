//! Fuzz oracle: inspect every page and decode it, checking the contracts the
//! public API promises. Plain Rust, shared by the libFuzzer targets and the
//! ordinary tests through #[path] includes.
use verified_tiff::{inspect_raster, raster_inspection_work_limit, Budget, Limits, Raster};

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
    /// Success was reported although buffers or budget were insufficient, or
    /// a complete decode was refused although buffers and budget sufficed.
    PartialSuccess,
    /// Bytes outside the decoded region were modified.
    Framing,
}

/// Two decodes over differently prefilled buffers must agree on the whole
/// decoded prefix of `output_len` bytes; a decoder that reports success after
/// a shorter write leaves prefill-dependent bytes inside the prefix.
pub fn check_decoded_length(a: &[u8], b: &[u8], output_len: usize) -> bool {
    a.len() >= output_len && b.len() >= output_len && a[..output_len] == b[..output_len]
}

/// Bytes beyond the decoded region must keep their prefill sentinel.
pub fn check_frame(buf: &[u8], prefill: u8, output_len: usize) -> bool {
    buf.len() >= output_len && buf[output_len..].iter().all(|b| *b == prefill)
}

/// Total charged work must stay within the budget the API promised.
pub fn check_work_within(spent: usize, promised: usize) -> bool { spent <= promised }

/// The same input decoded twice must give identical bytes.
pub fn check_identical(a: &[u8], b: &[u8], output_len: usize) -> bool {
    check_decoded_length(a, b, output_len)
}

/// With insufficient buffers or budget the API must return an error rather
/// than report success over a partial buffer; `sufficient` says whether the
/// run had output/scratch/budget at least the promised sizes.
pub fn check_error_not_partial(ok: bool, sufficient: bool) -> bool { !ok || sufficient }

/// Inspect every page of `data` under `limits`, decode each accepted page,
/// and check the API's promised contracts. `Err` is a finding; library
/// rejections are clean and counted in the summary.
pub fn check_input(data: &[u8], limits: &Limits) -> Result<Summary, Violation> {
    let mut summary = Summary { pages_ok: 0, pages_rejected: 0 };
    for page in 0..limits.max_directories {
        let promised = match raster_inspection_work_limit(page, limits) {
            Ok(bound) => bound,
            Err(_) => { summary.pages_rejected += 1; break; }
        };
        let mut budget = Budget { remaining: promised + INSPECTION_SLACK };
        match inspect_raster(data, page, limits, &mut budget) {
            Err(_) => { summary.pages_rejected += 1; break; }
            Ok(image) => {
                let spent = promised + INSPECTION_SLACK - budget.remaining;
                if !check_work_within(spent, promised) { return Err(Violation::WorkExceeded); }
                check_page(&image)?;
                summary.pages_ok += 1;
            }
        }
    }
    Ok(summary)
}

fn check_page(image: &Raster) -> Result<(), Violation> {
    let olen = image.output_len();
    let scratch = image.scratch_len();
    let work = image.decode_work_limit();
    let mut out_a = vec![PREFILL_A; olen + GUARD];
    let mut scr_a = vec![PREFILL_A; scratch + GUARD];
    let mut budget_a = Budget { remaining: work };
    let r_a = image.decode_with_scratch(&mut out_a, &mut scr_a, &mut budget_a);
    if r_a.is_ok() && budget_a.remaining != 0 { return Err(Violation::WorkExceeded); }
    let mut out_b = vec![PREFILL_B; olen + GUARD];
    let mut scr_b = vec![PREFILL_B; scratch + GUARD];
    let mut budget_b = Budget { remaining: work };
    let r_b = image.decode_with_scratch(&mut out_b, &mut scr_b, &mut budget_b);
    let mut out_c = vec![PREFILL_A; olen];
    let mut scr_c = vec![PREFILL_A; scratch];
    let mut budget_c = Budget { remaining: work };
    let r_c = image.decode_with_scratch(&mut out_c, &mut scr_c, &mut budget_c);
    if r_a.is_err() || r_b.is_err() || r_c.is_err() { return Err(Violation::PartialSuccess); }
    if !check_decoded_length(&out_a, &out_b, olen) { return Err(Violation::DecodedLength); }
    if !check_identical(&out_a, &out_c, olen) { return Err(Violation::NonDeterministic); }
    if !check_frame(&out_a, PREFILL_A, olen) || !check_frame(&out_b, PREFILL_B, olen)
        || !check_frame(&scr_a, PREFILL_A, scratch) { return Err(Violation::Framing); }
    let mut short = vec![PREFILL_A; olen.saturating_sub(1)];
    let mut scr_short = vec![PREFILL_A; scratch + GUARD];
    let mut budget_short = Budget { remaining: work };
    let r_short = image.decode_with_scratch(&mut short, &mut scr_short, &mut budget_short);
    if !check_error_not_partial(r_short.is_ok(), short.len() >= olen) {
        return Err(Violation::PartialSuccess);
    }
    let mut out_d = vec![PREFILL_A; olen + GUARD];
    let mut scr_d = vec![PREFILL_A; scratch + GUARD];
    let mut budget_d = Budget { remaining: work.saturating_sub(1) };
    let r_d = image.decode_with_scratch(&mut out_d, &mut scr_d, &mut budget_d);
    if !check_error_not_partial(r_d.is_ok(), budget_d.remaining >= work) {
        return Err(Violation::PartialSuccess);
    }
    Ok(())
}
