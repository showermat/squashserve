use std::collections::HashMap;
use std::ffi::{OsStr, OsString};
use std::fmt::{Debug, Display, Error as FmtError, Formatter};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use anyhow::{Context, Result};
use regex::bytes::Regex;

const HEAD_SIZE: usize = 4096;
const DEFAULT_CHARSET: &str = "UTF-8";
const ATTR_NS: &str = "user";

fn head(path: &Path) -> Result<Vec<u8>> {
	let mut file = std::fs::File::open(&path)?;
	let mut content = Vec::with_capacity(HEAD_SIZE);
	unsafe { content.set_len(HEAD_SIZE); } // I wish there were a nicer way to get a large uninitialized Vec
	let size = file.read(&mut content)?;
	content.resize(size, 0);
	Ok(content)
}

#[derive(Debug)]
struct LuaError {
	err: Arc<mlua::Error>,
}

impl std::convert::From<mlua::Error> for LuaError {
	fn from(e: mlua::Error) -> Self {
		Self { err: Arc::new(e) }
	}
}

impl Display for LuaError {
	fn fmt(&self, f: &mut Formatter) -> Result<(), FmtError> {
		Display::fmt(&self.err, f)
	}
}

impl std::error::Error for LuaError { }
unsafe impl Send for LuaError { }
unsafe impl Sync for LuaError { }

type LuaResult<T> = std::result::Result<T, LuaError>;

fn mkluaerr<T: 'static + std::error::Error + Send + Sync>(err: T) -> mlua::Error {
	mlua::Error::ExternalError(Arc::new(err))
}

//fn luaanyhow<T: std::error::Error + Send + Sync + ?Sized>(err:
fn luaanyhow(err: anyhow::Error) -> mlua::Error {
	mlua::Error::ExternalError(Arc::from(Box::from(err)))
}

#[derive(Debug)]
enum InternalError {
	Utf8Path(PathBuf),
}

impl std::fmt::Display for InternalError {
	fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
		let msg = match self {
			Self::Utf8Path(path) => "Path contains invalid UTF-8: ".to_string() + &path.to_string_lossy(),
			
		};
		write!(f, "{}", msg)
	}
}

impl std::error::Error for InternalError { }


#[derive(Debug, Clone)]
struct File {
	path: PathBuf,
}

impl File {
	fn new(path: &Path) -> Self {
		Self { path: path.to_path_buf() }
	}
}

impl mlua::UserData for File {
	fn add_methods<'a, M: mlua::UserDataMethods<'a, Self>>(methods: &mut M) {
		fn utf8_err(path: &Path) -> mlua::Error {
			mlua::Error::ExternalError(Arc::new(InternalError::Utf8Path(path.to_path_buf())))
		}
		methods.add_method("path", |_, this, _: ()| {
			Ok(this.path.to_str().map(|x| x.to_string()).ok_or_else(|| utf8_err(&this.path))?)
		});
		methods.add_method("basename", |_, this, _: ()| {
			Ok(this.path.file_name().and_then(|x| x.to_str()).map(|x| x.to_string()).ok_or_else(|| utf8_err(&this.path))?)
		});
		methods.add_method("extension", |_, this, _: ()| {
			Ok(this.path.extension().and_then(|x| x.to_str()).map(|x| x.to_string()).ok_or_else(|| utf8_err(&this.path))?)
		});
		methods.add_method("stem", |_, this, _: ()| {
			Ok(this.path.file_stem().and_then(|x| x.to_str()).map(|x| x.to_string()).ok_or_else(|| utf8_err(&this.path))?)
		});
		methods.add_method("is_file", |_, this, _: ()| {
			Ok(this.path.is_file())
		});
		methods.add_method("is_dir", |_, this, _: ()| {
			Ok(this.path.is_dir())
		});
		methods.add_method("is_link", |_, this, _: ()| {
			Ok(this.path.symlink_metadata().map(|x| x.file_type().is_symlink()).map_err(mkluaerr)?)
		});
	}
}

fn mimetype(path: &Path) -> Result<String> {
	match mime_guess::from_path(&path).first() {
		Some(guess) => Ok(guess.essence_str().to_string()),
		None => {
			let magic = magic::Cookie::open(magic::flags::MIME_TYPE)?;
			magic.load(&["/usr/share/file/misc/magic"])?;
			Ok(magic.file(path)?)
		}
	}
}

fn is_html(path: &Path) -> bool {
	path.is_file() && mimetype(path).ok() == Some("text/html".to_string())
}

