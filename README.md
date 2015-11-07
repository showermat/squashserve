# ZSR Utils

## Motivation

ZSR attempts to provide an alternative to the [Open ZIM Project](http://www.openzim.org//wiki/OpenZIM) with slightly different aims.  The primary drawbacks of ZIM and Kiwix are that (a) ZIM files are optimized for access through the standalone application Kiwix, which essentially duplicates the functionality of a stripped-down web browser, and (b) converting ZIM files to and from other formats is difficult and lossy.  Kiwix is lacking in a lot of places -- for example, you can't have tabs open in multiple ZIM files simultaneously, and if you zoom a web page, you have to re-zoom every new tab you open.  Creating a ZIM file using zimwriterdb requires the user to install several Perl libraries and run a Postgresql server; extracting the original files with zimdump is simple enough, but the original directory structure is lost.  Additionally, all the internal links in the documents have to be rewritten to point to the `zim://` resources rather than their original targets, which is an iffy and imperfect process.

ZSR is designed to provide browsing with existing web browsers rather than a standalone application and the ability to extract the original, unmodified HTML tree from the archives.  The project contains an archive format that is used for storing mirrored sites and a C++ application that runs a small local webserver, allowing the user to browse the archives with their web browser of choice.

## Project Structure and Building

The following files are included in this project:

  - `util.cpp`, `mime.h`, and `util.h`: Miscellaneous utility functions

  - `prefs.h`: Preferences manager

  - `http.cpp` and `http.h`: Various abstractions for HTTP and HTML used by zsrsrv

  - `zsr.cpp` and `zsr.h`: ZSR file format library

  - `zsrutil.cpp`: ZSR command-line utility

  - `index.cpp`: Creates a Xapian index of the document tree to enable searching after compression

  - `Volume.cpp` and `Volume.h`: Class representing one volume for zsrsrv

  - `zsrsrv.cpp`: ZSR server

  - `accessories/wikidump.py` and `accessories/linkcheck.py`: Accessory programs to aid in the creation of ZSR files from wikis

  - `CMakeLists.txt`: Configuration for CMake

  - `LICENSE` and `README.md`: User information

Build requirements are as follows:

  - Build configuration is done with CMake.

  - A recent C++ compiler supporting C++14 is necessary.  I compiled with GCC 5.2.0.

  - I have made no effort to develop code that will work on anything but POSIX.  I think I use a couple features specific to Linux, but I would like to broaden that scope a little bit in the future.

  - The following libraries should be on most computers already: pthread, magic, lzma

  - The following libraries might need to be installed: [Mongoose](https://github.com/cesanta/mongoose) (`mongoose` in AUR), [Xapian](http://xapian.org) (`xapian-core` in extra)

Building should be as simple as:

    cmake .
    make

## File Format

The ZSR format is a general-purpose archive format designed to combine XZ compression with a random-access indexed scheme similar to that of ZIP.  It was designed specifically for this project, but there is no reason it could not be used more widely as well.  The file format was designed with these aims in mind:

  - **Fast random access**, necessary if the files in the archive are to be served by a web server.  This was accomplished using an index of the files in the archive that is read into memory once and then used to access the compressed chunks.

  - **XZ-based high compression**.  This was partially achieved by using XZ, which currently provides some of the best compression ratios available.  However, the effectiveness was decreased by the necessity of compressing each file separately to enable random access.  There is currently a modest improvement over ZIP, but I am looking for ways to further improve this ratio.

  - **Ability to reconstruct the original file tree**.  Open ZIM places all files in a flat system of "namespaces" and does not support hierarchical organization.  Since one of the aims of this project is to allow modification of the archive, it was necessary to have a format that conserved the original directory structure.

  - **Emphasis on read speed**.  Although writing archives is an important aim, the intended scenario involves archives being read much more frequently than they are written.  The XZ compression used in the format is much slower than ZIP compression, and the archive must be completely decompressed and rewritten if changes are to be made, but there is a small size improvement and the lightweight nature of the format makes decompression fast enough to allow most pages to load within a second or two on my computer.

The ZSR format is very simple.  It begins with the four ASCII bytes `!ZSR`, followed by an eight-byte integer specifying the start byte of the index.  After this follows an unspecified amount of XZ-compressed archive data, extending to the last byte before the index.  The remainder of the file consists of the index, which is a set of concatenated index entries.  There is one index entry for each file in the archive, formatted as follows: 8-byte ID of the file, 8-byte ID of the file's parent (0 if the file is the root), 8-byte start byte and length of the file's compressed data in the archive (both 0 for directories), the two-byte length of the file's name, and the file's name in UTF-8.  There are a few incompatible differences with the earlier, Python-implemented version of the archive format.  For example, the root is now explicitly included in the archive, and numbers are stored in the native byte order.

The encoder for the file format is implemented in zsr.cpp.  I provide a small utility for command-line compression and decompression of ZSR files in zsrutil.cpp.  Its usage is as follows:

    zsrutil c indir out.zsr

To decompress:

    zsrutil x in.zsr outdir

To decompress a single file:

    zsrutil in.zsr outfile relative/path/to/file/within/archive

The encoder can also be used as a library in other C++ programs by including the header zsr.h.  The only class you should have to deal with is `zsr::archive`.  Use as follows:

    #include <fstream>
    #include <zsr.h>

    zsr::archive ar{"/data/wikipedia"}; // Create a new archive from a directory tree...
    std::ofstream out{"/data/wikipedia.zsr"};
    ar.write(out); // ...and write it out to an archive file.
    out.close();

    zsr::archive ar2{"/data/wikipedia.zsr"}; // Open the archive file...
    ar2.extract("/data/wikipedia2"); // ...and extract it to a new location.
    if (ar2.check("wiki/Douglas_Adams.html")) // If a certain file exists in the archive...
        std::vector<char> article = ar2.get("wiki/Douglas_Adams.html") // ...then retrieve its contents.

## Creating Volumes

Individual archives of sets of associated web resources ("volumes") are just ZSR archives of the HTML source trees with some metadata.  To begin with, all links to internal resources must be relative, because HTML pages are passed by the ZSR server to the user's web browser without modification.  There are plenty of tools that will rewrite links for you, so I decided it would be redundant to incorporate rewriting capabilities into this project.  If all internal links in the source tree are sound, they should also work in the final volume (barring bugs in my software).

The only special attribute of a browsable ZSR volume is an additional directory named `_meta` in the root of the source tree.  (Woe be to that person who has to deal with a mirrored site that already has a directory called `_meta` in the root -- this directory name may become configurable in the future)  This directory can contain arbitrary files related to the site -- for example, I like to include a `readme.txt` with personal notes on how I mirrored and prepared the site for archiving and an `update.sh` that will automates mirroring of the latest version of the site.  The only files in `_meta` that are used by ZSR are `favicon.png`, a PNG image at least 48 by 48 pixels in size that is displayed in the volume list; `index`, a Xapian search index of the site (more on this later); and `info.txt`, which contains metadata about the archive.  These metadata  are in the format `key:value`, where `key` is composed of lower-case letters and `value` is any string that does not contain a newline.  Arbitrary metadata can be specified; ones specifically used by ZSR are "title", "description", and "home".  "home" is the relative path within the archive to the HTML file that is to serve as the volume's "home page".  This is the only key that is required.  Therefore, a minimal working ZSR volume might consist of the source tree plus a `_meta` directory containing an `info.txt` file with the line `home:index.html`.  All metadata are made available as tokens to the HTML templates for the site, so it is really up to the user to decide what metadata the volumes should contain and then to modify the HTML templates to use those data.

## Web Server

The main functionality of ZSR is in zsrsrv, a small program that acts as a web server, allowing the user to browse volumes from the web browser.  When launched for the first time (with `zsrsrv` -- there are no arguments), it will start a web server accepting requests on localhost port 2234.  Click on the gear icon to set which port to listen on and where the files are located.  Then click the circular arrow icon to refresh the list of volumes.  This can take a minute if you have many large volumes; be patient and the page will reload when it's done loading the volumes.  To create a new volume from a pristine source tree (without a `_meta` directory), click the plus icon, supply the requested information, and submit.  The archive will be created, which may take a long time, and the list of volumes will be reloaded.  To browse a volume, click on its title and you will be taken to the home page.

## Searching

zsrsrv uses Xapian to provide search functionality in its volumes.  This allows search terms to be entered in the search bars in the library view or the toolbar to look through pages in the volume.  It's still in its very early stages -- the searching is very basic, none too accurate, and only supports English, and the database takes up a lot of space -- but I hope to improve this over time.  For now, it's a lot better than nothing.

zsrsrv will make use of a standard Xapian index provided at `_meta/index` in the HTML file tree if it is available.  This can be built using `index` by passing the root directory of the file tree as the first argument.  It will iterate over all files in the tree with a text-type extension and index them.  That's all you need to enable searching.  If your ZSR file does not need to be searchable, you can simply skip the indexing process and create the archive without a `_meta/index` directory.  If you use zsrsrv's web interface to create a new ZSR file, it will be automatically indexed before compression.

## So, What about Wikipedia?

Of course, if you're going to be archiving sites for local browsing, Wikipedia is both the ultimate stress-test for the system and the first thing that everyone wants to have.  Of course, mirroring is out of the questions; I played around for a while with the XML database dumps that [Wikipedia provides](https://en.wikipedia.org/wiki/Wikipedia_database), but it seems that there are few good wiki-to-HTML converters out there, and none in Python (and besides, that doesn't take care of the images).  The best HTML archive of Wikipedia available, to my knowledge, is the ZIM file [provided by Kiwix](http://download.kiwix.org/portable/wikipedia_en_all.zip).  It proved time-consuming but not technically difficult to convert the ZIM archive to a locally browsable HTML directory.  From there, it should be a simple matter of indexing and compressing it.  Unfortunately, I've been having issues with Xapian running out of memory (It's not periodically flushing the index to disk?), and without search functionality it's not worth my time to continue.  I'm working on figuring it out and will update when I know more.

## Accessory Tools

### Wikidump

While this project is more concerned with converting file trees to browsable archives than with creating the file trees in the first place, it does include a utility for copying Wikimedia sites (with plans to eventually support Wikia) to local storage.  It's only been tested on a couple of sites, so it's quite likely that it will do stupid things when applied to wikis that aren't similar enough to my test sites.  If it gives you problems on a certain site, please let me know and I'll look into fixing it.

Usage is as follows:

    ./wikidump.py -vv -s 2 http://en.wikipedia.org/wiki/Main_Page

With no verbosity arguments, Wikidump will just display a continuously updating count of the number of items in its queue, which can be misleading because the program eliminates duplicates on removal rather than insertion.  I like the output with a verbosity level of 2; you can increase this further to get debugging output.  *Please* ask the program to sleep between retrievals (`-s` option) to reduce server load if you aren't dumping your own installation.

### Linkcheck

Mirroring a website rarely goes perfectly, so along with Wikidump I've provided a small script that will check for missing links in an HTML tree.  Just provide the root of the tree as the first argument, and it will recursively check links to make sure that the `href` of every `a` attribute is reachable (only if it is a local link) and otherwise print out the file with the bad link and the link destination.  It also checks that all `img`s with a local `src` attribute are reachable.  Of course, it's far from perfect -- it doesn't (yet) verify links included through the `link` tag or local `script`s, and it will never be able to process, for example, links programmatically generated in JavaScript.  Still, for checking the output of Wikidump, at least, it can be useful.

## Issues

  - The toolbar display uses an iframe and a JavaScript harness that tries to be as transparent as possible.  Unfortunately, it's not perfect, and there may be times when GET parameters don't get passed through, or clicking a link makes the toolbar go away, or something is hiding under the toolbar and won't come out.  I am continually working on improving this system, and I welcome reports of page configurations that don't work nicely.

  - I've been focusing on getting the basic framework up and running, so my code has been a bit sloppy.  There are a lot of bugs marked `TODO` or `FIXME` in the source that I need to get around to fixing.  For now, it should still work for most sane inputs.

  - Search functionality still needs a lot of work.

