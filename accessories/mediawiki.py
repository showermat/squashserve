#! /usr/bin/python

import sys
import bs4
import re
import os.path
import shutil
import requests
import urllib.parse
import json
import time
import signal
import datetime
import subprocess
import threading
import concurrent.futures
import queue
import traceback
import hashlib

import tracing

# TODO
# It would be nice to download the category namespace for both Wikipedia and Wiktionary, but the HTML rendering appears to always be empty!
# Don't download redirects to pages that we're not downloading, like links to pages in namespaces
# Add a meta tag for the revision being retrieved, so in the future we can avoid refetching unchanged revisions (adds an extra GET and requires moving images; probably no faster)
# Eventually: postprocessing for redlinks?
#
# Data for October 2018:
# Wikipedia: downloaded in 21 days; 14133924 articles, 8418000 redirects, 6212208 images, all occupying 366 GB on disk; archiving took 8.2 days (7.4 for content) with memory usage up to 15.9 GB; resulting archive 132 GB
# Wiktionary: downloaded in 60 hours; 5857116 articles, 37992 redirects, 70341 images, all occupying 39 GB on disk; archiving took 31:15; resulting archive 8.76 GB
#
# Problem pages:
# ...

debug = False
debug_pages = ["Main Page", "China", "Hydronium", "Maxwell's equations", "Persimmon", "Alpha Centauri", "Boranes", "Busy signal",
	"Periodic table (large cells)", "Gallery of sovereign state flags", "Go (programming language)", "Foreign relations of China",
	"Askinosie Chocolate", "Art Pepper", "ThisShouldThrowAnException", "Tokyo", "Myeloperoxidase", "Ayu", "Apex predator",
	"Chongqing", "Implosive consonant", "São Paulo", "Yellowroot", "%$ON%", "Aluminum", "Aluminium", "PAD (control code)",
	"C0 and C1 control codes", "Peach", "PEACH"]

sites = {
	"wikipedia": ("Wikipedia", "The free encyclopedia", "https://en.wikipedia.org", "https://upload.wikimedia.org/wikipedia/commons/8/80/Wikipedia-logo-v2.svg", {0: ""}),
	"wiktionary": ("Wiktionary", "The free dictionary", "https://en.wiktionary.org", "https://upload.wikimedia.org/wikipedia/commons/e/ec/Wiktionary-logo.svg", {0: "", 100: "Appendix", 104: "Index", 110: "Thesaurus", 114: "Citations", 118: "Reconstruction"}),
	"wikivoyage": ("Wikivoyage", "Free worldwide travel guide", "https://en.wikivoyage.org", "https://en.wikivoyage.org/static/images/project-logos/enwikivoyage.png", {0: ""}),
}
rootdir = sys.argv[1]
if rootdir == "debug":
	debug = True
	rootdir = "wikipedia"
(site_name, site_description, origin_root, faviconsrc, namespaces) = sites[rootdir]

safe_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
concurrency = 16
origin = origin_root + "/wiki"
imgdir = "img"
oldimgdir = "img_old"
htmldir = "wiki"
metadir = "_meta"
infoname = "info.lua"
faviconname = "favicon.png"
cssname = "wikistyle.css"
tracefname = os.path.join(rootdir, "trace.svg")
logfname = os.path.join(rootdir, "dump.log")
resfpath = os.path.join(rootdir, "resume.txt")
useragent = "wikidump/0.4 robot contact:matthew.schauer@e10x.net (Please block temporarily and send e-mail if bandwidth limit exceeded)"

def timestr(format = "%Y-%m-%d %H:%M:%S"):
	return datetime.datetime.now().strftime(format)

class HTTPError(Exception):
	def __init__(self, code, url):
		self.code = code
		self.url = url
	def __str__(self):
		return "HTTP %d fetching %s" % (self.code, self.url)

def friendlyname(fname):
	subs = {"\"": "[quote]", "/": "[slash]", " ": "_", "_+": "_"}
	parts = fname.split("#", 1)
	for (char, sub) in subs.items(): parts[0] = re.sub(char, sub, parts[0])
	if parts[0][0] in safe_chars: first = parts[0][0]
	else: first = "_"
	return first + "/" + parts[0] + ".html" + ("#" + parts[1] if len(parts) > 1 else "")

