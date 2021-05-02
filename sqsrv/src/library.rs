use std::collections::{BTreeMap, HashMap, HashSet};
use std::ffi::OsStr;
use std::io::BufReader;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};
use super::{AppError, Result};
use itertools::Itertools;
use rocket::http::ContentType;
use rocket::http::uri::Uri;
use squashfs::read::{Archive, Node, OwnedFile, XattrType};
use thiserror::Error;
use walkdir::WalkDir;

const VOLEXT: &str = "sfs";

pub mod tokens {
	use std::collections::HashMap;
	use serde::Serialize;

	type Volume = HashMap<String, String>;

	#[derive(Debug, Serialize)]
	pub struct Category {
		pub id: String,
		pub name: String,
		pub loaded: bool,
		pub volumes: Vec<Volume>,
	}
}

fn string_keyed_xattrs(node: &Node) -> std::result::Result<HashMap<String, Vec<u8>>, squashfs::SquashfsError> {
	Ok(node.xattrs(XattrType::User)?.into_iter()
		.filter_map(|(k, v)| String::from_utf8(k).ok().map(|x| (x, v)))
		.collect::<HashMap<String, Vec<u8>>>())
}

pub fn path_encode(path: &Path) -> Result<PathBuf> {
	Ok(PathBuf::from(Uri::percent_encode(path.to_str().ok_or_else(|| AppError::PathUtf8(path.to_path_buf()))?).replace("%2F", "/")))
}

fn resolve_path(node: &Node, cached_attrs: Option<&HashMap<String, Vec<u8>>>) -> Result<Option<PathBuf>> {
	fn attr_path(attrs: &HashMap<String, Vec<u8>>) -> Option<PathBuf> {
		let attr_str = |attr: &str| attrs.get(attr).and_then(|x| String::from_utf8(x.to_vec()).ok());
		match (attr_str("parent"), attr_str("name")) {
			(Some(parent), Some(name)) => Some(PathBuf::from(parent).join(name)),
			_ => None,
		}
	}
	match node.path() {
		Some(path) => Ok(Some(path.to_path_buf())),
		None => match cached_attrs {
			Some(attrs) => Ok(attr_path(attrs)),
			None => {
				let attrs = string_keyed_xattrs(node)?;
				Ok(attr_path(&attrs))
			},
		},
	}
}

pub fn content_type(node: &Node) -> Result<ContentType> {
	use std::str::FromStr;
	let info = node.xattrs(XattrType::User)?.into_iter()
		.map(|(k, v)| (String::from_utf8_lossy(&k).into_owned(), String::from_utf8_lossy(&v).into_owned()))
		.collect::<HashMap<String, String>>();
	let debug_path = node.path().unwrap_or("<unknown>".as_ref());
	Ok(match info.get("type") {
		Some(ctype) => ContentType::from_str(ctype)
			.map_err(|x| AppError::ContentType(debug_path.to_path_buf(), format!("\"{}\" is not a valid MIME type: {}", ctype, x)))?,
		None => {
			let ext = node
				.path().ok_or(AppError::ContentType(debug_path.to_path_buf(), "Couldn't get file path to determine content type".to_string()))?
				.extension().ok_or(AppError::ContentType(debug_path.to_path_buf(), "Unknown extension".to_string()))?
				.to_str().ok_or(AppError::ContentType(debug_path.to_path_buf(), "Invalid extension".to_string()))?;
			ContentType::from_extension(ext)
				.ok_or(AppError::ContentType(debug_path.to_path_buf(), "Unknown content type".to_string()))?
		}
	})
}

pub struct Info {
	original: HashMap<String, Vec<u8>>,
	pub id: String,
	pub title: String,
	pub metadir: PathBuf,
	pub icon: Option<PathBuf>,
	pub home: Option<PathBuf>,
}

