#! /usr/bin/python

import sys;
import lxml;
import bs4;
import re;
import os.path;
import shutil;
import requests;
import urllib.parse;
import json;
import time;
import signal;
import datetime;
import subprocess;
import threading;
import multiprocessing.pool;
import concurrent.futures;
import queue;

# TODO
# IMPORTANT: vet namespaces and allow some to be included per site: (appendix, wikisaurus, wiktionary, category) in Wiktionary, possibly (category) in Wikipedia?
# Don't download redirects to pages that we're not downloading, like links to pages in namespaces
# Links to anchors in other pages are based on section name, whereas the anchors themselves are randomized names -- so the link doesn't work
# Links in footnotes to where they were cited are broken
# Add a meta tag for the revision being retrieved, so in the future we can avoid refetching unchanged revisions
# Table of contents only if there's more than one section (or more than two?)
# Multi-level bulleted lists have the same left margin at all levels
# Remove gratuitous metadata from HTML to decrease output size
# 
# Problem pages:
# Interpleader: bulleted lists in infobox at right

sites = {
	"wikipedia": ("Wikipedia", "The free encyclopedia", "https://en.wikipedia.org", "https://upload.wikimedia.org/wikipedia/en/8/80/Wikipedia-logo-v2.svg"),
	"wiktionary": ("Wiktionary", "The free dictionary", "https://en.wiktionary.org", "https://upload.wikimedia.org/wikipedia/commons/0/06/Wiktionary-logo-v2.svg"),
}
rootdir = sys.argv[1];
(site_name, site_description, origin_root, faviconsrc) = sites[rootdir];

concurrency = 32;
apiurl = origin_root + "/api/rest_v1";
origin = origin_root + "/wiki";
imgdir = "img";
oldimgdir = "img_old";
htmldir = "wiki";
metadir = "_meta";
infoname = "info.txt";
faviconname = "favicon.png";
cssname = "wikistyle.css";
logfname = os.path.join(rootdir, "dump.log");
resfpath = os.path.join(rootdir, "resume.txt");
useragent = "wikidump/0.3 contact:matthew.schauer.x@gmail.com (Please block temporarily and send email if bandwidth limit exceeded)"

def friendlyname(fname):
	subs = {"\"": "[quote]", "/": "[slash]", " ": "_", "_+": "_"};
	parts = fname.split("#", 1);
	for (char, sub) in subs.items(): parts[0] = re.sub(char, sub, parts[0]);
	return parts[0] + ".html" + ("#" + parts[1] if len(parts) > 1 else "");

def outline(html): # FIXME In the current state, this does not preserve HTML sub-formatting (such as superscript) in headers
	ret = [];
	cur = None;
	for child in html.body.children:
		if child.name == "h2":
			if cur:
				ret.append(cur);
				cur = None;
			if "id" not in child.attrs: continue;
			cur = (child["id"], child.get_text(), []);
		elif child.name == "h3":
			if "id" not in child.attrs: continue;
			cur[2].append((child["id"], child.get_text()));
		else: continue;
	if cur: ret.append(cur);
	return ret;

def addtoc(html):
	if not html.h2: return;
	toc = html.new_tag("div", id="toc");
	toc["class"] = "plainlist";
	toctitle = html.new_tag("h2")
	toctitle.string = "Contents";
	toc.append(toctitle);
	toclist = html.new_tag("ul");
	for h2 in outline(html):
		a = html.new_tag("a", href = "#" + h2[0]);
		a.string = h2[1];
		li = html.new_tag("li");
		li.append(a);
		if (len(h2[2]) > 0):
			sublist = html.new_tag("ul");
			for h3 in h2[2]:
				a2 = html.new_tag("a", href = "#" + h3[0]);
				a2.string = h3[1];
				li2 = html.new_tag("li");
				li2.append(a2);
				sublist.append(li2);
			li.append(sublist);
		toclist.append(li);
	toc.append(toclist);
	html.h2.insert_before(toc);