def outline(html): # FIXME In its current state, this does not preserve HTML sub-formatting (such as superscript) in headers
	ret = []
	for section in html.children:
		if section.name != "section": continue
		if len(section.contents) == 0: continue
		if section.contents[0].name not in ["h1", "h2", "h3"]: continue
		ret.append((section["id"], section.contents[0].get_text(), outline(section)))
	return ret

def addtoc(html):
	if not html.h2: return
	contents = outline(html.body)
	if len(contents) <= 1: return
	toc = html.new_tag("div", id="toc")
	toc["class"] = "plainlist"
	toctitle = html.new_tag("h2")
	toctitle.string = "Contents"
	toc.append(toctitle)
	toclist = html.new_tag("ul")
	for h2 in contents:
		a = html.new_tag("a", href = "#" + h2[0])
		a.string = h2[1]
		li = html.new_tag("li")
		li.append(a)
		if (len(h2[2]) > 0):
			sublist = html.new_tag("ul")
			for h3 in h2[2]:
				a2 = html.new_tag("a", href = "#" + h3[0])
				a2.string = h3[1]
				li2 = html.new_tag("li")
				li2.append(a2)
				sublist.append(li2)
			li.append(sublist)
		toclist.append(li)
	toc.append(toclist)
	html.section.append(toc)

def resolve_styles(html):
	styles = {}
	for tag in html.find_all(True):
		if "style" not in tag.attrs: continue
		stmtlist = [ stmt.strip() for stmt in tag["style"].split(";") if len(stmt.strip()) > 0 ]
		if len(stmtlist) <= 1: continue
		styledef = ";".join(sorted(stmtlist))
		if styledef not in styles: styles[styledef] = "mwis%s" % (len(styles))
		if "class" in tag.attrs: tag["class"].append(styles[styledef])
		else: tag["class"] = styles[styledef]
		del tag["style"]
	css = ""
	for (style, cls) in styles.items():
		css += ".%s { %s; } " % (cls, style)
	styleblock = html.new_tag("style")
	styleblock.string = css
	html.head.append(styleblock)

def writeout(title, content, tracer):
	if not content: return
	tracer.processing()
	noproto = re.compile("^//")
	html = bs4.BeautifulSoup(content, "lxml")

	if html.base: html.base.extract()
	pagetitle = html.new_tag("h1")
	pagetitle.string = title
	html.body.insert(0, pagetitle)
	html.title.string = title
	addtoc(html)

	html.head.append(html.new_tag("meta", charset="utf-8")); # FIXME Does this work?  Some articles already have it; others appear not to
	html.head.append(html.new_tag("link", rel="stylesheet", href="../../" + cssname))
	for link in html("a", href=noproto) + html("link", href=noproto): # Stylesheet links
		link["href"] = "https:" + link["href"]
		if "rel" in link.attrs and "stylesheet" in link["rel"]: link.extract()
	for link in html("a", href=re.compile("^\./.*:")): # Links to other namespaces
		if link["href"].split(":", 1)[0][2:] in namespaces.values(): link["href"] = "../" + friendlyname(link["href"][2:])
		else: link["href"] = origin + link["href"][1:]
	for link in html("a", href=re.compile("^\./")): # Local article links
		link["href"] = "../" + friendlyname(link["href"][2:])
	for link in html("a", href=re.compile("^/wiki")): # Absolute article links
		# link["href"] = origin + link["href"][5:]
		link["href"] = "../" + friendlyname(link["href"][6:])
	for link in html("area", href=re.compile("^/wiki/")): # Image maps
		link["href"] = "../../wiki/" + friendlyname(link["href"][6:])
	for img in html("img"): # Article images
		src = img["src"]
		if noproto.match(src): src = "https:" + src
		elif re.compile("^/").match(src): src = origin_root + src
		elif re.compile("^\.\.").match(src): raise Exception("Found relative image link " + src)
		elif re.compile("^[a-z]*://").match(src): pass
		elif re.compile("^./Special:FilePath/").match(src): src = origin + src[1:]
		else: raise Exception("Unhandled image link case " + src)
		fname = urllib.parse.unquote(urllib.parse.urlparse(src)[2].split("/")[-1])
		if re.search("//wikimedia.org/api/rest_v1/media/math/render/svg", src): fname += ".svg"
		if not re.search("\.[a-zA-Z]{3,4}$", fname): raise Exception("File name missing extension: " + src);
		subkey = safe_chars[hashlib.md5(fname.encode("UTF-8")).digest()[0] % len(safe_chars)]
		img["src"] = os.path.join("..", "..", imgdir, subkey, fname)
		if os.path.isfile(os.path.join(rootdir, imgdir, subkey, fname)): continue
		if os.path.isfile(os.path.join(rootdir, oldimgdir, subkey, fname)):
			os.rename(os.path.join(rootdir, oldimgdir, subkey, fname), os.path.join(rootdir, imgdir, subkey, fname))
		else:
			tracer.image(src)
			for i in range(5):
				time.sleep(2 ** i)
				try:
					reply = requests.get(src, headers={"user-agent": useragent})
					if reply.status_code != 200: raise HTTPError(reply.status_code, src)
					destdir = os.path.join(rootdir, imgdir, subkey)
					if not os.path.exists(destdir): os.mkdir(destdir)
					with open(os.path.join(destdir, fname), "wb") as out: out.write(reply.content)
					break
				except:
					print("(retry fetching %s in %d seconds)" % (src, 2 ** i))

	tracer.cleaning()
	for tag in html.find_all(True):
		for attr in ["srcset", "data-mw", "about"]:
			if attr in tag.attrs: del tag[attr]
	for comment in html.find_all(string = lambda text: isinstance(text, bs4.Comment)):
		comment.extract()
	#resolve_styles(html); # Too much effort for negligible space savings

	dest = os.path.join(rootdir, htmldir, friendlyname(title))
	if not os.path.exists(os.path.dirname(dest)): os.mkdir(os.path.dirname(dest))
	with open(dest, "w") as out: out.write(str(html))

