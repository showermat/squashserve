function match(id)
{
	return new Bloodhound({
		datumTokenizer: function (d) { return Bloodhound.tokenizers.whitespace(d.value); },
		queryTokenizer : Bloodhound.tokenizers.whitespace,
		remote : { url : "/complete/" + id + "/%QUERY", wildcard : "%QUERY", rateLimitWait : 500 }
	});
}

function clearform(volid)
{
	$("form.search[data-volid=" + volid + "] input.search-input").typeahead("val", ""); // Is this the best way to do it?
	return true;
}

function autocomplete(elem, volid, newtab = false)
{
	var matcher = match(volid);
	matcher.initialize();
	elem.typeahead({
		highlight : true,
		hint : false,
		minLength : 2,
	}, {
		displayKey : "title",
		source : matcher.ttAdapter(),
		limit : 100,
		templates : {
			suggestion : function(data) { return "<p><a href='" + data.url + "' onclick='clearform(\"" + volid + "\")' " + (newtab ? " target='_blank'" : "") + ">" + data.title + "</a></p>"; },
			//footer : "<p class='tt-footer'><a href='/titles/" + volid + "/'>See all</a></p>"
		}
	});
	//$("p.tt-footer a").on("click", function() { $(this).attr("href", $(this).attr("href") + elem.val()); })
}

