#![feature(map_first_last)]
#![feature(btree_drain_filter)]

use std::collections::{BTreeMap,BTreeSet};
use std::convert::TryInto;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::RwLock;
use std::sync::atomic::{AtomicU64, Ordering};
use itertools::Itertools;

#[derive(thiserror::Error, Debug)]
pub enum Error {
	#[error("I/O error: {0}")] Io(#[from] std::io::Error),
	#[error("Tried to write keys in non-lexicographic order")] Order,
	#[error("Cannot add to a tree that is finished writing")] Finished,
	#[error("String contains invalid UTF-8: {0}")] Utf8(#[from] std::string::FromUtf8Error),
	#[error("Values must be smaller than 2**60")] Range,
	#[error("Error in Sled database: {0}")] Sled(#[from] sled::Error),
}

type Result<T> = std::result::Result<T, Error>;

type Value = u64;

const DEFLAG: Value = 0x0FFFFFFFFFFFFFFF;
const FLAG_PARTIAL: Value = 0x1000000000000000;

fn diff_start(base: &str, check: &str) -> usize {
	let mut i = 0;
	for (base_char, check_char) in base.chars().zip(check.chars()) {
		if base_char != check_char { break; }
		i += base_char.len_utf8();
	}
	i
}

fn normalize(s: &str) -> String {
	s.to_lowercase()
}

enum WriteNode {
	Written(u64),
	Unwritten(Vec<Value>, BTreeMap<String, Box<WriteNode>>),
}

impl WriteNode {
	fn empty() -> Self {
		Self::Unwritten(vec![], BTreeMap::new())
	}

	fn from_value(val: Value) -> Box<Self> {
		Box::new(Self::Unwritten(vec![val], BTreeMap::new()))
	}

	fn from_children(children: Vec<(String, Box<Self>)>) -> Box<Self> {
		Box::new(Self::Unwritten(vec![], children.into_iter().collect()))
	}

	fn write(&mut self, out: &mut dyn Write, offset: &mut u64) -> Result<()> {
		if let WriteNode::Unwritten(values, children) = self {
			for (_, child) in children.iter_mut() { child.write(out, offset)?; }
			let mut bytes: Vec<u8> = vec![];
			bytes.extend(&(values.len() as u32).to_be_bytes());
			for value in values { bytes.extend(&value.to_be_bytes()); }
			bytes.extend(&(children.len() as u32).to_be_bytes());
			for (k, v) in children {
				bytes.extend(&(k.len() as u32).to_be_bytes());
				bytes.extend(k.as_bytes());
				match **v {
					WriteNode::Written(offset) => bytes.extend(&offset.to_be_bytes()),
					_ => panic!("Encountered unwritten child after writing all children"),
				}
			}
			let start = *offset;
			out.write_all(&bytes)?;
			*self = Self::Written(start);
			*offset += bytes.len() as u64;
		}
		Ok(())
	}

	fn add(&mut self, key: &str, val: Value, out: &mut dyn Write, offset: &mut u64) -> Result<()> {
		match self {
			WriteNode::Written(_) => Err(Error::Finished),
			WriteNode::Unwritten(values, children) => {
				if key == "" {
					assert!(children.is_empty());
					values.push(val);
				}
				else {
					match children.last_entry() {
						None => { children.insert(key.to_string(), Self::from_value(val)); },
						Some(mut entry) => {
							assert!(entry.key() != "");
							if key == entry.key() { entry.get_mut().add("", val, out, offset)?; }
							else {
								if key <= entry.key() { Err(Error::Order)?; }
								let splitidx = diff_start(entry.key(), key);
								if splitidx == entry.key().len() { return entry.get_mut().add(&key[splitidx..], val, out, offset); }
								else if splitidx == 0 {
									entry.get_mut().write(out, offset)?;
									children.insert(key.to_string(), Self::from_value(val));
								}
								else {
									let subkey = entry.key()[splitidx..].to_string();
									let mut subtree = entry.remove();
									subtree.write(out, offset)?;
									let newnode = Self::from_children(vec![(subkey, subtree), (key[splitidx..].to_string(), Self::from_value(val))]);
									children.insert(key[..splitidx].to_string(), newnode);
								}
							}
						},
					}
				}
				Ok(())
			}
		}
	}

	#[allow(dead_code)]
	fn debug_print(&self, prefix: &str) -> String {
		match self {
			WriteNode::Written(offset) => format!("*{}", offset),
			WriteNode::Unwritten(values, children) => {
				let mut ret = format!("{:?}", values.iter().map(|x| x & DEFLAG).collect::<Vec<u64>>());
				for (key, child) in children {
					ret += &format!("\n{}{:?} → {}", prefix, key, child.debug_print(&(prefix.to_string() + "  ")));
				}
				ret
			}
		}
	}
}

