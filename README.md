# ZSR Utils

## Motivation

ZSR attempts to provide an alternative to the [OpenZIM Project](http://www.openzim.org/wiki/OpenZIM) with slightly different aims.  The primary drawbacks of ZIM and Kiwix are that (a) ZIM files are optimized for access through the standalone application Kiwix, which essentially duplicates the functionality of a stripped-down web browser, and (b) converting ZIM files to and from other formats is difficult and lossy.  Kiwix is lacking in a lot of places -- for example, you can't have tabs open in multiple ZIM files simultaneously, and if you zoom a web page, you have to re-zoom every new tab you open.  Creating a ZIM file using `zimwriterdb` requires the user to install several Perl libraries and run a Postgresql server; extracting the original files with `zimdump` is simple enough, but the original directory structure is lost.  Additionally, all the internal links in the documents have to be rewritten to point to the `zim://` resources rather than their original targets, which is an iffy and imperfect process.

ZSR is designed to facilitate browsing archived websites with existing web browsers rather than a standalone application and the ability to extract the original, unmodified HTML tree from the archives.  The project provides an archive format that is used for storing mirrored sites and a C++ application that runs a small local webserver, allowing the user to browse the archives with their web browser of choice.


## Project Structure and Building

The following files are included in this project:

  - `util/*`: Miscellaneous utility functions
  - `http.cpp` and `http.h`: Various abstractions for HTTP and HTML used by zsrsrv
  - `compress.cpp` and `compress.h`: Wrappers around liblzma for stream compression and decompression
  - `zsr.cpp` and `zsr.h`: ZSR file format library
  - `mkvol.cpp`: Utility to create ZSR archives with the additional metadata necessary for use with zsrsrv
  - `search.cpp` and `search.h`: On-disk radix tree library for fast title search
  - `diskmap.h`: On-disk hash map variant for storing directory contents
  - `zsrutil.cpp`: ZSR command-line utility
  - `Volume.cpp` and `Volume.h`: Class representing one volume for zsrsrv
  - `zsrsrv.cpp`: ZSR HTTP server
  - `zsrmnt.cpp`: Permits mounting ZSR archives as read-only FUSE filesystems
  - `accessories/wikidump.py` and `accessories/linkcheck.py`: Accessory programs to aid in the creation of ZSR files from wikis
  - `lib/*`: Third-party libraries
  - `resources/*`: Web resources for zsrsrv
  - `CMakeLists.txt`: Configuration for CMake
  - `LICENSE` and `README.md`: User information

Build requirements are as follows:

  - Build configuration is done with CMake.

  - A recent C++ compiler supporting C++14 is necessary.  I compiled with GCC 5.2.0.

  - I have made no effort to develop code that will work on anything but Linux on an x86_64 architecture.  I may broaden this a bit in the future.

  - The following libraries should be on most systems already: pthread, magic, lzma, iconv

  - The following library might need to be installed: [Mongoose](https://github.com/cesanta/mongoose) (`mongoose` in AUR)

Building should be as simple as:

    $ cmake .
    $ make


## File Format

The ZSR format is a general-purpose archive format designed to combine XZ compression with a random-access indexed scheme similar to that of ZIP.  It was designed specifically for this project, but there is no reason it could not be used more generally as well.  The file format was designed with these aims:

  - **Fast random access**, necessary if the files in the archive are to be served by a web server.  This was accomplished using an index of the files in the archive that is read into memory once and then used to access the compressed chunks.

  - **High compression**.  This was partially achieved by using XZ, which currently provides some of the best compression ratios available.  However, the effectiveness was decreased by the necessity of compressing each file separately to enable random access.  There is currently a modest improvement over ZIP, but I am looking for ways to further improve this ratio.

  - **Total encapsulation**, so that a volume can be transferred as a single file.  OpenZIM separates the search index from the ZIM file, which requires each user to index files before they can be searched.  ZSR aims to encapsulate all data necessary to use the archive in a single file.

  - **Ability to reconstruct the original file tree**.  OpenZIM places all files in a flat system of "namespaces" and does not support hierarchical organization.  Since one of the aims of this project is to allow modification of the archive, it was necessary to have a format that conserved the original directory structure.

  - **Emphasis on read speed**.  Although writing archives is important, the intended scenario involves archives being read much more frequently than they are written.  The XZ compression used in the format is much slower than ZIP compression, and the archive must be completely decompressed and rewritten if changes are to be made, but there is a small size improvement and the lightweight nature of the format makes decompression fast enough to allow most pages to load within a second or two on my computer.

The ZSR format is fairly simple.  It consists of:

  - Header: the four ASCII bytes `!ZSR` followed by an eight-byte integer specifying the start byte of the index
  - Volume metadata: the one-byte number of volume metadata, followed by the following for each datum:
      - The two-byte length of the key and the key itself
      - The two-byte length of the value and the value itself
  - File metadata: the one-byte number of file metadata, followed by the following for each datum:
      - The two-byte length of the key and the key itself
  - Data: a data entry for each file, consisting of:
      - The eight-byte ID of this entry's parent, or 0 for the root (index IDs are assigned in the order entries are stored in the index, starting with 0 for the root)
      - The one-byte type of the file: 1 for directory, 2 for regular file, 3 for symbolic link
      - The two-byte length of the entry's name and the name itself
      - If the entry is a *file*:
          - For each file metadatum defined at the beginning of the archive:
              - The two-byte length of the value and the value itself
          - The eight-byte size of the file when decompressed
          - The length of the file's compressed data
          - The file's data, compressed in XZ format
      - If the entry is a *link*:
          - The eight-byte ID of the entry to which this link points
      - If the entry is a *directory*:
          - The eight-byte number of children of this directory
          - For each child, ordered by hash:
              - The eight-byte hash of the child's name
              - The eight-byte ID of the child
  - Index: one entry for each file, allowing fast random access to any file by ID
      - The eight-byte number of index entries; that is, the total number of files and directories stored in the index
      - For each file, the offset from the start of the data section to the start of the data entry for that file
  - User data: arbitrary user-defined data may follow the archive.  (This is used, for example, to store the search index for volumes.)  This region of the file is made available to the user as a `std::istream` when the file is opened.

The encoder for the file format is implemented in `zsr.cpp`.  I provide a small utility for command-line compression and decompression of ZSR files in `zsrutil.cpp`.  Its usage is as follows:

    $ zsrutil c indir out.zsr

To decompress:

    $ zsrutil x in.zsr

To decompress a single file:

    $ zsrutil x in.zsr relative/path/to/file/within/archive

`zsrutil` can also be used to list files in the archive and metadata concerning the archive.

The encoder can also be used as a library in other C++ programs by including the header `zsr.h`.  The only classes you should have to deal with are `zsr::writer`, `zsr::archive`, and `zsr::iterator`.  Use as follows:

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

To browse an archived site, you need to create a "volume", which is just a ZSR archive of the HTML source tree with some metadata.  To begin with, all links to internal resources must be relative, because HTML pages are passed by the ZSR server to the user's web browser without modification.  There are plenty of tools that will rewrite links for you, so I decided it would be redundant to incorporate rewriting capabilities into this project.  If all internal links in the source tree are sound, they should also work in the final volume.

The only special attribute of a browsable ZSR volume is an additional directory named `_meta` in the root of the source tree.  (Woe be to that person who has to deal with a mirrored site that already has a directory called `_meta` in the root -- this directory name may be configurable in the future.)  This directory can contain arbitrary files related to the site -- for example, I like to include a `readme.txt` with personal notes on how I mirrored and prepared the site for archiving and an `update.sh` that will automatically mirror the latest version of the site.  The only files in `_meta` that are used by ZSR are `favicon.png`, a PNG image at least 48 by 48 pixels in size that is displayed in the volume list, and `info.txt`, which contains metadata about the archive.  These metadata  are in the format `key:value`, where `key` is composed of lower-case letters and `value` is any string that does not contain a newline.  Arbitrary metadata can be specified; ones actually used by ZSR include "title", "description", and "home".  "home" is the relative path within the archive to the HTML file that is to serve as the volume's "home page".  This is the only key that is required.  Therefore, a minimal working ZSR volume might consist of the source tree plus a `_meta` directory containing an `info.txt` file with only the line `home:index.html`.  All metadata are made available as tokens to the HTML templates for the site, so it is really up to the user to decide what metadata the volumes should contain and then to modify the HTML templates to use those data.

Once the `_meta` directory has been filled with the appropriate material, the creation of a volume archive can be automated by running the `mkvol` binary:

    $ mkvol /data/wikipedia /data/volumes/wikipedia.zsr

`mkvol` will make use of two special metadata values, if they are present.  Because it extracts the titles from HTML pages to create the search index, it needs to know the encoding of the HTML files.  The `encoding` datum in `info.txt` can be used to set this encoding.  If it is not present, `mkvol` will assume UTF-8; any file that cannot be converted will be indexed by the name of the HTML file instead.  The `title_filter` metadatum allows the user to specify a regular expression to use to process the title before indexing.  The contents of the first regex group will be used as the title.  For example, supplying the title filter `^(.*) - Wikipedia` will cause the page titled `Douglas Adams - Wikipedia` to be indexed as just `Douglas Adams`.


## Web Server and FUSE Mounter

The main functionality of ZSR is in zsrsrv, a small program that acts as a web server, allowing the user to browse volumes from a web browser.  When launched for the first time (with `zsrsrv` -- there are no arguments), it will start a web server accepting requests on localhost port 2234.  Click on the gear icon to set which port to listen on and where the volume archives are located.  Then click the circular arrow icon to refresh the list of volumes.  To create a new volume from a pristine source tree (without a `_meta` directory), click the plus icon, supply the requested information, and submit.  The archive will be created, which may take a long time, and the list of volumes will be reloaded.  To browse a volume, click on its title and you will be taken to the home page.  Very large document trees may take several hours to archive; it is recommended to run `mkvol` from the command line yourself so that errors can be more easily diagnosed and resolved.

If there is a file named `categories.txt` in the same directory as the volume archives, it will be used to categorize the volumes.  The file should consist of one line for each category, with three fields separated by colons: the category ID (lower-case alphanumeric), the category name to be displayed, and a space-separated list of the volume IDs in that category.  (The ID of a volume is the filename without the final `.zsr`.)  Thus, to define a category containing the files `wiki1.zsr` and `wiki2.zsr`, `categories.txt` should contain a line like `cat1:Category Name:wiki1 wiki2`.  Archives will be loaded into memory only when their category is loaded by clicking on it in the volume list.

Volumes and standard ZSR archives can be mounted as read-only filesystems through FUSE using `zsrmnt`:

    $ zsrmnt /data/volumes/wikipedia.zsr ~/mnt

This will make the archive browsable like a normal directory.  Files and directory subtrees can be transparently extracted by copying them out of the mounted archive with a file manager or `cp`.  Unmount with `fusermount -u`:

    $ fusermount -u ~/mnt


## Searching

Earlier versions of zsrsrv provided full-content search with [Xapian](https://xapian.org/), but for volumes over one or two gigabytes, it proved impractical to bundle the search databases in the volume, so full-content search has been dropped indefinitely.  Instead, a search on page titles has been built into zsrsrv.  During volume creation, the titles of all HTML files in the tree are indexed in a serialized radix tree that is stored in the user data portion of the volume, and this radix tree is traversed on-disk when a query is made.  This method has proven quite efficient and scalable, taking up almost no memory and requiring few disk reads.  Every title is indexed multiple times, once starting after each non-word character, so searches will find any page where the query is at the beginning of a word.

The first 50 results of the search are placed in a typeahead box below the search input.  Selecting one of these will take the user directly to the matching article.  Pressing enter will take the user to the matching article if there is an article whose title matches the query exactly, or if there is only one article matching the query.  Otherwise, it displays a page with a full list of all matching articles.


## So, What about Wikipedia?

Of course, the holy grail of website archiving, and the most useful site to have, is Wikipedia.  A goal of this project was to enable efficient offline Wikipedia browsing suitable for replacing OpenZIM.  I have succeeded in creating a working volume from an HTML source tree of a May 2015 snapshot of the English Wikipedia, although the approach needs to be refined.  The raw source tree contains about 9.2 million files, both HTML and thumbnail images, and occupies about 178 GiB on disk.  With the current code, this compresses down to a 55.7-GiB volume that loads in 24 seconds from my SSD and occupies about 822 MiB of RAM once loaded.  Searching is almost instantaneous, and page loading takes less than one second, so the browsing experience is quite acceptable.  I am working now on decreasing the memory footprint and loading time of the archive.

The volume was created from the Wikipedia ZIM file because that is the best preprocessed HTML tree of Wikipedia that I could find for download.  The original ZIM file is 47.4 GiB in size and does not include search indexing.  Downloading, extracting, and cleaning the HTML tree took several days of processing; once a browsable source tree had been created, `mkvol` took 54.5 hours and 6.9 GiB of RAM to compile the volume from the source tree.  I lack the means to host the archive, but one can be created fairly easily by following the steps in `accessories/enwiki_zsr.sh`.  To be safe, the machine should have about 500 GiB of available disk space and at least 8 GiB of RAM.

The HTML tree for Wikipedia extracted from ZIM has plenty of problems of its own, concerning missing files, poor formatting, and others, in addition to having to perform time-costly renaming of many files.  I am working on a solution that generates an HTML source tree directly from the periodic database dumps that Wikipedia provides -- this will also allow the user to always have a copy of Wikipedia no more than a month old, rather than the once- or twice-a-year updates provided by OpenZIM.  This project will be updated as this course of inquiry progresses (or fails to progress).


## Accessory Tools

### Wikidump

While this project is more concerned with converting file trees to browsable archives than with creating the file trees in the first place, it does include a utility for copying Wikimedia sites (with plans to eventually support Wikia) to local storage.  It's only been tested on a couple of sites, so it's quite likely that it will do stupid things when applied to wikis that aren't similar enough to my test sites.  If it gives you problems on a certain site, please let me know and I'll look into fixing it.

Usage is as follows:

    ./wikidump.py -vv -s 2 http://en.wikipedia.org/wiki/Main_Page

With no verbosity arguments, Wikidump will just display a continuously updating count of the number of items in its queue, which can be misleading because the program eliminates duplicates on removal rather than insertion.  I like the output with a verbosity level of 2; you can increase this further to get debugging output.  *Please* ask the program to sleep between retrievals (`-s` option) to reduce server load if you aren't dumping your own installation.


### Linkcheck

Mirroring a website rarely goes perfectly, so along with Wikidump I've provided a small script that will check for missing links in an HTML tree.  Just provide the root of the tree as the first argument, and it will recursively check links to make sure that the `href` of every `a` element is reachable (only if it is a local link) and otherwise print out the file with the bad link and the link destination.  It also checks that all `img`s with a local `src` attribute are reachable.  Of course, it's far from perfect -- it doesn't (yet) verify links included through the `link` tag or local `script`s, and it will never be able to process, for example, links programmatically generated in JavaScript.  Still, for checking the output of Wikidump, at least, it can be useful.


## Credits

  - Except as indicated below, all content in this project is created solely by me and released under the Apache License version 2.0.

  - `lib/radix_tree*` are copyright 2010, Yuuki Takano, and have been modified to suit the needs of this project.  The originals can be obtained from <https://github.com/ytakano/radix_tree>.

  - `lib/json.hpp` is copyright 2013-2016, Niels Lohmann, and is released under the MIT License.  The original can be obtained from <https://github.com/nlohmann/json>.

  - `resources/fonts/fontawesome*` and `resources/css/font-awesome*.css` are from the Font Awesome project by Dave Gandy, released under the Creative Commons license 3.0.  The originals can be obtained from <http://fontawesome.io>.

  - `resources/js/jquery-2.1.4.min.js` is copyright, the jQuery Foundation, and is released under the terms of the jQuery License.  The original can be obtained from <https://github.com/jquery/jquery>.

  - `resources/icon/loading.gif` is retrieved from <https://commons.wikimedia.org/wiki/File:Ajax-loader.gif> and can be freely reused under the terms of the [WTFPL](http://www.wtfpl.net/).

