//! Fuzz oracle checks (fuzz/oracle.rs): every check must flag a deliberately
//! wrong case and accept the corresponding correct one.
use verified_tiff::Limits;

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
fn oracle_rejects_garbage_cleanly() {
    let garbage = vec![0x49, 0x49, 0x2a, 0x00, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0];
    let s = oracle::check_input(&garbage, &LIMITS).expect("no violation on garbage");
    assert_eq!(s.pages_ok, 0);
    assert!(s.pages_rejected >= 1, "garbage must be rejected cleanly");
}
