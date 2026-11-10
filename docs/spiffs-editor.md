# ESP SPIFFS editor

The ESP32, ESP32-S3, ESP32-C3 and ESP8266 components provide an `Open SPIFFS...`
action in the component context menu. It opens the SPIFFS file editor for the
flash image.

The editor provides Text and Hex views. Common text and source extensions such
as `txt`, `csv`, `json`, `xml`, `html`, `c` and `py` open in Text mode by default;
other files open in Hex mode. Both views can be edited when a writable runtime
copy is available.

The editor can create empty text or binary files, import files from the host,
export selected files to the host, and delete files. It also displays used, free
and total SPIFFS space.

Changes are stored in a writable runtime copy and do not modify the original
firmware image. Images without a detected SPIFFS partition remain read-only.
