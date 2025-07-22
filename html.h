/*
 * (C) 2013-2025 by Christian Hesse <mail@eworm.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef _HTML_H
#define _HTML_H

/* This is used for default documents. Usually you will not see this anyway. */
#define PAGE307 \
	"<html><head><title>307 temporary redirect</title>" \
	"</head><body>307 temporary redirect: " \
	"<a href=\"%s\">%s</a></body></html>"
#define PAGE404 \
	"<html><head><title>404 Not Found</title>" \
	"</head><body>404 Not Found: %s</body></html>"

/* status page */
#define CIRCLE_GREEN	"&#x1F7E2;"
#define CIRCLE_YELLOW	"&#x1F7E1;"
#define CIRCLE_ORANGE	"&#x1F7E0;"
#define CIRCLE_RED	"&#x1F534;"
#define CIRCLE_BLUE	"&#x1F535;"

#define STATUS_HEAD \
	"<!DOCTYPE html><html lang=\"en\">" \
	"<head><title>pacredir status</title>" \
	"<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">" \
	"<meta http-equiv=\"refresh\" content=\"30\">" \
	"<style>body { font-family: sans-serif; } " \
	"h1 { padding-left: 36px; background-image: url(\"/favicon.png\"); background-repeat: no-repeat; } " \
	"h2 a { color: black; } " \
	"a { text-decoration: none; } " \
	"a:hover { text-decoration: underline; } " \
	"th { background: #efefef; } " \
	"td { text-align: center; padding: 1px 5px; } " \
	"tr:nth-child(even) { background: #dfdfdf; } " \
	"tr:nth-child(odd) { background: #efefef; } " \
	"tr:hover { background: #dfefef; }</style>" \
	"<link rel=\"icon\" href=\"/favicon.png \" type=\"image/png\">" \
	"</head><body><h1>pacredir status</h1>" \
	"<p>This is <code>pacredir</code> version <i>" VERSION "</i> running on <i>%s</i>. " \
	"Visit <a href=\"" WEBURL "\">" WEBSITE "</a> for documentation.</p>" \
	"<table>" \
	"<tr><td>Distribution:</td><td><b>" ID "</b></td></tr>" \
	"<tr><td>Architecture:</td><td><b>" ARCH "</b></td></tr>" \
	"<tr><td>Redirects:</td><td><b>%d</b></td></tr>" \
	"<tr><td>Not found:</td><td><b>%d</b></td></tr>" \
	"<tr><td>Over all:</td><td>%s</td></tr>" \
	"</table>"

#define STATUS_INT_HEAD \
	"<h2 id=\"ignored-interfaces\"><a href=\"#ignored-interfaces\">Ignored interfaces</a></h2>" \
	"<table><tr><th>interface</th><th>link</th></tr>"
#define STATUS_INT_ONE \
	"<tr><td>%s</td><td>%d</td></tr>"
#define STATUS_INT_ONE_NA \
	"<tr><td>%s</td><td>" CIRCLE_ORANGE "</td></tr>"
#define STATUS_INT_NONE \
	"<tr><td colspan=2>(none)</td></tr>"
#define STATUS_INT_FOOT \
	"</table>"

#define STATUS_HOST_HEAD \
	"<h2 id=\"hosts\"><a href=\"#hosts\">Hosts</a></h2>" \
	"<table><tr>" \
	"<th>host</th>" \
	"<th>port</th>" \
	"<th colspan=2>state</th>" \
	"<th colspan=2>finds</th>" \
	"<th colspan=2>bad</th></tr>"
#define STATUS_HOST_ONE \
	"<tr>" \
	"<td>%s</td>" \
	"<td>%d</td>" \
	"<td>%s</td><td>%s</td>" \
	"<td>%s</td><td>%d</td>" \
	"<td>%s</td><td>%d</td></tr>"
#define STATUS_HOST_NONE \
	"<tr><td colspan=8>(none)</td></tr>"
#define STATUS_HOST_FOOT \
	"</table>"

#define STATUS_FOOT \
	"</body></html>"

#endif /* _HTML_H */
