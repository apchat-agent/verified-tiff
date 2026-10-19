//! Fuzz oracle checks (fuzz/oracle.rs): every check must flag a deliberately
//! wrong case and accept the corresponding correct one.
use std::cell::Cell;
use verified_tiff::{Budget, Error, Limits};

#[path = "../fuzz/oracle.rs"]
mod oracle;

const LIMITS: Limits = Limits { max_input: 32768, max_dimension: 128, max_output: 16384,
    max_directories: 8, max_tags: 128, max_segments: 128 };

fn fixture(name: &str) -> Vec<u8> {
    let mut p = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("tests/fixtures");
    p.push(name);
    std::fs::read(p).unwrap()
}

#[test]
fn short_write_is_flagged() {
    // Fake decoder result: success after writing only output_len-1 bytes, so
    // the two differently prefilled buffers disagree inside the prefix.
    let olen = 8usize;
    let mut a = vec![7u8; olen + 1];
    a[olen - 1] = oracle::PREFILL_A;
    let mut b = vec![7u8; olen + 1];
    b[olen - 1] = oracle::PREFILL_B;
    assert!(!oracle::check_decoded_length(&a, &b, olen), "short write must be flagged");
    assert!(!oracle::check_identical(&a, &b, olen), "short write must be flagged");
}

#[test]
fn overlong_write_is_flagged() {
    // Fake decoder result: success after overwriting the tail guard.
    let olen = 8usize;
    let a = vec![9u8; olen + 3];
    assert!(!oracle::check_frame(&a, oracle::PREFILL_A, olen), "overlong write must be flagged");
}

#[test]
fn overcharged_work_is_flagged() {
    assert!(!oracle::check_work_within(401, 400), "overspend must be flagged");
    assert!(oracle::check_work_within(400, 400));
    assert!(oracle::check_work_within(0, 400));
}

#[test]
fn partial_success_is_flagged() {
    // Ok reported although the buffer or budget was insufficient.
    assert!(!oracle::check_error_not_partial(true, false), "partial success must be flagged");
    assert!(oracle::check_error_not_partial(false, false));
    assert!(oracle::check_error_not_partial(true, true));
}

#[test]
fn full_correct_write_passes() {
    let olen = 6usize;
    let mut a = vec![1u8, 2, 3, 4, 5, 6];
    a.push(oracle::PREFILL_A);
    let mut b = vec![1u8, 2, 3, 4, 5, 6];
    b.push(oracle::PREFILL_B);
    assert!(oracle::check_decoded_length(&a, &b, olen));
    assert!(oracle::check_identical(&a, &b, olen));
    assert!(oracle::check_frame(&a, oracle::PREFILL_A, olen));
    assert!(oracle::check_frame(&b, oracle::PREFILL_B, olen));
}

#[test]
fn oracle_inspects_and_decodes_a_profile_fixture() {
    let data = fixture("gray-strip.tif");
    let s = oracle::check_input(&data, &LIMITS).expect("no violation on a valid fixture");
    assert!(s.pages_ok >= 1, "gray-strip.tif must inspect and decode");
}

#[test]
fn oracle_decodes_a_caller_scratch_fixture() {
    let data = fixture("jpeg-gray.tif");
    let s = oracle::check_input(&data, &LIMITS).expect("no violation on a jpeg fixture");
    assert!(s.pages_ok >= 1, "jpeg-gray.tif must inspect and decode");
}

#[test]
fn oracle_rejects_garbage_cleanly() {
    let garbage = vec![0x49, 0x49, 0x2a, 0x00, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0];
    let s = oracle::check_input(&garbage, &LIMITS).expect("no violation on garbage");
    assert_eq!(s.pages_ok, 0);
    assert!(s.pages_rejected >= 1, "garbage must be rejected cleanly");
}

#[test]
fn overflow_bound_counts_one_rejection() {
    let huge = Limits { max_input: 1 << 20, max_dimension: 1 << 20, max_output: usize::MAX,
        max_directories: 8, max_tags: 128, max_segments: 128 };
    let garbage = vec![0x49, 0x49, 0x2a, 0x00, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0];
    let s = oracle::check_input(&garbage, &huge).expect("no violation on overflow bound");
    assert_eq!(s.pages_ok, 0);
    assert_eq!(s.pages_rejected, 1);
}