impl Info {
	pub fn new(id: &str, archive: &Archive) -> Result<Self> {
		fn get_str_def(info: &HashMap<String, Vec<u8>>, key: &str, def: &str) -> Result<String> {
			Ok(info.get(key).cloned().map(String::from_utf8).unwrap_or(Ok(def.to_string()))?)
		}
		fn check_file(archive: &Archive, path: PathBuf) -> Result<Option<PathBuf>> {
			match archive.get(&path)? {
				Some(node) => match node.resolve()? {
					None => Ok(None),
					Some(resolved) => match resolved.as_file() {
						Ok(_) => Ok(path_encode(&path).ok()),
						Err(_) => Ok(None),
					},
				},
				None => Ok(None),
			}
		}
		let info = string_keyed_xattrs(&archive.get("")?.expect("Couldn't get volume root"))?;
		let metadir = path_encode(&PathBuf::from(get_str_def(&info, "metadir", ".meta")?))?;
		let icon = check_file(archive, metadir.join("favicon.png"))?;
		let home = check_file(archive, PathBuf::from(get_str_def(&info, "home", "index.html")?))?;
		let title = get_str_def(&info, "title", id)?;
		Ok(Self {
			original: info,
			id: id.to_string(),
			title: title,
			metadir: metadir,
			icon: icon,
			home: home,
		})
	}

	fn overrides(&self) -> HashMap<String, String> {
		fn path_to_string(path: &Path) -> String {
			path.to_str().expect("Path is invalid UTF-8").to_string()
		}
		let mut ret = vec![
			("id".to_string(), self.id.clone()),
			("title".to_string(), self.title.clone()),
			("metadir".to_string(), path_to_string(&self.metadir)),
		].into_iter().collect::<HashMap<String, String>>();
		if let Some(icon) = &self.icon { ret.insert("icon".to_string(), path_to_string(&icon)); }
		if let Some(home) = &self.home { ret.insert("home".to_string(), path_to_string(&home)); }
		ret
	}

	pub fn get(&self, key: &str) -> Option<Vec<u8>> {
		match self.overrides().get(key) {
			Some(val) => Some(val.as_bytes().to_vec()),
			None => self.original.get(key).map(|x| x.to_vec()),
		}
	}

	pub fn string_values(&self) -> HashMap<String, String> { // This approach could use some improvement
		let mut ret = self.original.iter().filter_map(|(k, v)| String::from_utf8(v.to_vec()).ok().map(|x| (k.to_string(), x))).collect::<HashMap<String, String>>();
		ret.extend(self.overrides());
		return ret
	}
}

pub struct Volume {
	archive: Archive,
	info: Info,
}

impl Volume {
	pub fn new(id: &str, path: &Path) -> Result<Self> {
		let archive = Archive::new(path)?;
		Ok(Self { info: Info::new(&id, &archive)?, archive: archive })
	}

	pub fn info(&self) -> &Info {
		&self.info
	}

	pub fn get(&self, path: &Path) -> Result<Option<Node>> {
		Ok(self.archive.get(path)?)
	}

	fn index(&self) -> Result<Option<disktree::Tree<BufReader<OwnedFile>>>> {
		Ok(self.archive.get(".meta/titles.idx")?
			.and_then(|x| x.into_owned_file().ok())
			.and_then(|x| disktree::Tree::new(BufReader::new(x)).ok()))
	}

	pub fn random(&self, ctype: ContentType) -> Result<Option<PathBuf>> {
		let check = |id: u64| -> Result<Option<PathBuf>> {
			let node = self.archive.get_id(id)?;
			match node.is_file()? && resolve_path(&node, None)?.is_some() && content_type(&node)? == ctype {
				true => Ok(resolve_path(&node, None)?),
				false => Ok(None),
			}
		};
		let total = self.archive.size() as u64;
		for _ in 0..64 {
			let candidate = rand::random::<u64>() % total + 1;
			if let Some(res) = check(candidate)? { return Ok(Some(res)); }
		}
		let start = rand::random::<u64>() % total + 1;
		let mut candidate = start;
		loop {
			candidate = candidate % total + 1; // This won't work for archives with 1 inode, but I think that's okay.
			if candidate == start { break; }
			if let Some(res) = check(candidate)? { return Ok(Some(res)); }
		}
		return Ok(None);
	}

