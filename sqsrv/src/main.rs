#![feature(decl_macro)]

#[macro_use] extern crate anyhow;
extern crate itertools;
extern crate liquid;
extern crate owning_ref;
#[macro_use] extern crate rocket;
#[macro_use] extern crate serde_json;
extern crate squashfs;

mod library;

use std::collections::{BTreeMap, HashMap};
use std::convert::TryInto;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use library::{Library, Volume};
use anyhow::{Context, Result};
use liquid::Parser;
use owning_ref::OwningHandle;
use rocket::config::{Config as RocketConfig, Environment};
use rocket::http::ContentType;
use rocket::http::uri::{Formatter, FromUriParam, Path as UriPath, SegmentError, Segments, Uri, UriDisplay};
use rocket::request::FromSegments;
use rocket::response::{Redirect, Stream};
use rocket::response::content::{Content, Html, Json};
use rocket::State;
use serde::{Deserialize, Serialize};
use squashfs::read::{Archive, Data, Node, OwnedFile, XattrType};

struct UncheckedPath {
	path: PathBuf,
}

impl UncheckedPath {
	fn get(self) -> PathBuf {
		self.path
	}
}

impl<'a> FromSegments<'a> for UncheckedPath {
	type Error = SegmentError;

	fn from_segments(segments: Segments<'a>) -> Result<Self, Self::Error> {
		let mut ret = PathBuf::new();
		for segment in segments {
			let decoded = Uri::percent_decode(segment.as_bytes()).map_err(SegmentError::Utf8)?;
			if decoded == ".." { ret.pop(); }
			else if decoded.contains("/") { return Err(SegmentError::BadChar('/')); }
			else { ret.push(&*decoded); }
		}
		Ok(Self { path: ret })
	}
}

impl UriDisplay<UriPath> for UncheckedPath {
	fn fmt(&self, f: &mut Formatter<UriPath>) -> std::fmt::Result {
		self.path.fmt(f)
	}
}

impl FromUriParam<UriPath, PathBuf> for UncheckedPath {
	type Target = Self;

	fn from_uri_param(path: PathBuf) -> UncheckedPath {
		UncheckedPath { path: path }
	}
}

#[derive(Debug, Deserialize)]
struct SslConfig {
	cert: String,
	key: String,
}

#[derive(Debug, Deserialize)]
pub struct CategoryConfig {
	name: String,
	volumes: Vec<String>,
}

#[derive(Debug, Deserialize)]
#[serde(default = "Config::default")]
struct Config {
	basedir: PathBuf,
	port: u16,
	resources: PathBuf,
	listen: String,
	accept: Vec<String>,
	complete: u32,
	title_results: u32,
	toolbar: bool,
	ssl: Option<SslConfig>,
	categories: Vec<CategoryConfig>,
}

impl Config {
	fn default() -> Self {
		Self {
			basedir: PathBuf::from("."),
			port: 2234,
			resources: PathBuf::from("./resources.sfs"),
			listen: "127.0.0.1".to_string(),
			accept: vec![],
			complete: 40,
			title_results: 100,
			toolbar: true,
			ssl: None,
			categories: Vec::new(),
		}
	}

	fn new() -> Result<Self> {
		Ok(serde_yaml::from_reader(std::fs::File::open("sqsrv.yaml")?)?) // TODO Fallback order, and put file name in constant
	}
}

struct App {
	resources: Archive,
	config: Config,
	library: Library,
	parser: Parser,
}

impl App {
	fn resource<T: AsRef<Path>>(&self, path: T) -> Result<(OwnedFile, ContentType)> {
		let ctype = ContentType::from_extension(path.as_ref().extension().ok_or(anyhow!("Unknown resource extension"))?.to_str().ok_or(anyhow!("Invalid resource extension"))?).ok_or(anyhow!("Unknown resource content type"))?; // TODO Replace this here and below with xattr-based detection
		Ok((self.resources.get(&path)?.ok_or(anyhow!("File {} does not exist", path.as_ref().display()))?.into_owned_file()?, ctype)) // TODO Return 404
	}

	fn template<T: Serialize>(&self, template: &str, t: &T) -> Result<String> { // TODO
		let values = liquid::to_object(t)?;
		let source = self.resource(&format!("html/{}.html", template))?.0.to_string()?;
		Ok(self.parser.parse(&source)?.render(&values)?)
	}
}

