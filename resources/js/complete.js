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
	var elem = $("form.search[data-volid=" + volid + "] input.search-input")
	elem.typeahead("val", "");
	elem.blur();
	return true;
}

function autocomplete(elem, volid, newtab)
{
	var matcher = match(volid);
	matcher.initialize();
	elem.typeahead({
		highlight : true,
		hint : false,
		minLength : 3,
	}, {
		displayKey : "title",
		source : matcher.ttAdapter(),
		limit : 100,
		templates : {
			suggestion : function(data) { return "<p><a href=\"" + data.url + "\" onclick='clearform(\"" + volid + "\")' " + (newtab ? " target='_blank'" : "") + ">" + data.title + "</a></p>"; },
			//footer : "<p class='tt-footer'><a href='/titles/" + volid + "/'>See all</a></p>"
		}
	});
	//$("p.tt-footer a").on("click", function() { $(this).attr("href", $(this).attr("href") + elem.val()); })
}

function search_setup(page, input, submit, newtab = false)
{
	$(input).val("");
	$(input).each(function() { autocomplete($(this), $(this).parent().data("volid"), newtab); });
	$(submit).on('submit', function() {
		var query = $(this).find(input).val();
		var location = "/" + page + "/" + $(this).data("volid") + "/" + encodeURIComponent(query);
		if (newtab)
		{
			window.open(location);
			clearform($(this).data("volid"));
		}
		else window.location = location;
		return false;
	});
}
