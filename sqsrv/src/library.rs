use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};
use anyhow::{Context, Result};
use itertools::Itertools;
use squashfs::read::{Archive, Node, OwnedFile, XattrType};

const VOLEXT: &str = "sfs"; // TODO Use in mkvol as well

pub mod tokens {
	use std::collections::HashMap;
	use std::path::PathBuf;
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

fn string_keyed_xattrs(node: &Node) -> Result<HashMap<String, Vec<u8>>> {
	Ok(node.xattrs(XattrType::User)?.into_iter()
		.filter_map(|(k, v)| String::from_utf8(k).ok().map(|x| (x, v)))
		.collect::<HashMap<String, Vec<u8>>>())
}

pub struct Info {
	original: HashMap<String, Vec<u8>>,
	pub id: String,
	pub title: String,
	pub metadir: PathBuf,
	pub favicon: PathBuf,
	pub home: PathBuf,
}

impl Info {
	pub fn new(id: &str, archive: &Archive) -> Result<Self> {
		fn get_str_def(info: &HashMap<String, Vec<u8>>, key: &str, def: &str) -> Result<String> {
			Ok(info.get(key).cloned().map(String::from_utf8).unwrap_or(Ok(def.to_string()))?)
		}
		let info = string_keyed_xattrs(&archive.get("")?.expect("Couldn't get volume root"))?;
		let metadir = PathBuf::from(get_str_def(&info, "metadir", ".meta")?);
		let home = PathBuf::from(get_str_def(&info, "home", "index.html")?); // TODO Check existence; also consider auto-generated indices
		let favicon_path = match info.get("icon") {
			Some(path) => PathBuf::from(&String::from_utf8(path.to_vec())?),
			None => metadir.join("favicon.png"),
		};
		let favicon = match archive.get(&favicon_path) { // TODO Should probably just use `icon` property and let template take care of the fallback
			Ok(Some(_)) => Path::new("/content").join(id.clone()).join(favicon_path),
			Ok(None) => PathBuf::from("/rsrc/img/volume.svg"),
			Err(e) => Err(e)?,
		};
		let title = get_str_def(&info, "title", id)?;
		Ok(Self {
			original: info,
			id: id.to_string(),
			title: title,
			metadir: metadir,
			favicon: favicon,
			home: home,
		})
	}

