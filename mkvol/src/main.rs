#[macro_use] extern crate anyhow;
#[macro_use] extern crate clap;
extern crate encoding;
#[macro_use] extern crate lazy_static;
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
use chrono::prelude::*;

const VOLEXT: &str = "sfs";
const CLRLN: &str = "\r\x1b[2K";
const TITLE_IDX: &str = "titles.idx";

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
		(@arg METADIR: -m --metadir +takes_value "Use custom subdirectory for metadata, rather than .meta")
		(@arg TMPDIR: --tmpdir +takes_value "Store temporary files here, rather than in the same directory as the output file")
	).get_matches();

	let root = Path::new(args.value_of("DIR").expect("Missing required argument"));
	let outname = args.value_of("OUT").map(PathBuf::from).unwrap_or(root.with_extension(VOLEXT));
	let tmpdir = args.value_of("TMPDIR").map(PathBuf::from).unwrap_or(outname.parent().expect("Output file has no parent").to_path_buf());
	let metadir = OsString::from(args.value_of("METADIR").unwrap_or(".meta"));
	let mut writer = Writer::open(outname.clone()).with_context(|| format!("Unable to open output file {}", outname.display()))?;
	let inf = Info::new(&root, &metadir).with_context(|| format!("Couldn't load volume metadata in {}", root.join(&metadir).display()))?;
	let mut metasource: Option<SourceFile> = None;
	let mut rootsource: Option<SourceFile> = None;
	let mut processor = TreeProcessor::new(writer, root).with_context(|| format!("Failed to create tree processor for writing archive from {}", root.display()))?;
	let mut index = {
		let tmpf = tmpdir.join("titles.tmp");
		disktree::Builder::new(tmpf.clone()).with_context(|| format!("Couldn't open temporary file {} for title index", tmpf.display()))?
	};

	let mut stdout = std::io::stdout();
	let mut process = |mut source: SourceFile, relpath: PathBuf| -> Result<()> {
		inf.set_attrs(&source.path, &mut source.content.xattrs).with_context(|| "Failed to invoke Lua function to calculate attributes");
		source.content.xattrs.insert(OsString::from("user.name"), path_to_bytes(relpath.file_name()).with_context(|| format!("Failed converting file name of {} to bytes", relpath.display()))?);
		source.content.xattrs.insert(OsString::from("user.parent"), path_to_bytes(relpath.parent().map(|x| x.as_os_str())).with_context(|| format!("Failed converting file parent of {} to bytes", relpath.display()))?);
		if &relpath == &metadir { metasource = Some(source); }
		else if &relpath == Path::new("") {
			let extra_attrs = vec![
				("application_version", crate_version!().to_string()),
				("archived", Local::now().format("%Y-%m-%d").to_string()),
				("indexing", "disktree".to_string()),
				("metadir", metadir.clone().into_string().expect("Couldn't convert metadir to string")),
			];
			for (k, v) in extra_attrs { source.content.xattrs.insert(OsString::from("user.".to_string() + k), v.as_bytes().to_vec()); }
			rootsource = Some(source);
		}
		else if relpath == Path::new(&metadir).join(TITLE_IDX) {
			println!("{}Warning: Index file {} already exists in tree.  It will be ignored and overwritten.", CLRLN, relpath.display());
		}
		else {
			let maybe_title = source.content.xattrs.get(OsStr::new("user.title")).cloned();
			let id = processor.add(source).with_context(|| "Failed adding entry to archive")? as u64;
			if let Some(title) = maybe_title {
				if let Ok(utf_title) = String::from_utf8(title) {
					index.add(&utf_title, id).with_context(|| format!("Could not add title \"{}\" to title index", utf_title))?;
				}
			}
			print!("{}{} {}", CLRLN, id, relpath.display());
			stdout.flush();
		}
		Ok(())
	};
	for entry in processor.iter() {
		let mut source = entry.with_context(|| "Tree processor failed to handle file {}")?;
		let relpath = source.path.strip_prefix(root).expect("Directory walk encountered file not child of root").to_path_buf();
		if let Err(e) = process(source, relpath.clone()) {
			println!("{}Failed to process file {}: {}", CLRLN, relpath.display(), e); // TODO Some errors should probably cause us to abort
		}
	}
	println!();
	let meta = match metasource.take() {
		Some(real_meta) => real_meta,
		None => SourceFile { path: root.join(&metadir), content: Source::defaults(SourceData::Dir(Box::new(std::iter::empty()))) },
	};

	println!("Building search index...");
	let titles_tmp = tmpdir.join(TITLE_IDX);
	fn showprogress(permille: u64) {
		print!("{}{}%", CLRLN, permille as f64 / 10.0);
		std::io::stdout().flush();
	};
	index.build(&File::create(titles_tmp.clone()).with_context(|| format!("Could not create {} for temporary title index storage", titles_tmp.display()))?, Some(showprogress)).with_context(|| "Failed building title index")?; // TODO It would be nice if we could write the index directly to the archive
	print!("{}", CLRLN);
	let mut title_index = SourceFile { path: root.join(&metadir).join(TITLE_IDX), content: Source::defaults(SourceData::File(Box::new(File::open(&titles_tmp).with_context(|| format!("Could not read temporary title index {}", titles_tmp.display()))?))) };
	//title_index.content.flags |= BlockFlags::DontCompress | BlockFlags::DontFragment;

	println!("Writing special files...");
	for s in vec![title_index, meta, rootsource.expect("Root not encountered by tree processor")] {
		let path_display = s.path.display().to_string();
		processor.add(s).with_context(|| format!("Failed to add {} to archive", path_display))?;
	}
	processor.finish().with_context(|| "Finishing archive failed")?;
	std::fs::remove_file(&titles_tmp).with_context(|| format!("Could not remove temporary index file {}", titles_tmp.display()))?;
	Ok(())
}
