
var primary_attr = {"title" : "Title", location : "HTML directory", home : "Home HTML", favicon: "Icon image"};
var secondary_attr = ["description", "language", "created", "refer"];

$(document).ready(function() {
	var table = $("#add-attributes");
	for (var attr in primary_attr) table.append("<tr><td><span class=\"form-key\">" + primary_attr[attr] + "</span></td><td><input type=\"text\" name=\"" + attr + "\" class=\"form-value\"></td><td>&nbsp;</td></tr>");
	var attrcnt = 0;
	for (var attr in secondary_attr)
	{
		table.append("<tr><td><input type=\"text\" name=\"key_" + attrcnt + "\" value=\"" + secondary_attr[attr] + "\" class=\"form-key\"></td><td><input type=\"text\" name=\"value_" + attrcnt + "\" class=\"form-value\"></td><td><a href=\"#\" class=\"form-action row-remove\"><img src=\"/rsrc/icon/remove.svg\"></a></td></tr>");
		attrcnt += 1;
	}
	$("#row-add").on('click', function() {
		table.append("<tr><td><input type=\"text\" name=\"key_" + attrcnt + "\" class=\"form-key\"></td><td><input type=\"text\" name=\"value_" + attrcnt + "\" class=\"form-value\"></td><td><a href=\"#\" class=\"form-action row-remove\"><img src=\"/rsrc/icon/remove.svg\"></a></td></tr>");
		attrcnt += 1;
		$(".row-remove").on('click', function() {
			$(this).parent().parent().remove();
			return false;
		});
	});
	$(".row-remove").on('click', function() {
		$(this).parent().parent().remove();
		return false;
	});
});