fn html_encoding(path: &Path) -> Result<String> {
	lazy_static! {
		static ref CHECKS: Vec<(Regex, usize)> = vec![
			(Regex::new(r#"(?i-u)<\s*(html|meta)\s+[^>]*charset\s*=\s*["']?([^"' \t]+)["']?[^>]*>"#).expect("HTML encoding check 1 does not compile"), 2), // <html|meta charset="...">
			(Regex::new(r#"(?i-u)<\s*meta\s+[^>]*content\s*=\s*["'][^"']+;\s+charset=([^ \t"']+)[^"']*["'][^>]*>"#).expect("HTML encoding check 2 does not compile"), 1), // <meta http-equiv="Content-Type" content="text/html; charset=...">
		];
	}
	let content = head(path)?;
	for (check, group) in CHECKS.iter() {
		if let Some(encoding) = check.captures(&content).and_then(|x| x.get(*group)).and_then(|x| String::from_utf8(x.as_bytes().to_vec()).ok()) {
			return Ok(encoding)
		}
	}
	Ok(DEFAULT_CHARSET.to_string())
}

fn to_utf8(input: &[u8], charset: &str) -> Result<String> {
	let encoder = encoding::label::encoding_from_whatwg_label(&charset).ok_or(anyhow!("Encoding {} not known", charset))?;
	Ok(encoder.decode(&input, encoding::DecoderTrap::Replace).map_err(|e| anyhow!("Failed to decode to {}", charset))?)
}

fn html_title_encoded(path: &Path, encoding: &str) -> Result<String> {
	lazy_static! {
		static ref TITLE_RE: Regex = Regex::new(r"(?i-u)<\s*title[^>]*>\s*(.*?)\s*<\s*/\s*title[^>]*>").expect("HTML title regex does not compile");
	}
	let content = head(path)?;
	let match_bytes = TITLE_RE.captures(&content).and_then(|x| x.get(1)).map(|x| x.as_bytes().clone()).unwrap_or(b"");
	let decoded = to_utf8(&match_bytes, encoding).unwrap_or("".to_string());
	Ok(html_escape::decode_html_entities(&decoded).into_owned())
}

fn html_title(path: &Path) -> Result<String> {
	html_title_encoded(&path, &html_encoding(&path)?)
}

fn lua_sub_html_title(lua: &mlua::Lua, (file, pattern): (File, String)) -> mlua::Result<mlua::Table> {
	let ret = lua.create_table()?;
	if is_html(&file.path) {
		let title = html_title(&file.path).map_err(luaanyhow)?;
		let regex = regex::Regex::new(&pattern).map_err(mkluaerr)?;
		let processed = regex.captures(&title).ok_or(anyhow!("Pattern \"{}\" does not capture a group", pattern)).map_err(luaanyhow)?.get(1).map(|x| x.as_str()).unwrap_or(&title);
		ret.set("title", processed.to_string())?;
	}
	Ok(ret)
}

fn table_to_map<'a, K, V: mlua::FromLua<'a>>(table: mlua::Table<'a>) -> LuaResult<HashMap<K, V>>
	where K: mlua::FromLua<'a> + std::cmp::Eq + std::hash::Hash {
	Ok(table.pairs::<K, V>().collect::<mlua::Result<HashMap<K, V>>>()?)
}

pub struct Info {
	root: PathBuf,
	lua: mlua::Lua,
}

impl Info {
	fn xattr_name(name: &str) -> OsString {
		OsString::from(ATTR_NS.to_string() + "." + name)
	}

	fn str_map_into_attrs(map: HashMap<String, String>) -> HashMap<OsString, Vec<u8>> {
		map.into_iter().map(|(k, v)| (Self::xattr_name(&k), v.into_bytes())).collect()
	}

	fn set_globals(lua: &mlua::Lua) -> LuaResult<()> {
		// TODO This needs some serious boilerplate reduction
		lua.globals().set("file", lua.create_function(|_, path: String| Ok(File::new(&Path::new(&path))))?)?;
		lua.globals().set("to_utf8", lua.create_function(|_, (input, encoding): (Vec<u8>, String)| Ok(to_utf8(&input, &encoding).map_err(luaanyhow)?))?)?;
		lua.globals().set("mimetype", lua.create_function(|_, file: File| Ok(mimetype(&file.path).map_err(luaanyhow)?))?)?;
		lua.globals().set("is_html", lua.create_function(|_, file: File| Ok(is_html(&file.path)))?)?;
		lua.globals().set("html_encoding", lua.create_function(|_, file: File| Ok(html_encoding(&file.path).map_err(luaanyhow)?))?)?;
		lua.globals().set("html_title_encoded", lua.create_function(|_, (file, encoding): (File, String)| Ok(html_title_encoded(&file.path, &encoding).map_err(luaanyhow)?))?)?;
		lua.globals().set("html_title", lua.create_function(|_, file: File| Ok(html_title(&file.path).map_err(luaanyhow)?))?)?;
		lua.globals().set("sub_html_title", lua.create_function(lua_sub_html_title)?)?;
		Ok(())
	}

	pub fn new(root: &Path, meta_dir: &OsStr) -> Result<Self> {
		let lua = mlua::Lua::new();
		Self::set_globals(&lua)?;
		let mut script = String::new();
		let path = [root, Path::new(meta_dir), Path::new("info.lua")].iter().collect::<PathBuf>();
		std::fs::File::open(&path)?.read_to_string(&mut script)?;
		lua.load(&script).exec().map_err(LuaError::from)?;
		Ok(Self { root: root.to_path_buf(), lua: lua })
	}

	pub fn set_attrs(&self, path: &Path, xattrs: &mut HashMap<OsString, Vec<u8>>) -> Result<()> {
		let calculated: HashMap<OsString, Vec<u8>> = if path == self.root {
			Self::str_map_into_attrs(self.lua.globals().get::<_, mlua::Table>("info").map(|x| table_to_map::<String, String>(x)).unwrap_or(Ok(HashMap::new()))?)
		}
		else {
			match self.lua.globals().get::<_, Option<mlua::Function>>("attrs").map_err(LuaError::from)? { // TODO Consider fetching this function once at creation and storing as a field -- this is tricky with lifetimes
				Some(f) => Self::str_map_into_attrs(table_to_map::<String, String>(f.call::<File, mlua::Table>(File::new(path)).map_err(LuaError::from)?).map_err(LuaError::from)?),
				None => HashMap::new(),
			}
		};
		xattrs.extend(calculated);
		if path.is_file() {
			let mime = String::from_utf8(xattrs.entry(Self::xattr_name("type")).or_insert(mimetype(&path)?.into_bytes()).to_vec()).unwrap_or("application/octet-stream".to_string());
			if mime == "text/html" {
				if let std::collections::hash_map::Entry::Vacant(entry) = xattrs.entry(Self::xattr_name("title")) {
					let _ = entry.insert(html_title(&path)?.into_bytes());
				}
			}
		}
		xattrs.retain(|k, v| v.len() > 0);
		Ok(())
	}
}
