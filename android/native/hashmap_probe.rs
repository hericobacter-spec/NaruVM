// CPU-only diagnostic for the standard collection/Arc operations at the crash site.
use std::{collections::HashMap, sync::Arc};
fn main() {
    let mut checked=0usize;
    for cycle in 0u64..2048 {
        let mut map: HashMap<[u8;32],(u64,Arc<[u64;8]>)>=HashMap::new();
        let mut held=Vec::new();
        for i in 0u64..257 {
            let mut key=[0u8;32];
            for (j,chunk) in key.chunks_exact_mut(8).enumerate() {
                chunk.copy_from_slice(&(i.wrapping_mul(17+j as u64)+cycle*1009).to_le_bytes());
            }
            let object=Arc::new([i^cycle;8]);
            if i%3==0 { held.push(Arc::clone(&object)); }
            map.insert(key,(i,object));
        }
        let mut gone=Vec::new();
        map.retain(|_,(i,object)| {
            assert_eq!(**object,[*i^cycle;8]); checked+=1;
            if Arc::get_mut(object).is_none() { return true; }
            gone.push((*i,Arc::clone(object))); false
        });
        assert_eq!(map.len(),held.len());
        gone.sort_by_key(|(i,_)|*i);
        drop(gone); drop(held);
        map.retain(|_,(_,object)| Arc::get_mut(object).is_none());
        assert!(map.is_empty());
    }
    println!("HASHMAP_CPU_RESULT checked={checked} cycles=2048 gpu=none guest=none pass=1");
}
