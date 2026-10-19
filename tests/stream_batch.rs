use verified_tiff::{native_stream::Inflater,Budget,Error};

fn batched(wire:&[u8],cap:usize,window:usize,chunk:usize,work:usize)->Result<(Vec<u8>,usize),Error>{
    let mut start=0usize;let mut end=window.min(wire.len());
    let mut decoder=Inflater::new(&wire[..end],cap)?;
    let (mut at,mut bit)=(2usize,0usize);let mut budget=Budget{remaining:work};let mut output=Vec::new();
    loop {
        if end<wire.len() && end-start-at<1024 {
            start+=at;at=0;end=(start+window).min(wire.len());
        }
        if decoder.mode()==4&&end<wire.len(){return Err(Error::Invalid);}
        let input=&wire[start..end];let mut scratch=vec![0xa5;chunk+13];
        let (next,b,n,done)=decoder.pull(input,at,bit,input.len()-4,end==wire.len(),&mut scratch,chunk,&mut budget)?;
        assert!(scratch[n..].iter().all(|v|*v==0xa5));
        output.extend_from_slice(&scratch[..n]);
        assert!(done || n>0 || next>at || b!=bit || end<wire.len());
        at=next;bit=b;
        if done{return Ok((output,work-budget.remaining));}
    }
}

#[test]
fn batch_windows_outputs_and_work_match_scalar_transitions(){
    let expected:Vec<u8>=(0..130001).map(|i|((i*73+(i/97)*19+i/4093)&255) as u8).collect();
    for wire in [include_bytes!("fixtures/stream-batch-stored.zlib").as_slice(),include_bytes!("fixtures/stream-batch-fixed.zlib").as_slice(),include_bytes!("fixtures/stream-batch-dynamic.zlib").as_slice()] {
        let fee=2_000_001;let mut budget=Budget{remaining:fee};let mut scalar=Inflater::new(wire,expected.len()).unwrap();
        let (mut at,mut bit)=(2,0);let mut reference=Vec::new();
        loop {
            let (a,b,event,byte)=scalar.step(wire,at,bit,wire.len()-4,&mut budget).unwrap();at=a;bit=b;
            if event==1{reference.push(byte);}
            if event==2{break;}
        }
        assert_eq!(reference,expected);let steps=fee-budget.remaining;
        for window in [2048,4096,65536] {for chunk in [1,7,4096,131072] {
            let (actual,charged)=batched(wire,expected.len(),window,chunk,fee).unwrap();
            assert_eq!(actual,expected);assert_eq!(charged,2*steps);
        }}
        assert_eq!(batched(wire,expected.len(),4096,4096,2*steps).unwrap().0,expected);
        assert_eq!(batched(wire,expected.len(),4096,4096,2*steps-1),Err(Error::Limit));
        assert!(batched(wire,expected.len()-1,4096,4096,fee).is_err());
        for n in [0,1,5,wire.len()/2,wire.len()-4,wire.len()-1] {
            assert!(batched(&wire[..n],expected.len(),4096,4096,fee).is_err());
        }
        let mut corrupt=wire.to_vec();*corrupt.last_mut().unwrap()^=1;
        assert!(batched(&corrupt,expected.len(),4096,4096,fee).is_err());
        let mut trailing=wire.to_vec();trailing.extend_from_slice(b"junk");
        assert!(batched(&trailing,expected.len(),4096,4096,fee).is_err());
    }
}