def writeout(title, content):
	if not content: return;
	noproto = re.compile("^//");
	html = bs4.BeautifulSoup(content, "lxml");

	if html.base: html.base.extract();
	pagetitle = html.new_tag("h1");
	pagetitle.string = title;
	html.body.insert(0, pagetitle);
	html.title.string = title;
	addtoc(html);

	html.head.append(html.new_tag("meta", charset="utf-8")); # FIXME Does this work?  Some articles already have it; others appear not to
	html.head.append(html.new_tag("link", rel="stylesheet", href="../" + cssname));
	for link in html("a", href=noproto) + html("link", href=noproto): # Stylesheet links
		link["href"] = "https:" + link["href"];
		if "rel" in link.attrs and "stylesheet" in link["rel"]: link.extract();
	for link in html("a", href=re.compile("^\./.*:")): # Links to other namespaces
		link["href"] = origin + link["href"][1:];
	for link in html("a", href=re.compile("^\./")): # Local article links
		link["href"] = "./" + friendlyname(link["href"][2:]);
	for link in html("a", href=re.compile("^/wiki")): # Absolute article links
		# link["href"] = origin + link["href"][5:];
		link["href"] = "./" + friendlyname(link["href"][6:]);
	for link in html("area", href=re.compile("^/wiki/")): # Image maps
		link["href"] = "../wiki/" + friendlyname(link["href"][6:]);
	for img in html("img"): # Article images
		src = img["src"];
		if noproto.match(src): src = "https:" + src;
		elif re.compile("^/").match(src): src = origin_root + src;
		elif re.compile("^\.\.").match(src): raise Exception("Found relative image link " + src);
		elif re.compile("^[a-z]*://").match(src): pass;
		else: raise Exception("Unhandled image link case " + src);
		img["src"] = src;
		fname = urllib.parse.unquote(urllib.parse.urlparse(src)[2].split("/")[-1]);
		if re.search("//wikimedia.org/api/rest_v1/media/math/render/svg", src): fname += ".svg";
		if os.path.isfile(os.path.join(rootdir, oldimgdir, fname)):
			os.rename(os.path.join(rootdir, oldimgdir, fname), os.path.join(rootdir, imgdir, fname));
			img["src"] = os.path.join("..", imgdir, fname);
			continue;
		reply = requests.get(src, headers={"user-agent": useragent});
		if reply.status_code != 200: continue;
		if not re.search("\.[a-zA-Z]{3,4}$", fname): raise Exception("File name missing extension: " + src); #fname += mimetypes.guess_extension(reply.headers["content-type"]); # I don't think this ever happens.
		out = open(os.path.join(rootdir, imgdir, fname), "wb");
		out.write(reply.content);
		out.close();
		img["src"] = os.path.join("..", imgdir, fname);

	out = open(os.path.join(rootdir, htmldir, friendlyname(title)), "w");
	out.write(str(html));
	out.close();

def download(title):
	fname = os.path.join(rootdir, htmldir, friendlyname(title));
	if os.path.lexists(fname): return None; # Do not re-download existing articles
	url = apiurl + "/page/html/%s?redirect=true";
	target = urllib.parse.quote(title, safe="");
	while True:
		#print("\nGET %s => " % (url % (target)), end="");
		res = requests.get(url % (target), allow_redirects=False, headers={"user-agent": useragent});
		#print(res.status_code);
		if res.status_code == 301: target = res.headers["location"];
		else: break;
	if res.status_code == 302:
		os.symlink("./" + friendlyname(urllib.parse.unquote(res.headers["location"])), fname);
	elif res.status_code == 200:
		return res.text;
	else:
		raise Exception("HTTP %s" % (res.status_code)); # TODO Specialize this exception

cnt = 0;
def getpage(title):
	global cnt, logfile, interrupt; # Why?
	cnt += 1;
	title = urllib.parse.unquote(title);
	print("\r\033[2K%d %s" % (cnt, title), end="");
	# print("%d %s" % (cnt, title));
	for i in range(3):
		if interrupt: break;
		try:
			writeout(title, download(title));
			break;
		except Exception as e:
			if i < 2:
				time.sleep(4);
				continue;
			print("\r\033[2K%d %s: %s" % (cnt, title, str(e)));
			logfile.write(title + ": " + str(e) + "\n");
			logfile.flush();
			break;

