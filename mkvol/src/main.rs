#[macro_use] extern crate anyhow;
#[macro_use] extern crate clap;
extern crate encoding;
#[macro_use] extern crate lazy_static;
extern crate magic;
extern crate mlua;
extern crate squashfs;

mod info;

use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use info::Info;
use squashfs::write::{Source, SourceData, SourceFile, TreeProcessor, Writer};
use anyhow::{Context, Result};
use chrono::prelude::*;
use std_semaphore::Semaphore;

const VOLEXT: &str = "sfs";
const CLRLN: &str = "\r\x1b[2K";
const TITLE_IDX: &str = "titles.idx";

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
		(@arg OUT: -o +takes_value "Output file to create, or DIR.sfs by default") // TODO Probably more standard to create in current directory by default
		(@arg METADIR: -m --metadir +takes_value "Use custom subdirectory for metadata, rather than .meta")
		(@arg TMPDIR: --tmpdir +takes_value "Store temporary files here, rather than in the same directory as the output file")
	).get_matches();

	let root = Path::new(args.value_of("DIR").expect("Missing required argument"));
	let outname = args.value_of("OUT").map(PathBuf::from).unwrap_or(root.with_extension(VOLEXT));
	let tmpdir = args.value_of("TMPDIR").map(PathBuf::from).unwrap_or(outname.parent().expect("Output file has no parent").to_path_buf());
	let metadir = OsString::from(args.value_of("METADIR").unwrap_or(".meta"));
	let writer = Writer::open(outname.clone()).with_context(|| format!("Unable to open output file {}", outname.display()))?;
	let inf = Arc::new(Mutex::new(Info::new(&root, &metadir).with_context(|| format!("Couldn't load volume metadata in {}", root.join(&metadir).display()))?)); // TODO How hard would it be to make Info thread-safe?  (With the requirement that the info.lua be threadsafe)
	let processor = Arc::new(TreeProcessor::new(writer, root).with_context(|| format!("Failed to create tree processor for writing archive from {}", root.display()))?);
	let metasource: Arc<Mutex<Option<SourceFile>>> = Arc::new(Mutex::new(None));
	let rootsource: Arc<Mutex<Option<SourceFile>>> = Arc::new(Mutex::new(None));
	let index = Arc::new({
		let tmpf = tmpdir.join("titles.tmp");
		disktree::Builder::new(tmpf.clone()).with_context(|| format!("Couldn't open temporary file {} for title index", tmpf.display()))?
	});
	let workers = threadpool::Builder::new().build();
	let limiter = Arc::new(Semaphore::new(workers.max_count() as isize * 2)); // Required pending https://github.com/rust-threadpool/rust-threadpool/pull/104
	let iter_processor = processor.clone();
	for entry in iter_processor.iter() {
		limiter.acquire();
		let limiter = limiter.clone();
		let source = entry.with_context(|| "Tree processor failed to handle file")?;
		let inf = inf.clone();
		let metadir = metadir.clone(); // TODO Is this inefficient?  Should I Arc it?  Probably not necessary, but...
		let relpath = source.path.strip_prefix(root).expect("Directory walk encountered file not child of root").to_path_buf();
		let metasource = metasource.clone();
		let rootsource = rootsource.clone();
		let processor = processor.clone();
		let index = index.clone();
		workers.execute(move || {
			let process = move |mut source: SourceFile, relpath: PathBuf| -> Result<()> {
				inf.lock().expect("Poisoned lock").set_attrs(&source.path, &mut source.content.xattrs).with_context(|| "Failed to invoke Lua function to calculate attributes")?;
				source.content.xattrs.insert(OsString::from("user.name"), path_to_bytes(relpath.file_name()).with_context(|| format!("Failed converting file name of {} to bytes", relpath.display()))?);
				source.content.xattrs.insert(OsString::from("user.parent"), path_to_bytes(relpath.parent().map(|x| x.as_os_str())).with_context(|| format!("Failed converting file parent of {} to bytes", relpath.display()))?);
				if &relpath == &metadir { *metasource.lock().expect("Poisoned lock") = Some(source); }
				else if &relpath == Path::new("") {
					let extra_attrs = vec![
						("application_version", crate_version!().to_string()),
						("archived", Local::now().format("%Y-%m-%d").to_string()),
						("indexing", "disktree".to_string()),
						("metadir", metadir.clone().into_string().expect("Couldn't convert metadir to string")),
					];
					for (k, v) in extra_attrs { source.content.xattrs.insert(OsString::from("user.".to_string() + k), v.as_bytes().to_vec()); }
					*rootsource.lock().expect("Poisoned lock") = Some(source);
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
					let stdout = std::io::stdout();
					let mut stdout_lock = stdout.lock();
					write!(stdout_lock, "{}{} {}", CLRLN, id, relpath.display())?;
					let _ = stdout_lock.flush();
				}
				Ok(())
			};
			if let Err(e) = process(source, relpath.clone()) {
				println!("Error processing {}: {}", relpath.display(), e); // TODO Some errors should probably cause us to abort
			}
			limiter.release();
		});
	}
	workers.join();
	println!();
	let meta = match metasource.lock().expect("Poisoned lock").take() {
		Some(real_meta) => real_meta,
		None => SourceFile { path: root.join(&metadir), content: Source::defaults(SourceData::Dir(Box::new(std::iter::empty()))) },
	};

	println!("Building search index...");
	let titles_tmp = tmpdir.join(TITLE_IDX);
	fn showprogress(permille: u64) {
		print!("{}{}%", CLRLN, permille as f64 / 10.0);
		let _ = std::io::stdout().flush();
	};
	index.build(&File::create(titles_tmp.clone()).with_context(|| format!("Could not create {} for temporary title index storage", titles_tmp.display()))?, Some(showprogress)).with_context(|| "Failed building title index")?; // TODO It would be nice if we could write the index directly to the archive
	print!("{}", CLRLN);
	let title_index = SourceFile { path: root.join(&metadir).join(TITLE_IDX), content: Source::defaults(SourceData::File(Box::new(File::open(&titles_tmp).with_context(|| format!("Could not read temporary title index {}", titles_tmp.display()))?))) };
	//title_index.content.flags |= BlockFlags::DontCompress | BlockFlags::DontFragment;

	println!("Writing special files...");
	for s in vec![title_index, meta, rootsource.lock().expect("Poisoned lock").take().expect("Root not encountered by tree processor")] {
		let path_display = s.path.display().to_string();
		processor.add(s).with_context(|| format!("Failed to add {} to archive", path_display))?;
	}
	processor.finish().with_context(|| "Finishing archive failed")?;
	std::fs::remove_file(&titles_tmp).with_context(|| format!("Could not remove temporary index file {}", titles_tmp.display()))?;
	Ok(())
}