def download(title):
	fname = os.path.join(rootdir, htmldir, friendlyname(title))
	if os.path.lexists(fname): return None; # Do not re-download existing articles
	url = origin_root + "/api/rest_v1/page/html/%s?redirect=true"
	target = urllib.parse.quote(title, safe="")
	while True:
		#print("\nGET %s => " % (url % (target)), end="")
		res = requests.get(url % (target), allow_redirects=False, headers={"user-agent": useragent})
		#print(res.status_code)
		if res.status_code == 301: target = res.headers["location"]
		else: break
	if res.status_code == 302:
		location = urllib.parse.unquote(res.headers["location"]).split("#", 1)
		target = "../" + friendlyname(location[0]) + (("#" + location[1]) if len(location) > 1 else "")
		if not os.path.exists(os.path.dirname(fname)): os.mkdir(os.path.dirname(fname))
		with open(fname, "w") as outf:
			outf.write("<html><head><meta charset=\"utf8\"><title>%s</title><meta http-equiv=\"refresh\" content=\"0;url=%s\"></head></html>" % (title, target))
	elif res.status_code == 200:
		return res.text
	else:
		raise HTTPError(res.status_code, url % (target));

cnt = 0
def getpage(title, tracer):
	global cnt, logfile, interrupt; # Why?
	cnt += 1
	title = urllib.parse.unquote(title)
	print("\r\033[2K%d %s" % (cnt, title), end="")
	#print("%d %s" % (cnt, title))
	for i in range(5):
		if interrupt: break
		try:
			tracer.start(title)
			writeout(title, download(title), tracer)
			tracer.finish()
			break
		except Exception as e:
			if i < 4:
				time.sleep(4)
				continue
			print("\r\033[2K%d %s: %s" % (cnt, title, str(e)))
			logfile.write(timestr() + ": " + title + ":\n" + traceback.format_exc() + "\n")
			logfile.flush()
			break

interrupt = False
def articles(q, tag = (0, "")):
	global interrupt
	url = origin_root + "/w/api.php?format=json&action=query&list=allpages&aplimit=500&apnamespace=%d&apcontinue=%s"
	q.put((0, "Main Page"))
	while True:
		if interrupt: return
		req = requests.get(url % ([ i[0] for i in namespaces.items() ][tag[0]], urllib.parse.quote(tag[1])))
		req.encoding = "UTF-8"
		res = json.loads(req.text)
		try: tag = (tag[0], res["continue"]["apcontinue"])
		except KeyError:
			tag = (tag[0] + 1, "")
			if tag[0] >= len(namespaces): tag = None
		for item in res["query"]["allpages"]:
			while True:
				if interrupt: return
				try:
					q.put((0, item["title"]), timeout=1)
					break
				except queue.Full: continue
		if not tag: break
		q.put((1, tag))
	for i in range(concurrency): q.put((2, None))

