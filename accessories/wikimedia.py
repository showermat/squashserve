#! /usr/bin/python

import sys
import lxml
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
import multiprocessing.pool
import concurrent.futures
import queue

# TODO
# It would be nice to download the category namespace for both Wikipedia and Wiktionary, but the HTML rendering appears to always be empty!
# Don't download redirects to pages that we're not downloading, like links to pages in namespaces
# Links in footnotes to where they were cited are broken
# Add a meta tag for the revision being retrieved, so in the future we can avoid refetching unchanged revisions (adds an extra GET and requires moving images; probably no faster)
# Eventually: postprocessing for redlinks?
#
# Problem pages:
# ...

debug = False
trace = False
debug_pages = ["Main Page", "China", "Hydronium", "Maxwell's equations", "Persimmon", "Alpha Centauri", "Boranes", "Busy signal",
	"Periodic table (large cells)", "Gallery of sovereign state flags", "Go (programming language)", "Foreign relations of China"]

sites = {
	"wikipedia": ("Wikipedia", "The free encyclopedia", "https://en.wikipedia.org", "https://upload.wikimedia.org/wikipedia/en/8/80/Wikipedia-logo-v2.svg", {0: ""}),
	"wiktionary": ("Wiktionary", "The free dictionary", "https://en.wiktionary.org", "https://upload.wikimedia.org/wikipedia/commons/0/06/Wiktionary-logo-v2.svg", {0: "", 100: "Appendix", 104: "Index", 110: "Thesaurus", 114: "Citations", 118: "Reconstruction"}),
}
rootdir = sys.argv[1]
if rootdir == "debug":
	debug = True
	trace = False
	rootdir = "wikipedia"
(site_name, site_description, origin_root, faviconsrc, namespaces) = sites[rootdir]

concurrency = 32
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
today = datetime.date.today().strftime("%Y-%m-%d")
useragent = "wikidump/0.4 robot contact:matthew.schauer@e10x.net (Please block temporarily and send e-mail if bandwidth limit exceeded)"

def friendlyname(fname):
	subs = {"\"": "[quote]", "/": "[slash]", " ": "_", "_+": "_"}
	parts = fname.split("#", 1)
	for (char, sub) in subs.items(): parts[0] = re.sub(char, sub, parts[0])
	return parts[0] + ".html" + ("#" + parts[1] if len(parts) > 1 else "")

def outline(html): # FIXME In the current state, this does not preserve HTML sub-formatting (such as superscript) in headers
	ret = []
	cur = None
	for child in html.body.children:
		if child.name == "h2":
			if cur:
				ret.append(cur)
				cur = None
			if "id" not in child.attrs: continue
			cur = (child["id"], child.get_text(), [])
		elif child.name == "h3":
			if "id" not in child.attrs: continue
			cur[2].append((child["id"], child.get_text()))
		else: continue
	if cur: ret.append(cur)
	return ret

def addtoc(html):
	if not html.h2: return
	contents = outline(html)
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
	html.h2.insert_before(toc)

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

def writeout(title, content):
	if not content: return
	noproto = re.compile("^//")
	html = bs4.BeautifulSoup(content, "lxml")

	if html.base: html.base.extract()
	pagetitle = html.new_tag("h1")
	pagetitle.string = title
	html.body.insert(0, pagetitle)
	html.title.string = title
	addtoc(html)

	html.head.append(html.new_tag("meta", charset="utf-8")); # FIXME Does this work?  Some articles already have it; others appear not to
	html.head.append(html.new_tag("link", rel="stylesheet", href="../" + cssname))
	for link in html("a", href=noproto) + html("link", href=noproto): # Stylesheet links
		link["href"] = "https:" + link["href"]
		if "rel" in link.attrs and "stylesheet" in link["rel"]: link.extract()
	for link in html("a", href=re.compile("^\./.*:")): # Links to other namespaces
		if link["href"].split(":", 1)[0][2:] in namespaces.values(): link["href"] = "./" + friendlyname(link["href"][2:])
		else: link["href"] = origin + link["href"][1:]
	for link in html("a", href=re.compile("^\./")): # Local article links
		link["href"] = "./" + friendlyname(link["href"][2:])
	for link in html("a", href=re.compile("^/wiki")): # Absolute article links
		# link["href"] = origin + link["href"][5:]
		link["href"] = "./" + friendlyname(link["href"][6:])
	for link in html("area", href=re.compile("^/wiki/")): # Image maps
		link["href"] = "../wiki/" + friendlyname(link["href"][6:])
	for img in html("img"): # Article images
		src = img["src"]
		if noproto.match(src): src = "https:" + src
		elif re.compile("^/").match(src): src = origin_root + src
		elif re.compile("^\.\.").match(src): raise Exception("Found relative image link " + src)
		elif re.compile("^[a-z]*://").match(src): pass
		else: raise Exception("Unhandled image link case " + src)
		img["src"] = src
		fname = urllib.parse.unquote(urllib.parse.urlparse(src)[2].split("/")[-1])
		if re.search("//wikimedia.org/api/rest_v1/media/math/render/svg", src): fname += ".svg"
		if os.path.isfile(os.path.join(rootdir, oldimgdir, fname)):
			os.rename(os.path.join(rootdir, oldimgdir, fname), os.path.join(rootdir, imgdir, fname))
			img["src"] = os.path.join("..", imgdir, fname)
			continue
		reply = requests.get(src, headers={"user-agent": useragent})
		if reply.status_code != 200: continue
		if not re.search("\.[a-zA-Z]{3,4}$", fname): raise Exception("File name missing extension: " + src); #fname += mimetypes.guess_extension(reply.headers["content-type"]); # I don't think this ever happens.
		out = open(os.path.join(rootdir, imgdir, fname), "wb")
		out.write(reply.content)
		out.close()
		img["src"] = os.path.join("..", imgdir, fname)

	for tag in html.find_all(True):
		for attr in ["srcset", "data-mw", "about"]:
			if attr in tag.attrs: del tag[attr]
	for comment in html.find_all(string = lambda text: isinstance(text, bs4.Comment)):
		comment.extract()
	#resolve_styles(html); # Too much effort for negligible space savings

	out = open(os.path.join(rootdir, htmldir, friendlyname(title)), "w")
	out.write(str(html))
	out.close()

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
		os.symlink("./" + friendlyname(urllib.parse.unquote(res.headers["location"])), fname)
	elif res.status_code == 200:
		return res.text
	else:
		raise Exception("HTTP %s" % (res.status_code)); # TODO Specialize this exception

