# ZSR Utils

## Motivation

ZSR attempts to provide an alternative to the [OpenZIM Project](http://www.openzim.org/wiki/OpenZIM) with slightly different aims.  The primary drawbacks of ZIM and Kiwix are that (a) ZIM files are optimized for access through the standalone application Kiwix, which essentially duplicates the functionality of a stripped-down web browser, and (b) converting ZIM files to and from other formats is difficult and lossy.  Kiwix is lacking in a lot of places -- for example, you can't have tabs open in multiple ZIM files simultaneously, and if you zoom a web page, you have to re-zoom every new tab you open.  Creating a ZIM file using zimwriterdb requires the user to install several Perl libraries and run a Postgresql server; extracting the original files with zimdump is simple enough, but the original directory structure is lost.  Additionally, all the internal links in the documents have to be rewritten to point to the `zim://` resources rather than their original targets, which is an iffy and imperfect process.

ZSR is designed to facilitate browsing archived websites with existing web browsers rather than a standalone application and the ability to extract the original, unmodified HTML tree from the archives.  The project provides an archive format that is used for storing mirrored sites and a C++ application that runs a small local webserver, allowing the user to browse the archives with their web browser of choice.


## Project Structure and Building

The following files are included in this project:

  - `util.cpp`, `mime.h`, `htmlent.h`, and `util.h`: Miscellaneous utility functions

  - `prefs.h`: Preferences manager

  - `http.cpp` and `http.h`: Various abstractions for HTTP and HTML used by zsrsrv

  - `compress.cpp` and `compress.h`: Wrappers around liblzma for stream compression and decompression

  - `zsr.cpp` and `zsr.h`: ZSR file format library

  - `mkvol.cpp`: Utility to create ZSR archives with the additional metadata necessary to be used with `zsrsrv`

  - `search.cpp` and `search.h`: On-disk radix tree library for fast title search

  - `zsrutil.cpp`: ZSR command-line utility

  - `Volume.cpp` and `Volume.h`: Class representing one volume for zsrsrv

  - `zsrsrv.cpp`: ZSR HTTP server

  - `zsrmnt.cpp`: Permits mounting ZSR archives as read-only FUSE filesystems

  - `accessories/wikidump.py` and `accessories/linkcheck.py`: Accessory programs to aid in the creation of ZSR files from wikis

  - `CMakeLists.txt`: Configuration for CMake

  - `LICENSE` and `README.md`: User information

Build requirements are as follows:

  - Build configuration is done with CMake.

  - A recent C++ compiler supporting C++14 is necessary.  I compiled with GCC 5.2.0.

  - I have made no effort to develop code that will work on anything but Linux on an x86_64 architecture.  I may broaden this a bit in the future.

  - The following libraries should be on most systems already: pthread, magic, lzma

  - The following library might need to be installed: [Mongoose](https://github.com/cesanta/mongoose) (`mongoose` in AUR)

Building should be as simple as:

    $ cmake .
    $ make


## File Format

The ZSR format is a general-purpose archive format designed to combine XZ compression with a random-access indexed scheme similar to that of ZIP.  It was designed specifically for this project, but there is no reason it could not be used more generally as well.  The file format was designed with these aims:

  - **Fast random access**, necessary if the files in the archive are to be served by a web server.  This was accomplished using an index of the files in the archive that is read into memory once and then used to access the compressed chunks.

  - **High compression**.  This was partially achieved by using XZ, which currently provides some of the best compression ratios available.  However, the effectiveness was decreased by the necessity of compressing each file separately to enable random access.  There is currently a modest improvement over ZIP, but I am looking for ways to further improve this ratio.

  - **Ability to reconstruct the original file tree**.  OpenZIM places all files in a flat system of "namespaces" and does not support hierarchical organization.  Since one of the aims of this project is to allow modification of the archive, it was necessary to have a format that conserved the original directory structure.

  - **Emphasis on read speed**.  Although writing archives is important, the intended scenario involves archives being read much more frequently than they are written.  The XZ compression used in the format is much slower than ZIP compression, and the archive must be completely decompressed and rewritten if changes are to be made, but there is a small size improvement and the lightweight nature of the format makes decompression fast enough to allow most pages to load within a second or two on my computer.

The ZSR format is fairly simple.  It consists of:

  - Header: the four ASCII bytes `!ZSR` followed by an eight-byte integer specifying the start byte of the index

  - Volume medatadata: the one-byte number of volume metadata, followed by the following for each datum:

      - The two-byte length of the key and the key itself

      - The two-byte length of the value and the value itself

  - File metadata: the one-byte number of file metadata, followed by the following for each datum:

      - The two-byte length of the key and the key itself

  - Data: the XZ-compressed archive data for the files, extending to the last byte before the index

  - The eight-byte number of index entries; that is, the total number of files and directories stored in the index

  - Index: an index entry for each file, consisting of:

      - The eight-byte ID of this entry's parent, or 0 for the root (index IDs are assigned in the order entries are stored in the index, starting with 0 for the root)

      - The one-byte type of the file: 1 for directory, 2 for regular file, 3 for symbolic link

      - The two-byte length of the entry's name and the name itself

      - If the entry is a *file*:

          - The eight-byte start position and length of the file's compressed data in the archive

          - The eight-byte size of the file when decompressed

          - For each file metadatum defined at the beginning of the archive:

              - The two-byte length of the value and the value itself

  - User data: arbitrary user-defined data may follow the archive.  (This is used, for example, to store the search index for volumes.)  This region of the file is made available to the user as a `std::istream` when the file is opened.

The encoder for the file format is implemented in zsr.cpp.  I provide a small utility for command-line compression and decompression of ZSR files in zsrutil.cpp.  Its usage is as follows:

    $ zsrutil c indir out.zsr

To decompress:

    $ zsrutil x in.zsr

To decompress a single file:

    $ zsrutil x in.zsr relative/path/to/file/within/archive

`zsrutil` can also be used to list files in the archive and metadata concerning the archive.

The encoder can also be used as a library in other C++ programs by including the header zsr.h.  The only classes you should have to deal with are `zsr::writer`, `zsr::archive`, and `zsr::iterator`.  Use as follows:

    #include <fstream>
    #include <streambuf>
    #include <sstream>
    #include <string>
    #include "zsr.h"

    zsr::writer wr{"/data/wikipedia"}; // Create an archive writer from a directory tree...
    std::ofstream out{"/data/volumes/wikipedia.zsr"};
    ar.write(out); // ...and write it out to an archive file.
    out.close();

    zsr::archive ar{std::ifstream{"/data/volumes/wikipedia.zsr"}}; // Open the archive file...
    ar.extract("wiki", "/data/wikipedia2"); // ...and extract the "wiki" subdirectory to a new location.
    if (ar.check("wiki/Douglas_Adams.html")) // If a certain file exists in the archive:
    {
        zsr::iterator it = ar.get("wiki/Douglas_Adams.html") // Get the archive node by path.
        std::string title = it.meta("title"); // Get metadata stored with the node.
        std::streambuf *file = it.open(); // Get the stream of the file's contents...
        std::ostringstream oss{};
        oss << file; // ...extract it...
        std::string article = oss.str(); // ...and convert it to a string.
    }
    ar.reap(); // Clean up file descriptors opened by zsr::iterator.open().


## Creating Volumes

To broawse an archived site, you need to create a "volume", which is just a ZSR archive of the HTML source tree with some metadata.  To begin with, all links to internal resources must be relative, because HTML pages are passed by the ZSR server to the user's web browser without modification.  There are plenty of tools that will rewrite links for you, so I decided it would be redundant to incorporate rewriting capabilities into this project.  If all internal links in the source tree are sound, they should also work in the final volume.

The only special attribute of a browsable ZSR volume is an additional directory named `_meta` in the root of the source tree.  (Woe be to that person who has to deal with a mirrored site that already has a directory called `_meta` in the root -- this directory name may be configurable in the future.)  This directory can contain arbitrary files related to the site -- for example, I like to include a `readme.txt` with personal notes on how I mirrored and prepared the site for archiving and an `update.sh` that will automatically mirror the latest version of the site.  The only files in `_meta` that are used by ZSR are `favicon.png`, a PNG image at least 48 by 48 pixels in size that is displayed in the volume list, and `info.txt`, which contains metadata about the archive.  These metadata  are in the format `key:value`, where `key` is composed of lower-case letters and `value` is any string that does not contain a newline.  Arbitrary metadata can be specified; ones specifically used by ZSR are "title", "description", and "home".  "home" is the relative path within the archive to the HTML file that is to serve as the volume's "home page".  This is the only key that is required.  Therefore, a minimal working ZSR volume might consist of the source tree plus a `_meta` directory containing an `info.txt` file with only the line `home:index.html`.  All metadata are made available as tokens to the HTML templates for the site, so it is really up to the user to decide what metadata the volumes should contain and then to modify the HTML templates to use those data.

Once the `_meta` directory has been filled with the appropriate material, the creation of a volume archive can be automated by running the `mkvol` binary:

    $ mkvol /data/wikipedia /data/volumes/wikipedia.zsr


## Web Server and FUSE Mounter

The main functionality of ZSR is in zsrsrv, a small program that acts as a web server, allowing the user to browse volumes from a web browser.  When launched for the first time (with `zsrsrv` -- there are no arguments), it will start a web server accepting requests on localhost port 2234.  Click on the gear icon to set which port to listen on and where the files are located.  Then click the circular arrow icon to refresh the list of volumes.  This can take a minute if you have many large volumes; be patient and the page will reload when it's done loading the volumes.  To create a new volume from a pristine source tree (without a `_meta` directory), click the plus icon, supply the requested information, and submit.  The archive will be created, which may take a long time, and the list of volumes will be reloaded.  To browse a volume, click on its title and you will be taken to the home page.

Volumes and standard ZSR archives can be mounted as read-only filesystems through FUSE using `zsrmnt`:

    $ zsrmnt /data/volumes/wikipedia.zsr ~/mnt

This will make the archive browsable like a normal directory.  Files and directory subtrees can be transparently extracted by copying them out of the mounted archive with a file manager or `cp`.  Unmount with `fusermount -u`:

    $ fusermount -u ~/mnt


## Searching

(In progress)


## So, What about Wikipedia?

(In progress)


## Accessory Tools

### Wikidump

While this project is more concerned with converting file trees to browsable archives than with creating the file trees in the first place, it does include a utility for copying Wikimedia sites (with plans to eventually support Wikia) to local storage.  It's only been tested on a couple of sites, so it's quite likely that it will do stupid things when applied to wikis that aren't similar enough to my test sites.  If it gives you problems on a certain site, please let me know and I'll look into fixing it.

Usage is as follows:

    ./wikidump.py -vv -s 2 http://en.wikipedia.org/wiki/Main_Page

With no verbosity arguments, Wikidump will just display a continuously updating count of the number of items in its queue, which can be misleading because the program eliminates duplicates on removal rather than insertion.  I like the output with a verbosity level of 2; you can increase this further to get debugging output.  *Please* ask the program to sleep between retrievals (`-s` option) to reduce server load if you aren't dumping your own installation.


### Linkcheck

Mirroring a website rarely goes perfectly, so along with Wikidump I've provided a small script that will check for missing links in an HTML tree.  Just provide the root of the tree as the first argument, and it will recursively check links to make sure that the `href` of every `a` element is reachable (only if it is a local link) and otherwise print out the file with the bad link and the link destination.  It also checks that all `img`s with a local `src` attribute are reachable.  Of course, it's far from perfect -- it doesn't (yet) verify links included through the `link` tag or local `script`s, and it will never be able to process, for example, links programmatically generated in JavaScript.  Still, for checking the output of Wikidump, at least, it can be useful.


## Issues

  - The toolbar display uses an iframe and a JavaScript harness that tries to be as transparent as possible.  Unfortunately, it's not perfect, and there may be times when GET parameters don't get passed through, or clicking a link makes the toolbar go away, or something is hiding under the toolbar and won't come out.  I am continually working on improving this system, and I welcome reports of page configurations that don't work nicely.

  - I've been focusing on getting the basic framework up and running, so my code has been a bit sloppy.  There are a lot of bugs marked `TODO` or `FIXME` in the source that I need to get around to fixing.  For now, it seems to work for most sane inputs.

  - Search functionality still needs a lot of work.

