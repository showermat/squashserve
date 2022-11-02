# Squashserve

Squashserve is a server for browsing locally mirrored websites.  Conceptually, it's the same as browsing a local directory hierarchy of HTML files, but with added benefits:

  - The content is compressed into a single archive file for convenient sharing and space-saving.
  - Archives can optionally include search indices to enable fast searching by page title.

This is implemented by storing websites as single files called "volumes", which are simply SquashFS archives containing some metadata and on-disk indices.  Reading and writing SquashFS is accomplished with [squashfs-tools-ng](https://github.com/AgentD/squashfs-tools-ng), and indexing with [Sled](https://github.com/spacejam/sled).  This approach that scales easily to the size of Wikipedia (as of October 2020, an archive of 151 GiB easily browsable on a laptop).

## Installation and Configuration

Installation is currently manual.

 1. Install [squashfs-tools-ng](https://github.com/AgentD/squashfs-tools-ng) (for example, [from AUR](https://aur.archlinux.org/packages/squashfs-tools-ng)).
 2. Clone this repo.
 3. `cargo build --release`.
 4. Squashserve needs an archive of resources (HTML templates, fonts, etc.) to run.  This is just a normal SquashFS archive containing the files in `sqsrv/resources`.  Since you just installed `squashfs-tools-ng` in step 1, you can use it to build the archive: `gensquashfs -D sqsrv/resources resources.sfs`.  Store the created `resources.sfs` somewhere safe.

## Building a Volume

To build a volume, you need to obtain a directory tree of HTML files that is locally browsable (all links relative rather than absolute, for example).  Tools such as `wget` and HTTrack can do this for you.  Since a volume is simply a SquashFS file, you could pass such a directory tree through `mksquashfs` and the resulting file would be browsable by Squashserve.  However, it would be missing metadata and search indexing.

`mkvol` reads metadata and builds a search index while creating the volume.  To use it, create a `.meta` directory in the root of the HTML directory tree and create an `info.lua` within it.  In that file you can create in `info` table with arbitrary metadata:

    info = {
        title = "Wikipedia",
        description = "The free encyclopedia",
        language = "eng",
        created = "2020-09-13",
        source = "https://en.wikipedia.org",
        origin = "https://en.wikipedia.org;^wiki/./(.*).html$;wiki/$1",
        home = "wiki/M/Main_Page.html",
    }

Some of these are used by the Squashserve binary, if present.  Others are simply available to the HTML templates to display in the Web UI.  Including at least `title` and `home` is recommended.  `home` is the path to load when clicking on the volume in the library view.  `origin` is a pattern for transforming a path relative to the volume root into the URL of its source web page; if provided, the toolbar will provide a convenient link to jump to the live version of the page.

A `favicon.png` can also be placed in the `.meta` directory to be displayed in the library view and as the favicon when browsing.

Once the metadata have been added to the volume, running `mkvol <directory>` will start the creation process.

## Browsing

Create a config file at `$XDG_CONFIG_HOME/sqsrv/sqsrv.yaml` (usually `~/.config/sqsrv/sqsrv.yaml`) and use it to override any of the following settings as desired:

    basedir: .                  # Directory in which to look for volumes
    listen: 127.0.0.1           # Address on which to listen
    port: 2234                  # Port on which to listen
    resources: ./resources.sfs  # Path to resource archive
    complete: 40                # Number of results to expose in auto-completion
    title-results: 100          # Number of results per page in title search
    indices: true               # Whether to enable automatic index generation for directory paths
    toolbar: true               # Whether to enable the overlay toolbar on browsing pages
    #ssl:                       # Certificate details to enable SSL
    #    cert: /path/to/cert.pem
    #    key: /path/to/cert.key

Be sure to set the value of `resources` to the location of the resource archive built in the Installation section.

Then try running the `sqsrv` binary and visit `localhost:2234` (or the configured port) to view your volumes.

Volumes can be stored outside of the configured `basedir` and loaded on demand using `accessories/sqload`, which sends a request to the server asking it to load the specified file.  The volume will remain browsable until the volumes are refreshed or `sqsrv` is restarted.
