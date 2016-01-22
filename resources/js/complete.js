var match = function(id)
{
	return new Bloodhound({
		datumTokenizer: function (d) { return Bloodhound.tokenizers.whitespace(d.value); },
		queryTokenizer : Bloodhound.tokenizers.whitespace,
		remote : { url : "/complete/" + id + "/%QUERY", wildcard : "%QUERY" }
	});
};

function autocomplete(elem, volid)
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
		limit : 10,
		templates : {
			suggestion : function(data) {
				return "<p><a href='/view/" + volid + data.url + "'>" + data.title + "</a></p>";
			}
		}
	});
}

