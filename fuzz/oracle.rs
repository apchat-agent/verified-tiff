//! Fuzz oracle: inspect every page and decode it, checking the contracts the
//! public API promises. Plain Rust, shared by the libFuzzer targets and the
//! ordinary tests through #[path] includes.
use verified_tiff::{inspect_raster, raster_inspection_work_limit, Budget, Error, Limits, Raster};

/// Prefill sentinels and guard size for writes outside the decoded region.
pub const PREFILL_A: u8 = 0xA5;
pub const PREFILL_B: u8 = 0x5A;
pub const GUARD: usize = 16;
/// Inspection probes get the promised bound plus this slack, so overspending
/// the promise stays observable.
pub const INSPECTION_SLACK: usize = 1024;

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

/// The per-page decode surface the oracle exercises: the real `Raster` and
/// test fakes.
pub trait PageImage {
    fn output_len(&self) -> usize;
    fn scratch_len(&self) -> usize;
    fn decode_work_limit(&self) -> usize;
    fn decode_with_scratch(&self, output: &mut [u8], scratch: &mut [u8],
        budget: &mut Budget) -> Result<(), Error>;
}

impl PageImage for Raster<'_> {
    fn output_len(&self) -> usize { Raster::output_len(self) }
    fn scratch_len(&self) -> usize { Raster::scratch_len(self) }
    fn decode_work_limit(&self) -> usize { Raster::decode_work_limit(self) }
    fn decode_with_scratch(&self, output: &mut [u8], scratch: &mut [u8],
        budget: &mut Budget) -> Result<(), Error> {
        Raster::decode_with_scratch(self, output, scratch, budget)
    }
}

/// Inspection budget handed to one page probe: the promised bound plus slack.
pub fn inspection_budget(promised: usize) -> usize {
    promised.checked_add(INSPECTION_SLACK).unwrap_or(usize::MAX)
}

/// Work charged by one probe: budget before minus budget after.
pub fn inspection_spent(before: usize, after: usize) -> usize { before - after }

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
        let before = inspection_budget(promised);
        let mut budget = Budget { remaining: before };
        let result=inspect_raster(data,page,limits,&mut budget);
        if budget.remaining>before || !check_work_within(before-budget.remaining,promised) {
            return Err(Violation::WorkExceeded);
        }
        match result {
            Err(_) => { summary.pages_rejected += 1; break; }
            Ok(image) => {
                let spent = inspection_spent(before, budget.remaining);
                if !check_work_within(spent, promised) { return Err(Violation::WorkExceeded); }
                check_page(&image)?;
                summary.pages_ok += 1;
            }
        }
    }
    Ok(summary)
}

/// Run the full decode matrix over one accepted page and check every promised
/// contract; `I` is the real `Raster` or a test fake.
pub fn check_page<I: PageImage>(image: &I) -> Result<(), Violation> {
    let olen = image.output_len();
    let scratch = image.scratch_len();
    let work = image.decode_work_limit();
    // Every probe checks monotone budget, exact success charge and both frames.
    // Short buffers have backing guards, but only their short prefixes are offered.
    let probe = |out_len:usize,scr_len:usize,before:usize,prefill:u8|
        -> Result<Vec<u8>,Violation> {
        let mut out=vec![prefill; out_len+GUARD];
        let mut scr=vec![prefill; scr_len+GUARD];
        let mut budget=Budget{remaining:before};
        let result=image.decode_with_scratch(&mut out[..out_len],&mut scr[..scr_len],&mut budget);
        let sufficient=out_len>=olen && scr_len>=scratch && before>=work;
        if result.is_ok()!=sufficient {return Err(Violation::PartialSuccess);}
        if budget.remaining>before || (result.is_ok() && before-budget.remaining!=work) {
            return Err(Violation::WorkExceeded);
        }
        if !check_frame(&out,prefill,olen.min(out_len)) ||
           !check_frame(&scr,prefill,scratch.min(scr_len)) {
            return Err(Violation::Framing);
        }
        Ok(out)
    };
    let a=probe(olen+GUARD,scratch+GUARD,work,PREFILL_A)?;
    let b=probe(olen+GUARD,scratch+GUARD,work,PREFILL_B)?;
    let c=probe(olen,scratch,work,PREFILL_A)?;
    if !check_decoded_length(&a,&b,olen) {return Err(Violation::DecodedLength);}
    if !check_identical(&a,&c,olen) {return Err(Violation::NonDeterministic);}
    probe(olen.saturating_sub(1),scratch+GUARD,work,PREFILL_A)?;
    probe(olen+GUARD,scratch+GUARD,work.saturating_sub(1),PREFILL_A)?;
    if scratch>0 {probe(olen+GUARD,scratch-1,work,PREFILL_A)?;}
    Ok(())
}