	pub fn exact_title(&self, query: &str) -> Result<Option<PathBuf>> {
		match self.index()? {
			None => Ok(None),
			Some(mut index) => {
				let results = index.exact_search(query)?.into_iter().map(|id| self.archive.get_id(id).unwrap()).collect::<Vec<_>>();
				match results.as_slice() {
					[] => Ok(None),
					[res] => resolve_path(res, None),
					nodes => {
						let titles = nodes.into_iter().filter_map(|node| {
							let attrs = string_keyed_xattrs(&node).unwrap();
							let title = attrs.get("title").and_then(|x| String::from_utf8(x.to_vec()).ok());
							match title {
								Some(t) => Some((t, node)),
								None => None,
							}
						}).collect::<HashMap<_, _>>();
						if let Some(res) = titles.get(query) { resolve_path(res, None) }
						else {
							match titles.into_iter().fold((0, None), |acc, x| {
								let distance = strsim::levenshtein(query, &x.0);
								if acc.1.is_none() || distance < acc.0 { (distance, Some(x.1)) }
								else { acc }
							}).1 {
								Some(closest) => resolve_path(closest, None),
								None => Ok(None),
							}
						}
					},
				}
			},
		}
	}

	pub fn titles(&self, query: &str, page: u32, page_size: u32) -> Result<Option<(u32, BTreeMap<String, PathBuf>)>> {
		match self.index()? {
			None => Ok(None),
			Some(mut index) => {
				let results = index.search(query)?;
				let pages = ((results.len() as f64) / (page_size as f64)).ceil() as u32;
				let start = std::cmp::min(((page - 1) * page_size) as usize, results.len());
				let end = std::cmp::min((page * page_size) as usize, results.len());
				let ret = results[start .. end].into_iter().map(|id| {
					let node = self.archive.get_id(*id)?;
					let attrs = string_keyed_xattrs(&node)?;
					match attrs.get("title").and_then(|x| String::from_utf8(x.to_vec()).ok()) {
						Some(title) => match resolve_path(&node, Some(&attrs))? {
							Some(path) => Ok(Some((title, path_encode(&path)?))),
							None => Ok(None),
						},
						None => Ok(None),
					}
				}).filter_map(|x| {
					match x {
						Ok(Some(y)) => Some(Ok(y)),
						Err(e) => Some(Err(e)),
						_ => None,
					}
				}).collect::<Result<BTreeMap<String, PathBuf>>>()?;
				Ok(Some((pages, ret)))
			}
		}
	}
}

#[derive(Debug)]
struct Category {
	name: String,
	loaded: RwLock<bool>,
	volumes: RwLock<HashSet<String>>,
}

impl Category {
	fn new(name: String) -> Self {
		Category { name: name, loaded: RwLock::new(false), volumes: RwLock::new(HashSet::new()) }
	}
}

#[derive(Error, Debug)]
pub enum LoadError {
	#[error("Library directory {0} is inaccessible: {0}")] Inaccessible(PathBuf, std::io::Error),
	#[error("I/O error while reading library directory: {0}")] Io(#[from] std::io::Error),
	#[error("Unable to extract file name from path {0}")] Filename(PathBuf),
}

pub struct Library {
	all_volumes: RwLock<HashMap<String, (PathBuf, String)>>, // Vol name -> (location, cat name)
	categories: HashMap<String, Category>,
	volumes: RwLock<HashMap<String, Arc<Volume>>>, // Loaded volumes only
}

impl Library {
	pub fn new(dir: &Path) -> std::result::Result<Self, LoadError> {
		let tree = WalkDir::new(&dir).follow_links(true);
		let mut all_volumes: HashMap<String, (PathBuf, String)> = HashMap::new();
		let mut categories: HashMap<String, Category> = HashMap::new();
		for entry in tree {
			match entry {
				Err(e) => {
					let path = e.path().map(|x| x.to_string_lossy().into_owned()).unwrap_or("(unknown)".to_string());
					eprintln!("Unable to read {}: {}", path, e.to_string());
				},
				Ok(file) => {
					if file.file_type().is_file() && file.path().extension() == Some(OsStr::new(VOLEXT)) {
						let cat = file.path().ancestors().skip(1).take(std::cmp::max(file.depth(), 1) - 1)
							.filter_map(|x| x.file_name()).map(|x| x.to_string_lossy()).collect::<Vec<_>>()
							.into_iter().rev().join(": ");
						let plain_id = file.path().file_stem().and_then(|x| x.to_str())
							.ok_or_else(|| LoadError::Filename(file.path().to_path_buf()))?.to_string();
						let mut id = plain_id.clone();
						let mut suffix = 0;
						while all_volumes.contains_key(&id) {
							suffix += 1;
							id = format!("{}-{}", plain_id, suffix);
						}
						all_volumes.insert(id.to_string(), (file.path().to_path_buf(), cat.clone()));
						categories.entry(cat.to_string()).or_insert_with(|| Category::new(cat.to_string())).volumes.write().expect("Poisoned lock").insert(id);
					}
				},
			}
		}
		let ret = Self { all_volumes: RwLock::new(all_volumes), categories: categories, volumes: RwLock::new(HashMap::new()) };
		Ok(ret)
	}

