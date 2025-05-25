#include "html.h"

// HTML Template with status message support
const char *HTML_PAGE =
"<html>\n"
"<head>\n"
"<title>WiFi Config</title>\n"
"<style>\n"
"body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; padding: 20px; }\n"
"h1 { color: #333; }\n"
".container { display: flex; flex-direction: column; align-items: center; gap: 20px; }\n"
"form { background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0px 0px 10px rgba(0, 0, 0, 0.1); width: 300px; }\n"
"input, select { padding: 10px; margin-top: 10px; width: 100%; border: 1px solid #ccc; border-radius: 5px; }\n"
"input[type=submit] { background: #007BFF; color: white; border: none; cursor: pointer; font-size: 18px; padding: 8px; width: 200px; }\n"
"input[type=submit]:hover { background: #0056b3; }\n"
"p { font-size: 14px; font-weight: bold; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<h1>WiFi Configuration</h1>\n"
"<div class=\"container\">\n"

"<form action=\"/connect\" method=\"post\">\n"
"<label for=\"ssid\">Select WiFi:</label>\n"
"<select name=\"ssid\">\n"
"%s" // WiFi options placeholder
"</select><br><br>\n"
"<label for=\"password\">Password:</label>\n"
"<input type=\"password\" name=\"password\"><br><br>\n"
"<input type=\"submit\" value=\"Connect\">\n"
"</form>\n"

"<form action=\"/scan\" method=\"get\">\n"
"<input type=\"submit\" value=\"Scan for Networks\">\n"
"</form>\n"

"<form action=\"/register\" method=\"post\">\n"
"<label for=\"key\">Key:</label>\n"
"<input type=\"text\" name=\"key\" maxlength=\"16\"><br>\n"
"<label for=\"sensorID\">Sensor ID:</label>\n"
"<input type=\"text\" name=\"sensorID\" maxlength=\"8\"><br>\n"
"<label for=\"geoutm\">Geoutm:</label>\n"
"<input type=\"text\" name=\"geoutm\" maxlength=\"64\"><br>\n"
"<input type=\"submit\" value=\"Register Device\">\n"
"</form>\n"

"</div>\n"
"<p style=\"color: %s;\">%s</p>\n" // Status message
"</body></html>\n";
