#! /usr/bin/python

import sys;
import os;
import argparse;
import re;
import time;
import queue;
import hashlib;
import requests;
from urllib.parse import urlsplit, urlunparse, urljoin, unquote_plus, unquote;
from bs4 import BeautifulSoup as bs4;

def basename(path):
	return path.split("/")[-1];

def dirname(path):
	return "/".join(path.split("/")[:-1]);

def queryparse(query):
	ret = dict();
	for kvpair in query.split("&"):
		(key, val) = kvpair.split("=");
		ret[unquote_plus(key)] = unquote_plus(val);
	return ret;

# Get arguments
argp = argparse.ArgumentParser(description="Mirror a MediaWiki site in a form appropriate for local viewing");
argp.add_argument("-v", "--verbose", action="count", default=0, help="Enable verbose output");
argp.add_argument("-s", "--sleep", metavar="S", default=0, type=int, help="Sleep for S seconds between successive retrievals of resources");
argp.add_argument("-n", "--name", metavar="NAME", help="Explicitly set MediaWiki site name");
argp.add_argument("home", help="Home page of the site to mirror");
args = argp.parse_args();
if not re.search("^https?://.+/wiki/.+$", args.home): raise RuntimeError("Try a full path to a wiki page (http://.../wiki/Main_Page)");

# Miscellaneous setup
default_metapages=["Special", "Portal", "File", "Talk", "User", "Template", "User_talk", "Category_talk", "Template_talk", "User_blog", "Help", "Wikipedia", "MediaWiki", "Draft", "Wikipedia_talk"]; # Category
info = dict();
info["created"] = time.strftime("%Y-%m-%d");
info["refer"] = args.home;
recursive = True; # Set to false to prevent queueing of HTML pages for debug

# Determine site name
base = urlsplit(args.home);
wikitype = "mwiki";
if re.search("^.*.wikia.com$", base.netloc): wikitype = "wikia";
if wikitype == "wikia": raise RuntimeError("Wikia is not yet supported");
home = bs4(requests.get(args.home).text, "lxml");
if args.name: wikiname = args.name;
else:
	gen = home("meta", attrs={"name": "generator"});
	if len(gen) == 0 or not re.search("^MediaWiki.*$", gen[0]["content"]): raise RuntimeError("This does not appear to be a MediaWiki site");
	metapages = [];
	for link in home("a", href=True):
		page = basename(urlsplit(link["href"]).path);
		if ":" in page and "index.php" not in page:
			metaname = page[:page.find(":")];
			if metaname not in metapages and metaname != "Category": metapages.append(metaname);
	metapages = [ page for page in metapages if page not in default_metapages ];
	if len(metapages) != 1: raise RuntimeError("Could not reliably determine site name from %r (use -n to provide it)" % (metapages));
	wikiname = metapages[0];
info["title"] = wikiname;
if args.verbose >= 1: print("Site: %s (%s)" % (wikiname, wikitype));

# Set up directory structure
os.makedirs(wikiname, exist_ok=True);
os.chdir(wikiname);
for subdir in ["wiki", "rsrc", "img", "misc", "_meta"]: os.makedirs(subdir, exist_ok=True);
info["home"] = "wiki/" + re.sub("^/wiki/", "", base.path) + ".html";
info["origin"] = base.scheme + "://" + base.netloc;
infofile = open("_meta/info.txt", "w");
for (key, value) in info.items(): infofile.write(key + ":" + value + "\n");
infofile.close();
urls = queue.Queue();
urls.put(("wiki", args.home));

