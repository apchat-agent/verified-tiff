//! Permanent replay: every minimized fuzz regression must pass through the
//! step-1 oracle cleanly (accepted without violation, or rejected without a
//! panic), and the designated known-bad cases must be rejected outright.
use verified_tiff::Limits;

#[path = "../fuzz/oracle.rs"]
mod oracle;

const LIMITS: Limits = Limits { max_input: 32768, max_dimension: 128, max_output: 16384,
    max_directories: 8, max_tags: 128, max_segments: 128 };

fn regressions() -> Vec<(String, Vec<u8>)> {
    let mut dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    dir.push("tests/fixtures/fuzz-regressions");
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&dir) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().map_or(false, |x| x == "tif") {
                out.push((p.to_string_lossy().into_owned(), std::fs::read(&p).unwrap()));
            }
        }
    }
    out.sort();
    out
}

#[test]
fn every_regression_replays_without_violation() {
    let files = regressions();
    assert!(!files.is_empty(), "fuzz regression corpus must not be empty");
    for (name, data) in files {
        oracle::check_input(&data, &LIMITS)
            .unwrap_or_else(|v| panic!("{name}: oracle violation {v:?}"));
    }
}

#[test]
fn known_bad_case_is_rejected_without_panic() {
    let files = regressions();
    let (name, data) = files.iter().find(|(n, _)| n.contains("case-0001"))
        .expect("known-bad fixture case-0001 must exist");
    let s = oracle::check_input(data, &LIMITS).expect("no violation on a rejected file");
    assert_eq!(s.pages_ok, 0, "{name} must be rejected, not decoded");
}