#[test]
fn batch_rejection_preserves_suffix_and_handles_empty_output(){
    let wire=[0x78,0x01,0x01,3,0,0xfc,0xff,65,66,67,1,0x8d,0,0xc7];
    for (at,bit,end,write,work) in [(2,8,10,3,100),(11,0,10,3,100),(2,0,11,3,100),(2,0,10,9,100),(2,0,10,3,0),(2,0,10,3,1)] {
        let mut decoder=Inflater::new(&wire,3).unwrap();let mut out=[0xa5;8];
        assert!(decoder.pull(&wire,at,bit,end,true,&mut out,write,&mut Budget{remaining:work}).is_err());assert_eq!(out,[0xa5;8]);
    }
    let mut decoder=Inflater::new(&wire,3).unwrap();let mut out=[0xa5;8];let mut b=Budget{remaining:100};
    assert_eq!(decoder.pull(&wire,2,0,10,true,&mut out,0,&mut b),Ok((2,0,0,false)));assert_eq!(b.remaining,100);assert_eq!(out,[0xa5;8]);
    assert_eq!(decoder.pull(&wire,2,0,10,true,&mut out,8,&mut b),Ok((10,0,3,true)));assert_eq!(&out[..3],b"ABC");assert_eq!(&out[3..],&[0xa5;5]);
}

#[test]
fn copy_runs_wrap_overlap_and_input_windows(){
    let wire=include_bytes!("fixtures/copy-boundaries.zlib");
    let mut expected:Vec<u8>=(0..=255).collect();expected.extend_from_slice(b"AB");
    expected.extend_from_within(..258); // distance == length, nonconstant phrase
    for _ in 0..125 {for _ in 0..258 {let byte=expected[expected.len()-256];expected.push(byte);}}
    assert_eq!(expected.len(),32766);
    let last=*expected.last().unwrap();expected.extend(std::iter::repeat_n(last,258)); // distance 1 across wrap
    expected.extend_from_within(expected.len()-258..); // adjacent complete phrase
    let (reference,charged)=batched(wire,expected.len(),65536,65536,2_000_000).unwrap();
    assert_eq!(reference,expected);
    for window in [1031,2048,65536] {for chunk in [1,257,32769] {
        let (actual,work)=batched(wire,expected.len(),window,chunk,charged).unwrap();
        assert_eq!(actual,expected);assert_eq!(work,charged);
        assert_eq!(batched(wire,expected.len(),window,chunk,charged-1),Err(Error::Limit));
    }}
    let stored=include_bytes!("fixtures/stream-batch-stored.zlib");
    let expected:Vec<u8>=(0..130001).map(|i|((i*73+(i/97)*19+i/4093)&255) as u8).collect();
    for window in [2048,65536] {for chunk in [257,32769] {
        assert_eq!(batched(stored,expected.len(),window,chunk,2_000_000).unwrap().0,expected);
    }}
}

#[test]
fn period_two_matches_preserve_phase_across_ring_and_output_boundaries(){
    let wire=include_bytes!("fixtures/period-two.zlib");
    let mut expected=vec![127];expected.extend((0..33284).map(|i|if i%2==0{31}else{0}));
    let fee=2_000_000;let mut scalar=Inflater::new(wire,expected.len()).unwrap();
    let mut budget=Budget{remaining:fee};let (mut at,mut bit)=(2,0);let mut reference=Vec::new();
    loop {
        let (a,b,event,byte)=scalar.step(wire,at,bit,wire.len()-4,&mut budget).unwrap();at=a;bit=b;
        if event==1{reference.push(byte);}if event==2{break;}
    }
    assert_eq!(reference,expected);let charged=2*(fee-budget.remaining);
    for chunk in [1,3,255,256,257,32769] {
        let (actual,work)=batched(wire,expected.len(),1031,chunk,charged).unwrap();
        assert_eq!(actual,expected);assert_eq!(work,charged);
        assert_eq!(batched(wire,expected.len(),1031,chunk,charged-1),Err(Error::Limit));
    }
    for needed in [1,32769,expected.len()] {
        let mut out=vec![0xa5;needed+13];let fee=verified_tiff::native::work_limit(wire.len(),expected.len()).unwrap();
        let mut budget=Budget{remaining:fee};
        verified_tiff::native::decode(wire,8,1,needed,expected.len(),&mut out,&mut budget).unwrap();
        assert_eq!(&out[..needed],&expected[..needed]);assert_eq!(budget.remaining,0);
        assert!(out[needed..].iter().all(|b|*b==0xa5));
    }
}