# Retrieve URLs BFS
while not urls.empty():
	if args.verbose == 0:
		print("\r\033[K%d" % (urls.qsize()), end="");
		sys.stdout.flush();
	(utype, url) = urls.get();
	if utype == "wiki":
		articlepath = re.search("^.*/wiki/(.*)$", urlsplit(url).path);
		if not articlepath:
			if args.verbose >= 1: print("Warning: Skipping non-conformant wiki URL %r" % (url));
			continue;
		fpath = os.path.join("wiki", articlepath.group(1) + ".html");
		mode = "w";
	elif utype == "css":
		fpath = os.path.join("rsrc", hashlib.md5(url.encode("UTF-8")).hexdigest() + ".css");
		mode ="w";
	elif utype == "js":
		fpath = os.path.join("rsrc", hashlib.md5(url.encode("UTF-8")).hexdigest() + ".js");
		mode ="w";
	elif utype == "img":
		fpath = os.path.join("img", basename(urlsplit(url).path));
		mode = "wb";
	elif utype == "misc":
		fpath = os.path.join("misc", basename(urlsplit(url).path));
		mode = "wb";
	else:
		if args.verbose >= 1: print("Warning: Item with unknown type %r skipped in queue" % (utype));
		continue;
	if os.path.exists(fpath):
		if args.verbose >= 3: print("Skipping %r because destination already exists" % (url));
		continue;
	depth = len(dirname(fpath).split("/"));
	if depth <= 1: prefix = "./";
	else: prefix = "../" * (depth - 1);
	# May want to truncate each path element to 200 chars
	os.makedirs(dirname(fpath), exist_ok=True);
	if args.verbose >= 2: print("GET %s" % (url));
	out = open(fpath, mode);
	try: r = requests.get(url);
	except Exception:
		if args.verbose >= 1: print("Warning: Could not fetch URL %r" % (url));
		continue;
	if r.status_code != 200:
		if args.verbose >= 1: print("Warning: Could not fetch URL %r" % (url));
		continue;
	if utype == "wiki":
		html = bs4(r.text, "lxml");
		article = html("div", id="content")[0].extract();
		article["style"] = "margin: 10px; border-width: 1px;";
		#html.body.clear(); # Can't do this -- bug?
		body = html.new_tag("body");
		html.body.replace_with(body);
		html.body.append(article);
		for ahref in html("a", href=True):
			href = unquote(ahref["href"]);
			hrefurl = urlsplit(urljoin(url, href));
			if args.verbose == 3: print("HREF %s" % (href));
			metamatch = re.search("^/wiki/([^:/]*):.*", hrefurl.path);
			if metamatch: metapage = metamatch.group(1);
			else: metapage = "";
			if re.search("^#", href):
				if args.verbose >= 4: print("Anchor link: %s" % (href));
			elif hrefurl.netloc != urlsplit(url).netloc or metapage == wikiname or metapage in default_metapages:
				if args.verbose >= 4: print("Passthrough link: %s" % (href));
				ahref["href"] = urlunparse(hrefurl);
			elif re.search("^/wiki/", hrefurl.path):
				if args.verbose >= 4: print("Proper wiki link: %s" % (href));
				ahref["href"] = re.sub("^/wiki/", prefix, hrefurl.path) + ".html";
				if args.verbose >= 5: print("Wiki link converted to local %s" % (ahref["href"]));
				if recursive: urls.put(("wiki", urlunparse(hrefurl)));
			elif re.search("^/load.php", hrefurl.path):
				if args.verbose >= 4: print("Loaded resource: %s" % (href)); # Make the poor assumption that it is CSS
				fhash = hashlib.md5(urlunparse(hreful).encode("UTF-8")).hexdigest();
				ahref["href"] = prefix + "../rsrc/" + fhash + ".css";
				urls.put(("css", urlunparse(hrefurl)));
			elif re.search("^/index.php", hrefurl.path):
				if args.verbose >= 4: print("Index link: %s" % (href));
				if not hrefurl.query:
					if args.verbose >= f: print("Index link with no query treated as passthrough");
					ahref["href"] = urlunparse(hrefurl);
				else:
					query = queryparse(urlsplit(urljoin(url, ahref["href"])).query);
					if "title" in query and "redlink" not in query:
						if args.verbose >= 5: print("Index link treated as wiki link to %s" % (query["title"]));
						ahref["href"] = prefix + query["title"] + ".html";
						if recursive: urls.put(("wiki", urljoin(url, "/wiki/" + query["title"])));
					else:
						if args.verbose >= 5: print("Index link %s treated as passthrough" % (href));
						ahref["href"] = urlunparse(hrefurl);
			#elif re.search("^/load.php", hrefurl.path): # TODO
			else:
				if args.verbose >= 4: print("Unknown local resource: %s" % (href));
				ahref["href"] = prefix + "../misc/" + basename(hrefurl.path);
				urls.put(("misc", urlunparse(hrefurl)));
		for imgsrc in html("img", src=True):
			img = unquote(imgsrc["src"]);
			imgurl = urlsplit(urljoin(url, img));
			if imgurl.scheme == "data": continue;
			imgsrc["src"] = prefix + "../img/" + basename(imgurl.path);
			if args.verbose >= 3: print("IMG %s -> %s" % (img, imgsrc["src"]));
			urls.put(("img", urlunparse(imgurl)));
		for linkhref in html("link", href=True, rel=True):
			href = unquote(linkhref["href"]);
			linkurl = urlsplit(urljoin(url, href));
			if args.verbose == 3: print("LINK %s" % (href));
			rel = " ".join(linkhref["rel"]);
			if rel == "stylesheet":
				if args.verbose >= 4: print("Capturing stylesheet link %s" % (href));
				fhash = hashlib.md5(urlunparse(linkurl).encode("UTF-8")).hexdigest();
				linkhref["href"] = prefix + "../rsrc/" + fhash + ".css";
				urls.put(("css", urlunparse(linkurl)));
			elif rel == "shortcut icon":
				if args.verbose >= 4: print("Capturing favicon link %s" % (href));
				linkhref["href"] = prefix + "../img/" + basename(linkurl.path);
				urls.put(("img", urlunparse(linkurl)));
			else:
				if args.verbose >= 4: print("Passing through %r link %s" % (linkhref["rel"][0], href));
				linkhref["href"] = urlunparse(linkurl);
		for scriptsrc in html("script", src=True):
			src = unquote(scriptsrc["src"]);
			scripturl = urlsplit(urljoin(url, src));
			if args.verbose >= 3: print("JS %s" % (src));
			fhash = hashlib.md5(urlunparse(scripturl).encode("UTF-8")).hexdigest();
			scriptsrc["src"] = prefix + "../rsrc/" + fhash + ".js";
			urls.put(("js", urlunparse(scripturl)));
		out.write(str(html));
	elif utype == "css":
		content = r.text;
		def url_rewrite(match):
			newurl = urlsplit(urljoin(url, match.group(1)));
			if newurl.scheme == "data": return "url(\"" + match.group(1) + "\")";
			if args.verbose >= 4: print("CSS linked resource: %s" % (match.group(1)));
			urls.put(("img", urlunparse(newurl))); # Some fonts and such might end up in img
			return "url(\"../img/" + basename(newurl.path) + "\")";
		content = re.sub("url\([\'\"]?([^\'\"\)]+)[\'\"]?\)", url_rewrite, content);
		out.write(content);
	elif utype == "js":
		out.write(r.text); # Don't even bother trying to change JS URLs
	elif utype == "img":
		out.write(r.content);
	elif utype == "misc":
		out.write(r.content);
	out.close();
	time.sleep(args.sleep);
print();