def fetch_artlist(tag):
	url = apiurl + "/page/title/%s";
	res = requests.get(url % (tag));
	res.encoding = "UTF-8";
	return json.loads(res.text);

interrupt = False;
def articles(q, tag = ""):
	global interrupt;
	q.put((0, "Main Page"));
	while True:
		if interrupt: return;
		res = fetch_artlist(tag);
		try: tag = res["_links"]["next"]["href"];
		except KeyError: tag = None;
		for item in res["items"]:
			q.put((0, item));
		if not tag: break;
		q.put((1, tag));
	for i in range(concurrency): q.put((2, None));

resflock = threading.Lock();
savedtag = None;
def resumewrite(tag):
	global resflock, savedtag;
	resflock.acquire();
	if savedtag:
		resf = open(resfpath, "w");
		resf.write(savedtag);
		resf.close();
	savedtag = tag;
	resflock.release();

def process(q):
	global interrupt;
	while True:
		if interrupt: break;
		(cat, item) = q.get();
		if cat == 2: break;
		elif cat == 1: resumewrite(item);
		elif cat == 0: getpage(item);

def sigint(signum, frame):
	global interrupt;
	if signum not in [2, 15]: return;
	if interrupt: exit(1);
	interrupt = True;
	print("\r\033[2KCleaning up...");

for path in [rootdir, os.path.join(rootdir, htmldir), os.path.join(rootdir, metadir)]:
	if not os.path.isdir(path): os.mkdir(path);
if not os.path.exists(resfpath):
	if os.path.exists(os.path.join(rootdir, oldimgdir)):
		input("Remove old image directory?  Enter to continue, ^C to exit ");
		shutil.rmtree(os.path.join(rootdir, oldimgdir));
	if os.path.exists(os.path.join(rootdir, imgdir)): os.rename(os.path.join(rootdir, imgdir), os.path.join(rootdir, oldimgdir));
if not os.path.exists(os.path.join(rootdir, imgdir)): os.mkdir(os.path.join(rootdir, imgdir));
if not os.path.exists(os.path.join(rootdir, cssname)): shutil.copy(os.path.join(os.path.dirname(os.path.realpath(__file__)), cssname), os.path.join(rootdir, cssname));
start = "";
if os.path.isfile(resfpath):
	resf = open(resfpath);
	start = resf.read();
	resf.close();
logfile = open(logfname, "a");
# for page in ["Main Page", "China", "Hydronium", "Maxwell's equations", "Persimmon", "Alpha Centauri", "Boranes"]: getpage(page);
# for page in ["Periodic table (large cells)", "Gallery of sovereign state flags", "Go (programming language)"]: getpage(page);
# print();
# exit(0);

artq = queue.Queue(2048);
signal.signal(signal.SIGINT, sigint);
pool = concurrent.futures.ThreadPoolExecutor(concurrency + 1);
pool.submit(articles, artq, start);
for i in range(concurrency): pool.submit(process, artq);
pool.shutdown();
print();
logfile.close();
if os.path.isfile(resfpath) and not interrupt: os.unlink(resfpath);
if interrupt: exit(0);

info = """
title:%s
description:%s
language:eng
created:%s
refer:%s
origin:%s;^/(.*).html$;\$1
home:%s/Main_Page.html
""" % (site_name, site_description, datetime.date.today().strftime("%Y-%m-%d"), origin_root, origin_root, htmldir);
infofile = open(os.path.join(rootdir, metadir, infoname), "w");
infofile.write(info);
infofile.close();
subprocess.call(["convert", "-density", "200", "-background", "none", faviconsrc, "-resize", "120x120", "-gravity", "center", "-extent", "128x128", os.path.join(rootdir, metadir, faviconname)], shell=False);

