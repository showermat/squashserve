#[macro_use] extern crate anyhow;
#[macro_use] extern crate clap;
extern crate encoding;
extern crate magic;
extern crate mlua;
extern crate squashfs;

mod info;

use std::cell::RefCell;
use std::collections::HashMap;
use std::convert::TryInto;
use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use info::Info;
use squashfs::write::{BlockFlags, Source, SourceData, SourceFile, TreeIterator, TreeProcessor, Writer};
use anyhow::{Context, Result};

fn test_xattrs(items: &[(&str, &str)]) -> HashMap<OsString, Vec<u8>> {
	items.iter().map(|(k, v)| (OsString::from(k), v.as_bytes().to_vec())).collect()
}

fn test_dirents(items: Vec<(&'static str, u32)>) -> Box<dyn Iterator<Item=(OsString, u32)>> {
	Box::new(items.into_iter().map(|(k, v)| (OsString::from(k), v)))
}

fn str_xattrs(xattrs: HashMap<Vec<u8>, Vec<u8>>) -> HashMap<String, String> {
	xattrs.into_iter().map(|(k, v)| (String::from_utf8(k).unwrap(), String::from_utf8(v).unwrap())).collect()
}

fn path_to_bytes(path: Option<&OsStr>) -> Result<Vec<u8>> {
	let empty = OsStr::new("");
	let unwrapped = path.as_ref().unwrap_or(&empty);
	Ok(unwrapped.to_str().ok_or(anyhow!("Path {:?} contains invalid UTF-8", unwrapped))?.as_bytes().to_vec())
}

fn main() -> Result<()> {
	let args = clap_app!(mkvol =>
		(version: crate_version!())
		(author: "Matthew Schauer")
		(about: "Create SquashFS volumes for Squashserve")
		(@arg DIR: +required "Directory to archive")
		(@arg OUT: -o +takes_value "Output file to create, or DIR.sfs by default")
		(@arg METADIR: -m --metadir +takes_value "Use custom subdirectory for metadata, rather than .meta") // TODO Support
		(@arg TMPDIR: --tmpdir +takes_value "Store temporary files here, rather than in the same directory as the output file") // TODO Support
	).get_matches();

	let root = Path::new(args.value_of("DIR").expect("Missing required argument"));
	let outname = args.value_of("OUT").map(PathBuf::from).unwrap_or(root.with_extension("sfs"));
	let tmpdir = args.value_of("TMPDIR").map(PathBuf::from).unwrap_or(outname.parent().expect("Output file has no parent").to_path_buf());
	let metadir = OsString::from(args.value_of("METADIR").unwrap_or(".meta"));
	let mut writer = Writer::open(outname)?;
	let inf = Info::new(&root, &metadir)?;
	let mut metasource: Option<SourceFile> = None;
	let mut rootsource: Option<SourceFile> = None;
	let mut processor = TreeProcessor::new(writer, root)?;
	let mut index = disktree::Builder::new(tmpdir.join("titles.tmp"))?;

	let mut stdout = std::io::stdout();
	for entry in processor.iter() {
		let mut source = entry?;
		let relpath = source.path.strip_prefix(root).expect("Directory walk encountered file not child of root").to_path_buf();
		inf.set_attrs(&source.path, &mut source.content.xattrs).unwrap();
		source.content.xattrs.insert(OsString::from("user.name"), path_to_bytes(relpath.file_name())?);
		source.content.xattrs.insert(OsString::from("user.parent"), path_to_bytes(relpath.parent().map(|x| x.as_os_str()))?);
		if &relpath == &metadir { metasource = Some(source); }
		else if &relpath == Path::new("") {
			let extra_attrs = vec![
				("squashserve_version", crate_version!()),
				("created", "1970-01-01"), // TODO
				("indexing", "disktree"),
			];
			for (k, v) in extra_attrs { source.content.xattrs.insert(OsString::from("user.".to_string() + k), v.as_bytes().to_vec()); }
			rootsource = Some(source);
		}
		else {
			let maybe_title = source.content.xattrs.get(OsStr::new("user.title")).cloned();
			let id = processor.add(source)? as u64;
			if let Some(title) = maybe_title {
				if let Ok(utf_title) = String::from_utf8(title) { index.add(&utf_title, id)?; }
			}
			print!("\r\x1b[2K{} {}", id, relpath.display());
			stdout.flush();
		}
	}
	println!();
	let meta = match metasource.take() {
		Some(real_meta) => real_meta,
		None => SourceFile { path: root.join(&metadir), content: Source::defaults(SourceData::Dir(Box::new(std::iter::empty()))) },
	};

	println!("Building search index...");
	let titles_tmp = tmpdir.join("titles.idx");
	index.build(&File::create(titles_tmp.clone())?)?; // TODO It would be nice if we could write the index directly to the archive
	let mut title_index = SourceFile { path: root.join(&metadir).join("titles.idx"), content: Source::defaults(SourceData::File(Box::new(File::open(&titles_tmp)?))) };
	title_index.content.flags |= BlockFlags::DontCompress | BlockFlags::DontFragment;

	println!("Writing special files...");
	for s in vec![title_index, meta, rootsource.expect("Root not encountered by tree processor")] { processor.add(s)?; }
	processor.finish()?;
	std::fs::remove_file(&titles_tmp)?;
	Ok(())
}