#[test]
fn inspection_budget_and_spent_arithmetic() {
    assert_eq!(oracle::inspection_budget(5), 5 + oracle::INSPECTION_SLACK);
    assert_eq!(oracle::inspection_budget(0), oracle::INSPECTION_SLACK);
    assert_eq!(oracle::inspection_spent(100, 40), 60);
    assert_eq!(oracle::inspection_spent(1024, 1024), 0);
}

#[derive(Clone, Copy)]
struct FakeRun { ok: bool, write: usize, charge: usize, break_out_frame: bool, break_scr_frame: bool }
struct Fake { olen: usize, scratch: usize, work: usize, runs: [FakeRun; 5], at: Cell<usize> }

impl oracle::PageImage for Fake {
    fn output_len(&self) -> usize { self.olen }
    fn scratch_len(&self) -> usize { self.scratch }
    fn decode_work_limit(&self) -> usize { self.work }
    fn decode_with_scratch(&self, output: &mut [u8], scratch: &mut [u8],
        budget: &mut Budget) -> Result<(), Error> {
        let i = self.at.get();
        self.at.set(i + 1);
        let run = &self.runs[i];
        if !run.ok { return Err(Error::Limit); }
        for k in 0..run.write { output[k] = (k % 251) as u8 + 3; }
        if run.break_out_frame && run.write < output.len() { output[run.write] = 0xEE; }
        if run.break_scr_frame && !scratch.is_empty() { scratch[scratch.len() - 1] = 0xEE; }
        budget.remaining -= run.charge;
        Ok(())
    }
}

fn clean(olen: usize, scratch: usize, work: usize) -> Fake {
    let ok = FakeRun { ok: true, write: olen, charge: work, break_out_frame: false, break_scr_frame: false };
    let bad = FakeRun { ok: false, write: 0, charge: 0, break_out_frame: false, break_scr_frame: false };
    Fake { olen, scratch, work, at: Cell::new(0), runs: [ok, ok, ok, bad, bad] }
}

fn flagged(f: Fake, why: &str, want: oracle::Violation) {
    match oracle::check_page(&f) {
        Ok(()) => panic!("{why}: oracle must flag the fake result"),
        Err(v) => assert_eq!(v, want, "{why}: wrong violation"),
    }
}

#[test]
fn fake_clean_result_passes_every_check() {
    let f = clean(8, 5, 40);
    assert_eq!(oracle::check_page(&f), Ok(()));
}

#[test]
fn fake_short_write_is_flagged() {
    let mut f = clean(8, 5, 40);
    f.runs[0].write = 7;
    flagged(f, "short write", oracle::Violation::DecodedLength);
}

#[test]
fn fake_single_run_errors_are_flagged() {
    for index in 0..3 {
        let mut f = clean(8, 5, 40);
        f.runs[index].ok = false;
        flagged(f, "refused complete decode", oracle::Violation::PartialSuccess);
    }
}

#[test]
fn fake_overlong_output_writes_are_flagged() {
    for index in 0..2 {
        let mut f = clean(8, 5, 40);
        f.runs[index].write = 9;
        flagged(f, "overlong output write", oracle::Violation::Framing);
    }
}

#[test]
fn fake_overlong_scratch_writes_are_flagged() {
    let mut f = clean(8, 5, 40);
    f.runs[0].break_scr_frame = true;
    flagged(f, "overlong scratch write", oracle::Violation::Framing);
}

#[test]
fn fake_success_on_short_output_is_flagged() {
    let mut f = clean(8, 5, 40);
    f.runs[3].ok = true;
    f.runs[3].write = 7;
    f.runs[3].charge = 40;
    flagged(f, "success on short output", oracle::Violation::PartialSuccess);
}

#[test]
fn fake_success_on_short_budget_is_flagged() {
    let mut f = clean(8, 5, 40);
    f.runs[4].ok = true;
    f.runs[4].write = 8;
    f.runs[4].charge = 0;
    flagged(f, "success on short budget", oracle::Violation::PartialSuccess);
}