pub struct ReadHandle<'a> {
	handle: OwningHandle<Arc<Volume>, OwnedFile<'a>>,
}

impl<'a> std::io::Read for ReadHandle<'a> {
	fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
		(*self.handle).read(buf)
	}
}

// TODO Custom error handler

#[derive(Responder)]
enum Response<'a> {
	Stream(Content<Stream<ReadHandle<'a>>>),
	Redirect(Redirect),
	Html(Html<String>),
	Json(Json<String>),
}

impl<'a> Response<'a> {
	fn redirect<T: TryInto<Uri<'static>>>(to: T) -> Self {
		Self::Redirect(Redirect::to(to))
	}

	fn html(s: String) -> Self {
		Self::Html(Html(s))
	}

	fn json<T: Serialize + ?Sized>(t: &T) -> Result<Self> {
		Ok(Self::Json(Json(serde_json::to_string(t)?)))
	}
}

#[derive(Debug, Serialize)]
struct TitleEntry {
	title: String,
	url: PathBuf
}

fn content_type(node: &Node) -> Result<ContentType> {
	use std::str::FromStr;
	let info = node.xattrs(XattrType::User)?.into_iter().map(|(k, v)| (String::from_utf8_lossy(&k).into_owned(), String::from_utf8_lossy(&v).into_owned())).collect::<HashMap<String, String>>();
	Ok(match info.get("type") {
		Some(ctype) => ContentType::from_str(ctype).map_err(|x| anyhow!("\"{}\" is not a valid MIME type: {}", ctype, x))?,
		None => {
			let ext = node
				.path().ok_or(anyhow!("Couldn't get file path to determine content type"))?
				.extension().ok_or(anyhow!("Unknown resource extension"))?
				.to_str().ok_or(anyhow!("Invalid resource extension"))?;
			ContentType::from_extension(ext)
				.ok_or(anyhow!("Unknown resource content type"))?
		}
	})
}

#[get("/rsrc/<path..>")]
fn rsrc(app: State<App>, path: UncheckedPath) -> Result<Content<Vec<u8>>> {
	let (mut file, ctype) = app.resource(&path.get())?;
	Ok(Content(ctype, file.to_bytes()?))
}

#[get("/")]
fn home(app: State<App>) -> Result<Response> {
	Ok(Response::html(app.template("home", &json!({"priv": true, "viewbase": "/view", "categories": app.library.tokens()}))?)) // TODO priv, viewbase
}

#[get("/content/<id>/<path..>")] // TODO Fragments and query params passed through?
fn content<'a>(app: State<'a, App>, id: String, path: UncheckedPath) -> Result<Response<'a>> {
	let pathbuf = path.get();
	let volume = app.inner().library.get(&id)?;
	match (*volume).get(&pathbuf) {
		Err(_) => Ok(Response::html("error getting file".to_string())), // TODO Nice error
		Ok(None) => Ok(Response::html("I'm a 404.".to_string())), // TODO 404
		Ok(Some(node)) => {
			match node.data()? {
				Data::File(_) => {
					let ctype = content_type(&node)?;
					let conv = |vol: *const Volume| -> Result<OwnedFile<'a>> { unsafe { Ok((*vol).get(&pathbuf)?.unwrap().into_owned_file()?) } }; // TODO This needs some work!
					let read = ReadHandle { handle: OwningHandle::try_new(volume, conv)? };
					Ok(Response::Stream(Content(ctype, Stream::from(read))))
				},
				Data::Symlink(target) => Ok(Response::redirect(uri!(content: id, PathBuf::from(target)))),
				_ => Err(anyhow!("Target {} is not a file or symlink"))?,
			}
		},
	}
}

#[get("/view/<id>/<path..>")]
fn view<'a>(app: State<'a, App>, id: String, path: UncheckedPath) -> Result<Response<'a>> {
	let pathbuf = path.get();
	// TODO Implement toolbar
	Ok(Response::redirect(uri!(content: id, pathbuf)))
}

#[get("/view/<id>")]
fn view_default<'a>(app: State<'a, App>, id: String) -> Result<Redirect> {
	let home = app.inner().library.get(&id)?.info().home.clone();
	Ok(Redirect::to(uri!(view: id, home)))
}

