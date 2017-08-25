
$(document).ready(function() {
	$("#search-input").val("");
	autocomplete($("#search-input"), $("#search").data("volid"));
	$("#search").on('submit', function() {
		var query = $("#search-input").val();
		window.location = "/titles/" + $(this).data("volid") + "/" + encodeURIComponent(query);
		//$(this).children(".search-input").val("");
		return false;
	});
});