cnt = 0
def getpage(title):
	global cnt, logfile, interrupt; # Why?
	cnt += 1
	title = urllib.parse.unquote(title)
	print("\r\033[2K%d %s" % (cnt, title), end="")
	#print("%d %s" % (cnt, title))
	for i in range(3):
		if interrupt: break
		try:
			writeout(title, download(title))
			break
		except Exception as e:
			if i < 2:
				time.sleep(4)
				continue
			print("\r\033[2K%d %s: %s" % (cnt, title, str(e)))
			logfile.write(datetime.datetime.today().strftime("%Y-%m-%d %H:%M:%S") + ": " + title + ": " + str(e) + "\n")
			logfile.flush()
			break

interrupt = False
def articles(q, tag = (0, "")):
	global interrupt
	try:
		url = origin_root + "/w/api.php?format=json&action=query&list=allpages&aplimit=500&apnamespace=%d&apcontinue=%s"
		q.put((0, "Main Page"))
		while True:
			if interrupt: return
			req = requests.get(url % ([ i[0] for i in namespaces.items() ][tag[0]], tag[1]))
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
	except Exception as e: print(e)

resflock = threading.Lock()
savedtag = None
def resumewrite(tag):
	global resflock, savedtag
	with resflock:
		if savedtag:
			resf = open(resfpath, "w")
			resf.write(savedtag[0] + " " + savedtag[1])
			resf.close()
		savedtag = tag

def process(q):
	global interrupt
	global traceout, tracelock, tracerank
	if trace:
		now = datetime.datetime.now().timestamp() - tracestart
		with tracelock:
			rank = tracerank
			tracerank += 1
			traceout.write("\n<circle cx=\"%d\" cy=\"%d\" r=\"%d\" fill=\"blue\" stroke=\"none\" />" % (now, rank * 20 + 10, 2))
	while True:
		if trace: start = datetime.datetime.now().timestamp() - tracestart
		if interrupt: break
		(cat, item) = q.get()
		if cat == 2: break
		elif cat == 1: resumewrite(item)
		elif cat == 0: getpage(item)
		if trace and cat == 0:
			dur = datetime.datetime.now().timestamp() - start - tracestart
			if dur > 1:
				with tracelock:
					traceout.write("\n<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" />" % (start, rank * 20, dur, 18))
					if dur > 100: out.write("<text x=\"%d\" y=\"%d\" fill=\"black\" stroke=\"none\">%s</text>" % (start + 2, rank * 20 + 10, item))
	if trace:
		now = datetime.datetime.now().timestamp() - tracestart
		with tracelock: traceout.write("\n<circle cx=\"%d\" cy=\"%d\" r=\"%d\" fill=\"red\" stroke=\"none\" />" % (now, rank * 20 + 10, 2))

def sigint(signum, frame):
	global interrupt
	if signum not in [2, 15]: return
	if interrupt: exit(1)
	interrupt = True
	print("\nCleaning up...")

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
logfile.write("\n== %s %s ==\n" % (site_name, today))
if trace:
	traceout = open(tracefname, "w")
	tracelock = threading.Lock()
	traceout.write('<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n')
	traceout.write('<svg xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:svg="http://www.w3.org/2000/svg">')
	traceout.write('<rect x="0" y="0" width="100%" height="100%" fill="white" />')
	traceout.write('<g fill="white" stroke="black" stroke-width="1">')
	tracestart = datetime.datetime.now().timestamp()
	tracerank = 0
if debug:
	for page in debug_pages: getpage(page)
else:
	artq = queue.Queue(2048)
	signal.signal(signal.SIGINT, sigint)
	with concurrent.futures.ThreadPoolExecutor(concurrency + 1) as pool:
		pool.submit(articles, artq, start)
		for i in range(concurrency): pool.submit(process, artq)
print()
logfile.close()
if trace:
	traceout.write("</g></svg>")
	traceout.close()
if os.path.isfile(resfpath) and not interrupt: os.unlink(resfpath)
if interrupt: exit(0)

info = """params = {
	title = "%s",
	description = "%s",
	language = "eng",
	created = "%s",
	refer = "%s",
	origin = "%s;^(.*).html$;$1",
	home = "%s/Main_Page.html",
}

function index(path, ftype)
	if not is_html(path) then return "" end
	if ftype == T_REG then return html_title(path) end
	if ftype == T_LNK then return basename(path):gsub("_", " ") end
	return ""
end
""" % (site_name, site_description, today, origin_root, origin_root, htmldir)
infofile = open(os.path.join(rootdir, metadir, infoname), "w")
infofile.write(info)
infofile.close()
subprocess.call(["convert", "-density", "200", "-background", "none", faviconsrc, "-resize", "120x120", "-gravity", "center", "-extent", "128x128", os.path.join(rootdir, metadir, faviconname)], shell=False)
