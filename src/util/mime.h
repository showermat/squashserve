#ifndef UTIL_MIME_H
#define UTIL_MIME_H
#include <map>
#include <string>

/* I could use a ridiculously long list of every MIME type ever, but this plus
 * libmagic should take care of nearly all cases.  Thanks to
 * http://lwp.interglacial.com/appc_01.htm for the original version of this
 * list.
 */

const std::map<std::string, std::string> mime_types{
	{"au", "audio/basic"},
	{"avi", "video/avi"},
	{"bmp", "image/bmp"},
	{"bz2", "application/x-bzip2"},
	{"css", "text/css"},
	{"dtd", "application/xml-dtd"},
	{"doc", "application/msword"},
	{"exe", "application/octet-stream"},
	{"gif", "image/gif"},
	{"gz", "application/x-gzip"},
	{"hqx", "application/mac-binhex40"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"ico", "image/x-icon"},
	{"jar", "application/java-archive"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"js", "application/x-javascript"},
	{"mid", "audio/x-midi"},
	{"midi", "audio/x-midi"},
	{"mov", "video/quicktime"},
	{"mp3", "audio/mpeg"},
	{"mpeg", "video/mpeg"},
	{"ogg", "audio/vorbis"},
	{"pdf", "application/pdf"},
	{"php", "application/x-httpd-php"},
	{"pl", "application/x-perl"},
	{"png", "image/png"},
	{"ppt", "application/vnd.ms-powerpoint"},
	{"ps", "application/postscript"},
	{"py", "application/x-python"},
	{"qt", "video/quicktime"},
	{"ra", "audio/x-pn-realaudio"},
	{"ram", "audio/x-pn-realaudio"},
	{"rdf", "application/rdf"},
	{"rtf", "application/rtf"},
	{"sgml", "text/sgml"},
	{"sit", "application/x-stuffit"},
	{"svg", "image/svg+xml"},
	{"swf", "application/x-shockwave-flash"},
	{"gz", "application/x-tar"}, // FIXME This is a problem.  It should be .tar.gz, but I'm only checking the portion after the last period.
	{"tgz", "application/x-tar"},
	{"tiff", "image/tiff"},
	{"tsv", "text/tab-separated-values"},
	{"txt", "text/plain"},
	{"wav", "audio/x-wav"},
	{"xls", "application/vnd.ms-excel"},
	{"xml", "application/xml"},
	{"xpm", "image/x-pixmap"},
	{"xz", "application/x-xz"},
	{"zip", "application/zip,"}
};

#endif

