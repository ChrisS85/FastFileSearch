FastFileSearch
==============

Extremely fast file search using the NTFS USN journal. The performance is comparable to the (probably well-known) program Everything (www.voidtools.com). Since this program wasn't open source and I didn't know of any free library for this task I wrote this one.

It is targeted at being usable from other languages than C++, so the data types in the exported functions of the DLL are a bit friendlier to use.

The project includes a test AutoHotkey script file ("FileSearchTest.ahk") in the Release directory that can be used to see how the library is used.