struct Writer<T: Write> {
	out: T,
	tree: WriteNode,
	offset: u64,
}

impl<T: Write> Writer<T> {
	fn new(file: T) -> Self {
		Self { out: file, tree: WriteNode::empty(), offset: 0 }
	}

	fn add(&mut self, key: &str, val: Value) -> Result<()> {
		//println!("{}\n", self.tree.print(""));
		self.tree.add(key, val, &mut self.out, &mut self.offset)?;
		Ok(())
	}

	fn finish(&mut self) -> Result<()> {
		self.tree.write(&mut self.out, &mut self.offset)?;
		match self.tree {
			WriteNode::Written(offset) => self.out.write_all(&offset.to_be_bytes())?,
			_ => panic!("Encountered unwritten child after writing all children"),
		}
		self.out.flush()?;
		Ok(())
	}
}

pub struct Builder {
	tmpfile: PathBuf,
	db: sled::Db,
	len: AtomicU64,
	finished: RwLock<bool>,
}

fn sled_append(_key: &[u8], old: Option<&[u8]>, new: &[u8]) -> Option<Vec<u8>> {
	let mut v = old.map(|x| x.to_vec()).unwrap_or(vec![]);
	v.extend(new);
	Some(v)
}

impl Builder {
	pub fn new<T: AsRef<Path>>(file: T) -> Result<Self> {
		let path = file.as_ref().to_path_buf();
		let db = sled::open(&path)?;
		db.set_merge_operator(sled_append);
		Ok(Self { tmpfile: path, db: db, len: AtomicU64::new(0), finished: RwLock::new(false) })
	}

	pub fn add(&self, key: &str, val: Value) -> Result<()> {
		let finished = self.finished.read().expect("Poisoned lock");
		if *finished { Err(Error::Finished)?; }
		if val & !DEFLAG != 0 { Err(Error::Range)?; }
		let val_bytes = val.to_be_bytes();
		let suffix_bytes = (val | FLAG_PARTIAL).to_be_bytes();
		let norm = normalize(key);
		self.db.merge(norm.clone(), &val_bytes)?;
		self.len.fetch_add(1, Ordering::Relaxed);
		let mut i = 0;
		for (a, b) in norm.chars().tuple_windows() {
			i += a.len_utf8();
			if !a.is_alphanumeric() && b.is_alphanumeric() || a.is_whitespace() && !b.is_whitespace() {
				self.db.merge(norm[i..].as_bytes(), &suffix_bytes)?;
				self.len.fetch_add(1, Ordering::Relaxed);
			}
		}
		Ok(())
	}

	pub fn build<T: Write>(&self, out: T, callback: Option<&dyn Fn(u64)>) -> Result<()> {
		*self.finished.write().expect("Poisoned lock") = true;
		let mut writer = Writer::new(out);
		let mut processed: u64 = 0;
		let mut progress = 0;
		let len = self.len.load(Ordering::Relaxed);
		for entry in self.db.range::<&[u8], _>(..) {
			let (keyvec, valvec) = entry?;
			let key = String::from_utf8(keyvec.to_vec()).expect("Wrote UTF-8 to Sled, but didn't get it back");
			let vals = valvec.into_iter().chunks(8).into_iter().map(|chunk| {
				let bytes = chunk.map(|x| *x).collect::<Vec<u8>>();
				u64::from_be_bytes(bytes.as_slice().try_into().expect("Wrote u64s to Sled, but didn't get them back"))
			}).sorted().unique();
			for val in vals {
				writer.add(&key, val)?;
				if let Some(f) = callback {
					processed += 1;
					let new_progress = processed * 1000 / len;
					if new_progress > progress {
						progress = new_progress;
						f(progress);
					}
				}
			}
		}
		writer.finish()?;
		Ok(())
	}
}

impl Drop for Builder {
	fn drop(&mut self) {
		if let Err(e) = std::fs::remove_dir_all(&self.tmpfile) {
			eprintln!("Failed to remove temporary database at {}: {}.  It will have to be cleaned up manually", self.tmpfile.display(), e);
		}
	}
}

unsafe impl Sync for Builder { }
unsafe impl Send for Builder { }

pub struct Tree<T: Read + Seek> {
	f: T,
}

impl<T: Read + Seek> Tree<T> {
	fn checked_read(&mut self, buf: &mut [u8]) -> Result<()> {
		self.f.read_exact(buf)?;
		Ok(())
	}

	fn read_u64(&mut self) -> Result<u64> {
		let mut buf: [u8; 8] = [0; 8];
		self.checked_read(&mut buf)?;
		Ok(u64::from_be_bytes(buf))
	}