#[get("/titles/<id>?<q>&<p>")]
fn titles<'a>(app: State<'a, App>, id: String, q: String, p: Option<u32>) -> Result<Response<'a>> {
	let vol = app.library.get(&id)?;
	let page = std::cmp::max(p.unwrap_or(1), 1); // TODO Make "page 0" empty
	match vol.titles(&q, page, app.config.title_results)? {
		None => Err(anyhow!("This volume does not support searching"))?, // TODO
		Some((pages, res)) => {
			let full_res = res.into_iter().map(|(k, v)| TitleEntry { title: k, url: v }).collect::<Vec<_>>();
			Ok(Response::html(app.template("titles", &json!({"pages": pages, "page": page, "query": q, "viewbase": "/view", "volume": vol.info().string_values(), "results": full_res}))?))
		},
	}
}

#[get("/complete/<id>?<q>")]
fn complete<'a>(app: State<'a, App>, id: String, q: String) -> Result<Response<'a>> {
	match app.inner().library.get(&id)?.titles(&q, 1, app.config.complete)? {
		None => Ok(Response::json("[]")?),
		Some((pages, res)) => {
			let mut ret = res.into_iter().map(|(k, v)| TitleEntry { title: k, url: PathBuf::from("/view").join(&id).join(v) }).collect::<Vec<_>>();
			ret.push(TitleEntry { title: "<b>See all</b>".to_string(), url: PathBuf::from(uri!(titles: id, q, 1).to_string()) });
			Ok(Response::json(&ret)?)
		},
	}
}

#[get("/search/<id>?<q>")]
fn search<'a>(app: State<'a, App>, id: String, q: String) -> Result<Response<'a>> {
	let vol = app.library.get(&id)?;
	match vol.exact_title(&q)? {
		Some(path) => Ok(Response::redirect(uri!(view: id, path))),
		None => match vol.titles(&q, 1, 1)? {
			Some((1, res)) => Ok(Response::redirect(uri!(view: id, res.into_iter().next().expect("Can't get item from non-empty iterator").1))),
			_ => Ok(Response::redirect(uri!(titles: id, q, 1))),
		},
	}
}

#[get("/action/quit")]
fn quit<'a>(app: State<'a, App>) {
	// TODO Validate request is from localhost
	// TODO Can I send a response and then exit?
	std::process::exit(0);
}

fn main() -> Result<()> {
	let config = Config::new().with_context(|| "Couldn't read configuration")?;
	let library = Library::new(&config.basedir, &config.categories).with_context(|| "Couldn't load volumes")?;
	let resources = Archive::new(&config.resources).with_context(|| "Couldn't load resources")?;
	let parser = liquid::ParserBuilder::with_stdlib().build()?;
	let app = App {
		resources: resources,
		config: config,
		library: library,
		parser: parser,
	};
	let rocket_conf = RocketConfig::build(Environment::Development)
		.address(app.config.listen.clone())
		.port(app.config.port);
	let rocket_conf = match &app.config.ssl {
		Some(ssl) => rocket_conf.tls(ssl.cert.clone(), ssl.key.clone()),
		None => rocket_conf,
	};
	rocket::custom(rocket_conf.finalize()?)
		.manage(app)
		.mount("/", routes![content, home, quit, rsrc, view, view_default, titles, complete, search])
		.launch();
	Ok(())
}

/*fn main() -> Result<()> {
	let a = Archive::new("/home/matt/Scratch/wikivoyage.sfs")?;
	for entry in a.get("")?.as_dir().unwrap() {
		println!("{}", entry.name().unwrap());
	}
	let node = a.get("_meta/info.lua")?;
	let mut file = node.as_file().unwrap();
	println!("{} is {} bytes", node, file.size());
	println!("{}", file.to_string()?);
	for (k, v) in node.xattrs(squashfs::XattrType::User)? {
		println!("{}: {}", String::from_utf8_lossy(&k), String::from_utf8_lossy(&v));
	}
	let link = a.get("index.html")?;
	println!("{} points to {}", link, link.resolve()?);
	for i in 1..5 {
		println!("Node #{} is {}", i, a.get_id(i)?);
	}
	Ok(())
}*/