resflock = threading.Lock()
savedtag = None
def resumewrite(tag):
	global resflock, savedtag
	with resflock:
		if savedtag:
			with open(resfpath, "w") as resf:
				resf.write("%d %s" % savedtag)
		savedtag = tag

def process(q):
	global interrupt
	tracer = tracing.Tracer()
	while True:
		if interrupt: break
		(cat, item) = q.get()
		if cat == 2: break
		elif cat == 1: resumewrite(item)
		elif cat == 0: getpage(item, tracer)
	tracer.exit()

def sigint(signum, frame):
	global interrupt, logfile
	if signum not in [2, 15]: return
	if interrupt: exit(1)
	interrupt = True
	print("\nCleaning up...")
	logfile.write(timestr() + ": Interrupted\n")

for path in [rootdir, os.path.join(rootdir, htmldir), os.path.join(rootdir, metadir)]:
	if not os.path.isdir(path): os.mkdir(path)
if not os.path.exists(resfpath):
	if os.path.exists(os.path.join(rootdir, oldimgdir)):
		input("Remove old image directory?  Enter to continue, ^C to exit ")
		shutil.rmtree(os.path.join(rootdir, oldimgdir))
	if os.path.exists(os.path.join(rootdir, imgdir)): os.rename(os.path.join(rootdir, imgdir), os.path.join(rootdir, oldimgdir))
if not os.path.exists(os.path.join(rootdir, imgdir)): os.mkdir(os.path.join(rootdir, imgdir))
if not os.path.exists(os.path.join(rootdir, cssname)): shutil.copy(os.path.join(os.path.dirname(os.path.realpath(__file__)), cssname), os.path.join(rootdir, cssname))
start = (0, "")
if os.path.isfile(resfpath):
	resf = open(resfpath)
	parts = resf.read().split(" ")
	start = (int(parts[0]), parts[1])
	resf.close()
logfile = open(logfname, "a")
logfile.write("\n== %s %s ==\n" % (site_name, timestr()))
tracing.init(tracefname)
if debug:
	tracer = tracing.Tracer()
	for page in debug_pages: getpage(page, tracer)
else:
	artq = queue.Queue(2048)
	signal.signal(signal.SIGINT, sigint)
	with concurrent.futures.ThreadPoolExecutor(concurrency + 1) as pool:
		res = []
		res.append(pool.submit(articles, artq, start))
		for i in range(concurrency): res.append(pool.submit(process, artq))
		(finished, unfinished) = concurrent.futures.wait(res, None, concurrent.futures.FIRST_EXCEPTION)
		for done in finished:
			if done.exception():
				interrupt = True
				raise done.exception()
print()
logfile.close()
tracing.cleanup()
if os.path.isfile(resfpath) and not interrupt: os.unlink(resfpath)
if interrupt: exit(0)

info = """params = {
	title = "%s",
	description = "%s",
	language = "eng",
	created = "%s",
	refer = "%s",
	origin = "%s;^wiki/./(.*).html$;wiki/$1",
	home = "%s/M/Main_Page.html",
}

function meta(path, ftype)
	ret = {}
	if ftype == T_REG then
		if is_html(path) then ret["title"] = html_title(path) end
		ret["type"] = mimetype(path)
	elseif ftype == T_LNK then
		if is_html(path) then ret["title"] = basename(path):gsub("_", " ") end
	end
	return ret
end
""" % (site_name, site_description, timestr("%Y-%m-%d"), origin_root, origin_root, htmldir)
infofile = open(os.path.join(rootdir, metadir, infoname), "w")
infofile.write(info)
infofile.close()
subprocess.call(["convert", "-density", "200", "-background", "none", faviconsrc, "-resize", "120x120", "-gravity", "center", "-extent", "128x128", os.path.join(rootdir, metadir, faviconname)], shell=False)