	fn read_u32(&mut self) -> Result<u32> {
		let mut buf: [u8; 4] = [0; 4];
		self.checked_read(&mut buf)?;
		Ok(u32::from_be_bytes(buf))
	}

	fn read_str(&mut self) -> Result<String> {
		let len = self.read_u32()? as usize;
		let mut buf = vec![0; len];
		self.checked_read(&mut buf)?;
		Ok(String::from_utf8(buf)?)
	}

	pub fn new(file: T) -> Result<Self> {
		// TODO Optimizaiton ideas:
		//   - Store root offset to shave one read off each lookup
		//   - Maintain a cache of nodes in memory to reduce reads
		//   - Do binary search to find the right child rather than loading all children
		Ok(Self { f: file })
	}

	fn traverse(&mut self, offset: u64, query: &str) -> Result<Option<u64>> {
		if query == "" { return Ok(Some(offset)); }
		else {
			self.f.seek(SeekFrom::Start(offset))?;
			let num_values = self.read_u32()? as i64;
			self.f.seek(SeekFrom::Current(num_values * 8))?;
			let mut children: BTreeMap<String, u64> = BTreeMap::new();
			for _ in 0..self.read_u32()? { children.insert(self.read_str()?, self.read_u64()?); }
			if let Some((prev_key, target)) = children.range(..=query.to_string()).next_back() {
				if prev_key.len() <= query.len() && prev_key.as_str() == &query[..prev_key.len()] {
					return self.traverse(*target, &query[prev_key.len()..]);
				}
			}
			if let Some((next_key, target)) = children.range(query.to_string()..).next() {
				if next_key.len() > query.len() && query == &next_key[..query.len()] {
					return Ok(Some(*target));
				}
			}
			return Ok(None);
		}
	}

	fn find(&mut self, query: &str) -> Result<Option<u64>> {
		self.f.seek(SeekFrom::End(-8))?;
		let root = self.read_u64()?;
		Ok(self.traverse(root, &normalize(query))?)
	}

	fn subtree_closure(&mut self, offset: u64, ret: &mut BTreeSet<Value>) -> Result<()> {
		self.f.seek(SeekFrom::Start(offset))?;
		for _ in 0..self.read_u32()? { ret.insert(self.read_u64()? & DEFLAG); }
		let mut targets = vec![];
		for _ in 0..self.read_u32()? {
			let _ = self.read_str()?;
			targets.push(self.read_u64()?);
		}
		for target in targets { self.subtree_closure(target, ret)?; }
		Ok(())
	}

	pub fn search(&mut self, query: &str) -> Result<Vec<Value>> {
		let mut ret = BTreeSet::new();
		for word in query.split_whitespace() {
			match self.find(word)? {
				Some(node) => {
					let mut results = BTreeSet::new();
					self.subtree_closure(node, &mut results)?;
					if ret.is_empty() { ret.extend(results.into_iter()); }
					else { ret.drain_filter(|x| !results.contains(x)); }
					if ret.is_empty() { break; }
				},
				None => { break; },
			}
		}
		Ok(ret.into_iter().collect::<Vec<Value>>())
	}

	pub fn exact_search(&mut self, query: &str) -> Result<Vec<Value>> {
		match self.find(query)? {
			Some(node) => {
				self.f.seek(SeekFrom::Start(node))?;
				let mut ret = vec![];
				for _ in 0..self.read_u32()? {
					let value = self.read_u64()?;
					if value & FLAG_PARTIAL == 0 { ret.push(value & DEFLAG); }
				}
				Ok(ret)
			},
			None => Ok(vec![]),
		}
	}
}

/*#[test]
fn test() -> Result<()> {
	use std::fs::File;
	let mut out = File::create("/home/matt/code/squashserve/scratch/test.idx")?;
	let mut writer = Writer::new(out)?;
	for (k, v) in &[("aaa", 1), ("aaa", 2), ("aab", 3), ("ab", 4), ("abc", 5), ("baa", 6)] {
		writer.add(k, *v)?;
	}
	writer.finish()?;

	let mut f = File::open("/home/matt/code/squashserve/scratch/test.idx")?;
	let mut tree = Tree::new(f)?;
	assert_eq!(tree.search("aaa")?, vec![1, 2]);
	assert_eq!(tree.search("aa")?, vec![1, 2, 3]);
	assert_eq!(tree.search("ab")?, vec![4, 5]);
	assert_eq!(tree.search("c")?, vec![]);
	assert_eq!(tree.exact_search("aa")?, vec![]);
	assert_eq!(tree.exact_search("aaa")?, vec![1, 2]);
	assert_eq!(tree.exact_search("ab")?, vec![4]);
	
	Ok(())
}*/
