# ZSR Utils

## Motivation

ZSR attempts to provide an alternative to the [Open ZIM Project](http://www.openzim.org//wiki/OpenZIM) with slightly different aims.  The primary drawbacks of ZIM and Kiwix are that (a) ZIM files are optimized for access through the standalone application Kiwix, which essentially duplicates the functionality of a stripped-down web browser, and (b) converting ZIM files to and from other formats is difficult and lossy.  Kiwix is lacking in a lot of places -- for example, you can't have tabs open in multiple ZIM files simultaneously, and if you zoom a web page, you have to re-zoom every new tab you open.  Creating a ZIM file using zimwriterdb requires the user to install several Perl libraries and run a Postgresql server; extracting the original files with zimdump is simple enough, but the original directory structure is lost.  Additionally, all the internal links in the documents have to be rewritten to point to the `zim://` resources rather than their original targets, which is an iffy and imperfect process.

ZSR is designed with the following aims: a minimum of required packages (Python itself doesn't count!), browsing with existing web browsers rather than a standalone application, and the ability to extract the original, unmodified HTML tree from the archives.  The project contains an archive format thet is used for storing mirrored sites, and a Python script that runs a small local webserver, allowing the user to browse the archives with their web browser of choice.

## File Format

The ZSR format is a general-purpose archive format designed to combine XZ compression with a random-access indexed scheme similar to that of ZIP.  It was designed specifically for this project, but there is no reason it could not be used more widely as well.  The file format was designed with these aims in mind:

  - **Fast random access**, necessary if the files in the archive are to be served by a web server.  This was accomplished using an index of the files in the archive that is read into memory once and then used to access the compressed chunks.  In Python, the decompression of a randomly chosen file is comparable to that of the Python ZIP library.

  - **XZ-based high compression**.  This was partially achieved by using XZ, which currently provides some of the best compression ratios available.  However, the effectiveness was decreased by the necessity of compressing each file separately to enable random access.  There is currently a modest improvement over ZIP, but I am looking for ways to further improve this ratio.

  - **Ability to reconstruct the original file tree**.  Open ZIM places all files in a flat system of "namespaces" and does not support hierarchical organization.  Since one of the aims of this project is to allow modification of the archive, it was necessary to have a format that conserved the original directory structure.

  - **Emphasis on read speed**.  Although writing archives is an important aim, the intended scenario involves archives being read much more frequently than they are written.  The XZ compression used in the format is much slower than ZIP compression, and the archive must be completely decompressed and rewritten if changes are to be made, but there is a small size improvement and the lightweight nature of the format makes decompression fast enough to allow most pages to load within a second or two on my computer.

  - **Python library**, because the file reader is written in Python.  There were a couple other formats that looked promising (such as pixz), but the perfomance increase gained by being able to open the file once and store its index and the open filehandle in memory was more than enough to make up for this.

The ZSR format is very simple.  It begins with the four ASCII bytes `!ZSR`, followed by an eight-byte integer specifying the start byte of the index.  After this follows an unspecified amount of XZ-compressed archive data, extending to the last byte before the index.  The remainder of the file consists of the index, which is a set of concatenated index entries.  There is one index entry for each file in the archive, formatted as follows: 8-byte ID of the file, 8-byte ID of the file's parent (0 if the parent is the root, which is not explicitly included in the index), 8-byte start byte and length of the files' compressed data in the archive (both 0 for directories), the two-bit length of the file's name, and the file's name in UTF-8.  All values are stored litte-endian.

The encoder for the file format is implemented in about 150 lies of Python in zsr.py.  It can be called from the command line as follows.  To compress:

    zsr.py c indir out.zsr

To decompress:

    zsr.py x in.zsr outdir

To decompress a single file:

    zsr.py x in.zsr outfile relative/path/to/file/within/archive

The enocder can also be used as a library in other Python scripts.  The library defines the class `Zsr`, which is an object representing a single ZSR archive.  Use as follows:

    import zsr
    zsr.Zsr().create("/data/wikipedia", "wiki.zsr") # Creates a new archive from a file or directory
    archive = zsr.Zsr() # Create a new empty archive
    try: archive.read("wiki.zsr") # Read an archive from disk
    except zsr.BadZsr: exit(1) # zsr.BadZsr thrown on read error
    if archive.check("wiki/Python.html"): # Check whether a file exists in the archive
        print(archive.get("wiki/Python.html")) # Return the content of a single file
    archive.extract("/data/wikipedia2") # Extract the entire directory tree to a new directory

## Creating Volumes

Individual archives of sets of associated web resources ("volumes") are just ZSR archives of the HTML source trees with some metadata.  To begin with, all links to internal resources must be relative, because HTML pages are passed by the ZSR server to the user's web browser without modification.  There are plenty of tools that will rewrite links for you, so I decided it would be redundant to incorporate rewriting capabilities into this project.  If all internal links in the source tree are sound, they should also work in the final volume (barring bugs in my software).

The only special attribute of a browsable ZSR volume is an additional directory named `_meta` in the root of the source tree.  (Woe be to that person who has to deal with a mirrored site that already has a directory called `_meta` in the root!)  This directory can contain arbitrary files related to the site -- for example, I like to include a `readme.txt` with personal notes on how I mirrored and prepared the site for archiving and an `update.sh` that will automates mirroring of the latest version of the site.  The only two files in `_meta` that are used by ZSR are `favicon.png`, a PNG image at least 48 by 48 pixels in size that is displayed in the volume list, and `info.txt`, which contains metadata about the archive.  Once searching has been implemented, the search index will also reside here.  The metadata in `info.txt` are in the format "key:value", where key is composed of lower-case letters and value is any string that does not contain a newline.  Arbitrary metadata can be specified; ones specifically used by ZSR are "title", "description", and "home".  "home" is the relative path within the archive to the HTML file that is to serve as the volume's "home page".  This is the only key that is required.  Therefore, a minimal working ZSR volume might consist of the source tree plus a `_meta` directory containing an `info.txt` file with the line `home:index.html`.  All metadata are made available as tokens to the HTML templates for the site, so it is really up to the user to decide what metadata the volumes should contain and then to modify the HTML templates of the server to use those data.

## Web Server

The main functionality of ZSR is in zsrsrv, a small program that acts as a web server, allowing the user to browse volumes from the web browser.  When launched for the first time (with `zsrsrv` -- there are no arguments), it will start a web server accepting requests on localhost port 2234.  Click on the gear icon to set which port to listen on and where the files are located.  Then click the circular arrow icon to refresh the list of volumes.  This can take a minute if you have many large volumes; be patient and the page will reload when it's done loading the volumes.  To create a new volume from a pristine source tree (without a `_meta` directory), click the plus icon, supply the requested information, and submit.  The archive will be created, which may take a long time, and the list of volumes will be reloaded.  To browse a volume, click on its title and you will be taken to the home page.

## Issues

  - The toolbar display uses an iframe and a JavaScript harness that tries to be as transparent as possible.  Unfortunately, it's not perfect, and there may be times when GET parameters don't get passed through, or clicking a link makes the toolbar go away, or something is hiding under the toolbar and won't come out.  I am continually working on improving this system, and I welcome reports of page configurations that don't work nicely.

  - The search functionality -- which, I'll admit, is a pretty important part of this whole thing -- is, at the moment, totally unimplemented.  That's coming next.

  - Yes, I put semicolons in my Python code.  After so much C++, I can't help it.  It's not hurting you, is it?