	fn category_tokens(&self, id: &str) -> Result<tokens::Category> {
		let cat = self.categories.get(id).ok_or(AppError::NoCategory(id.to_string()))?;
		let loaded = *cat.loaded.read().expect("Poisoned lock");
		Ok(tokens::Category {
			id: id.to_string(),
			name: cat.name.to_string(),
			loaded: loaded,
			volumes: match loaded {
				true => cat.volumes.read().expect("Poisoned lock").iter().sorted()
					.filter_map(|id| self.volumes.read().expect("Poisoned lock").get(id).map(|x| x.info().string_values()))
					.collect(),
				false => vec![],
			},
		})
	}

	pub fn tokens(&self) -> Result<Vec<tokens::Category>> {
		self.categories.iter().map(|(id, cat)| (&cat.name, id)).sorted().map(|(_order, id)| self.category_tokens(id)).collect()
	}

	pub fn loaded(&self, category: &str) -> Result<bool> {
		match self.categories.get(category) {
			Some(cat) => Ok(*cat.loaded.read().expect("Poisoned lock")),
			None => Err(AppError::NoCategory(category.to_string()))?,
		}
	}
	
	pub fn load(&self, category: &str) -> Result<tokens::Category> {
		match self.categories.get(category) {
			Some(cat) => {
				let mut volumes = self.volumes.write().expect("Poisoned lock");
				let all_volumes = self.all_volumes.read().expect("Poisoned lock");
				for volname in cat.volumes.read().expect("Poisoned lock").iter() {
					if !volumes.contains_key(volname) {
						let path = &all_volumes.get(volname).expect(&format!("Volume \"{}\" for category \"{}\" not in all volumes", volname, category)).0;
						match Volume::new(volname, &path) {
							Ok(volume) => { volumes.insert(volname.to_string(), Arc::new(volume)); },
							Err(e) => eprintln!("Failed to load volume {}: {}", volname, e),
						}
					}
				}
				*cat.loaded.write().expect("Poisoned lock") = true;
			},
			None => Err(AppError::NoCategory(category.to_string()))?,
		}
		Ok(self.category_tokens(category)?)
	}

	pub fn unload(&self, category: &str) {
		if let Some(cat) = self.categories.get(category) {
			for volume in cat.volumes.read().expect("Poisoned lock").iter() {
				let _ = self.volumes.write().expect("Poisoned lock").remove(volume);
			}
			*cat.loaded.write().expect("Poisoned lock") = false;
		}
	}

	pub fn get(&self, id: &str) -> Result<Arc<Volume>> {
		let category = self.all_volumes.read().expect("Posoned lock").get(id).ok_or(AppError::NoVolume(id.to_string()))?.1.clone();
		if !self.loaded(&category).expect("Category in all volumes doesn't exist") {
			self.load(&category)?; // Technically there's a race condition here, where the category could get unloaded again before the next line runs
		}
		Ok(self.volumes.read().expect("Poisoned lock").get(id).expect("Loaded category but contained volume does not exist").clone())
	}

	pub fn load_external(&self, path: &Path) -> Result<Arc<Volume>> {
		let id = format!("@ext:{:x}", rand::random::<u32>());
		let vol = Volume::new(&id, path)?;
		self.volumes.write().expect("Poisoned lock").insert(id.clone(), Arc::new(vol));
		self.all_volumes.write().expect("Poisoned lock").insert(id.clone(), (path.to_path_buf(), "".to_string()));
		self.categories.get("").expect("No default category").volumes.write().expect("Poisoned lock").insert(id.clone());
		Ok(self.get(&id)?)
	}
}