	fn overrides(&self) -> HashMap<String, String> {
		fn path_to_string(path: &Path) -> String {
			path.to_str().expect("Path is invalid UTF-8").to_string()
		}
		vec![
			("id".to_string(), self.id.clone()),
			("title".to_string(), self.title.clone()),
			("metadir".to_string(), path_to_string(&self.metadir)),
			("favicon".to_string(), path_to_string(&self.favicon)),
			("home".to_string(), path_to_string(&self.home)),
		].into_iter().collect::<HashMap<String, String>>()
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

//impl Serialize for Info {}

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

	fn index(&self) -> Result<Option<disktree::Tree<OwnedFile>>> {
		Ok(self.archive.get(".meta/titles.idx")?
			.and_then(|x| x.into_owned_file().ok())
			.and_then(|x| disktree::Tree::new(x).ok()))
	}

	fn resolve_path(&self, id: u64) -> Result<Option<(String, PathBuf)>> {
		fn attr_path(attrs: &HashMap<String, Vec<u8>>) -> Option<PathBuf> {
			let attr_str = |attr: &str| attrs.get(attr).and_then(|x| String::from_utf8(x.to_vec()).ok());
			match (attr_str("parent"), attr_str("name")) {
				(Some(parent), Some(name)) => Some(PathBuf::from(parent).join(name)),
				_ => None,
			}
		}
		let attrs = string_keyed_xattrs(&self.archive.get_id(id)?)?;
		Ok(match attr_path(&attrs) {
			Some(path) => match attrs.get("title").and_then(|x| String::from_utf8(x.to_vec()).ok()) {
				Some(title) => Some((title, path)),
				None => None,
			},
			None => None,
		})
	}

	pub fn exact_title(&self, query: &str) -> Result<Option<PathBuf>> {
		match self.index()? {
			None => Ok(None),
			Some(mut index) => {
				let results = index.exact_search(query)?;
				match &results.as_slice() {
					&[res] => Ok(self.resolve_path(*res)?.map(|x| x.1)),
					_ => Ok(None),
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
				let ret = results[start .. end].into_iter().map(|id| self.resolve_path(*id))
				.filter_map(|x| {
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
	order: u32,
	volumes: HashSet<String>,
}

pub struct Library {
	dir: PathBuf,
	all_volumes: HashMap<String, String>,
	categories: HashMap<String, Category>,
	volumes: RwLock<HashMap<String, Arc<Volume>>>,
}

impl Library { // TODO Test categories -- load and unload.  Make sure to implement external volumes
	pub fn new(dir: &Path, cat_assign: &Vec<super::CategoryConfig>) -> Result<Self> {
		let mut all_volumes = cat_assign.iter().flat_map(|cat| cat.volumes.iter().map(move |name| (name.to_string(), cat.name.to_string()))).collect::<HashMap<String, String>>(); // Map vol name -> cat name
		let cat_order = cat_assign.iter().enumerate().map(|(i, cat)| (cat.name.clone(), i as u32 + 1)).collect::<HashMap<String, u32>>(); // Map cat name -> cat order
		let mut categories = HashMap::new();
		for entry in dir.read_dir().with_context(|| format!("Library directory {} is inaccessible", dir.display()))? {
			let file = entry?.path();
			if file.is_file() && file.extension().and_then(|x| x.to_str()) == Some(VOLEXT) {
				let id = file.file_stem().and_then(|x| x.to_str()).ok_or(anyhow!("Could not get file name"))?.to_string();
				let catname = all_volumes.entry(id.clone()).or_insert("".to_string()).to_string();
				categories.entry(catname.to_string()).or_insert(Category {
					name: catname.clone(),
					loaded: RwLock::new(false),
					order: *cat_order.get(&catname).unwrap_or(&0),
					volumes: HashSet::new(),
				}).volumes.insert(id);
			}
		}
		let ret = Self { dir: dir.to_path_buf(), all_volumes: all_volumes, categories: categories, volumes: RwLock::new(HashMap::new()) };
		ret.load("")?;
		Ok(ret)
	}

	pub fn tokens(&self) -> Vec<tokens::Category> {
		self.categories.iter().map(|(id, cat)| (id, cat)).sorted_by(|first, second| first.1.order.cmp(&second.1.order)).map(|(id, cat)| {
			tokens::Category {
				id: id.to_string(),
				name: cat.name.to_string(),
				loaded: *cat.loaded.read().expect("Poisoned lock"),
				volumes: cat.volumes.iter().sorted().map(|id| self.volumes.read().expect("Poisoned lock").get(id).unwrap().info().string_values()).collect(),
			}
		}).collect()
	}
	
	pub fn load(&self, category: &str) -> Result<()> {
		match self.categories.get(category) {
			Some(cat) => {
				let mut volumes = self.volumes.write().expect("Poisoned lock");
				for volume in &cat.volumes {
					volumes.insert(volume.to_string(), Arc::new(Volume::new(volume, &self.dir.join(Path::new(volume).with_extension(VOLEXT)))?));
				}
				*cat.loaded.write().expect("Poisoned lock") = true;
			},
			None => Err(anyhow!("Category {} does not exist", category))?,
		}
		Ok(())
	}

	pub fn unload(&self, category: &str) {
		if let Some(cat) = self.categories.get(category) {
			for volume in &cat.volumes { let _ = self.volumes.write().expect("Poisoned lock").remove(volume); }
			*cat.loaded.write().expect("Poisoned lock") = false;
		}
	}

	pub fn load_external(&self, path: &Path) -> Result<String> {
		unimplemented!();
	}

	pub fn get(&self, id: &str) -> Result<Arc<Volume>> {
		let category = self.all_volumes.get(id).ok_or(anyhow!("Volume {} not found", id))?.clone(); // TODO If volume is already present, skip the load
		self.load(&category)?;
		Ok(self.volumes.read().expect("Poisoned lock").get(id).expect("Volume absent even after loading requisite category").clone())
	}
}
